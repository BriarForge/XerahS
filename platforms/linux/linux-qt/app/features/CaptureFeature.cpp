// CORE-007, EU-001: capture commands share one QAction across the tray, window and menus.
#include "shell/ErrorPresentation.h"
#include "features/CaptureFeature.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "CapturePermissionHelp.h"
#include "CaptureRecoveryActions.h"
#include "CaptureWorkflows.h"
#include "EditorWindow.h"
#include "NativeWorkflowExecutions.h"
#include "RecordingController.h"
#include "ScrollCapture.h"
#include "WorkflowShortcutActions.h"
#include "features/RecordingFeature.h"
#include "settings/ApplicationSettings.h"
#include "capture/LastRegionStore.h"
#include <QStandardPaths>
#include <QDir>

#include <QApplication>
#include <QStringList>
#include <QMenu>
#include <QScopedValueRollback>
#include <QSystemTrayIcon>
#include <QUuid>

namespace xerahs::app {

xerahs::workflows::WorkflowChoice resolveShellWorkflow(AppContext &ctx, const QUuid &id) {
  auto choice = ctx.workflows->resolve(id);
  if (choice.workflow) choice.workflow = xerahs::settings::withApplicationDefaults(*choice.workflow,
      ctx.settings.current().values, ctx.workflows->store().hasSavedDefinitions());
  return choice;
}

namespace {

using xerahs::shell::ActivationAction;
using xerahs::shell::ActivationReplyState;
using xerahs::shell::ActivationRequest;
using xerahs::shell::makeActivationRequest;

CommandSurfaces captureSurfaces() {
  return CommandSurface::Tray | CommandSurface::Window | CommandSurface::CaptureMenu;
}

QAction *addCapture(AppContext &ctx, const QString &id, const QString &label, const std::function<void()> &trigger) {
  auto *action = ctx.commands.add(CommandSpec{id, label, CommandGroup::Capture, captureSurfaces()});
  if (action && trigger) QObject::connect(action, &QAction::triggered, &ctx.app, trigger);
  return action;
}


ApplicationWindow *commandWindow(AppContext &ctx) {
  return qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr);
}

// CORE-015: target, strategy, progress and partial handling live in ScrollCapture;
// this wires it to the controller's admission and the command registry.
void installScrollingCapture(AppContext &ctx) {
  auto ports = ScrollCapture::nativePorts(&ctx.app, [&ctx] { return ctx.capture.saveDirectory(); });
  ports.selectRegion = [&ctx](std::function<void(QRectF)> selected) { ctx.capture.selectRecordingRegion(std::move(selected)); };
  ports.acquire = [&ctx](std::function<void()> onCancel) { return ctx.capture.acquireExternal(std::move(onCancel)); };
  ports.release = [&ctx] { ctx.capture.releaseExternal(); };
  ports.submit = [&ctx](const QImage &image) { return ctx.capture.submitExternalImage(image, QStringLiteral("scrolling")); };
  ports.notify = [&ctx](const QString &title, const QString &body, bool success) {
    postTrayMessage(ctx, title, body, success, 5000);
  };
  auto *scrolling = new ScrollCapture(std::move(ports), {}, &ctx.app);
  scrolling->setPreferences(settings::capturePreferencesPort(ctx.settings));  // CORE-015 settings
  addCapture(ctx, QStringLiteral("capture-scrolling"), QStringLiteral("Capture scrolling…"), [&ctx, scrolling] {
    if (ctx.capture.busy() || ctx.recording.busy() || QApplication::activeModalWidget()) {
      if (auto *window = commandWindow(ctx)) window->showResult(QStringLiteral("Activation"), activationFailureMessage(QStringLiteral("activation-busy")), false);
      return;
    }
    scrolling->start();
  });
}

}  // namespace

QString activationFailureMessage(const QString &diagnostic) { return xerahs::shell::errorMessage(diagnostic); }

std::function<void()> prepareShellCapture(AppContext &ctx, const ActivationRequest &incoming) {
  auto choice = incoming.action == ActivationAction::RunWorkflow ? resolveShellWorkflow(ctx, QUuid(incoming.workflowId)) : resolveShellWorkflow(ctx);
  if (!choice.workflow) return {};
  choice.workflow->chooseActions |= incoming.action == ActivationAction::CaptureRegionChooseActions;
  // CORE-014: source commands reuse the default workflow's actions and output.
  if (incoming.action == ActivationAction::CaptureFullScreen) choice.workflow->capture = QStringLiteral("full-screen");
  else if (incoming.action == ActivationAction::CaptureMonitor) choice.workflow->capture = QStringLiteral("monitor");
  else if (incoming.action == ActivationAction::CaptureActiveWindow) choice.workflow->capture = QStringLiteral("active-window");
  else if (incoming.action != ActivationAction::RunWorkflow) choice.workflow->capture = QStringLiteral("region");
  static NativeWorkflowExecutions executions(NativeWorkflowExecutions::defaultDirectory());
  auto run = ctx.capture.prepareWorkflowCapture(*choice.workflow, incoming.id, ctx.invocationOrigin, executions.writer());
  if (!run) return {};
  return [&ctx, run = std::move(run)] {
    if (auto *window = commandWindow(ctx)) window->activate();
    run();
  };
}

std::unique_ptr<xerahs::shell::ActivationDispatcher> createShellDispatcher(AppContext &ctx) {
  using xerahs::shell::ActivationOperations;
  ActivationOperations operations;
  operations.activate = [&ctx] { if (auto *window = commandWindow(ctx)) window->activate(); };
  operations.captureRegion = [&ctx] { ctx.capture.captureRegion(); };
  operations.editImage = [](const QString &path) { if (auto *window = EditorWindow::open(path)) window->show(); };
  operations.captureBusy = [&ctx] { return ctx.capture.busy() || ctx.recording.busy(); };
  operations.interactionBlocked = [&ctx] {
    return QApplication::activeModalWidget() != nullptr || (ctx.shortcuts && ctx.shortcuts->pending());
  };
  operations.prepareCapture = [&ctx](const ActivationRequest &incoming) { return prepareShellCapture(ctx, incoming); };
  operations.startRecording = [&ctx] { startScreenRecording(ctx); };
  operations.stopRecording = [&ctx] { ctx.recording.stop(); };
  operations.canStartRecording = [&ctx] {
    return ctx.recording.unavailable(xerahs::recording::Format::Mp4).isEmpty()
        || ctx.recording.unavailable(xerahs::recording::Format::WebM).isEmpty();
  };
  operations.startRegionRecording = [&ctx] { startRegionRecording(ctx); };
  auto dispatcher = std::make_unique<xerahs::shell::ActivationDispatcher>(std::move(operations));
  ctx.activation = dispatcher.get();
  ctx.invoke = [&ctx](ActivationAction action, const QString &path, bool chooseActions, const QString &workflowId, const QString &source) {
    const QScopedValueRollback<QString> origin(ctx.invocationOrigin,
        ctx.invocationOrigin == QLatin1String("global-shortcut") ? ctx.invocationOrigin : source);
    const auto local = makeActivationRequest(chooseActions ? ActivationAction::CaptureRegionChooseActions : action, path, workflowId);
    const auto admitted = ctx.activation->admit(local);
    auto *window = commandWindow(ctx);
    const auto fail = [&](const QString &code) {
      if (!window) return;
      window->showResult(QStringLiteral("Activation"), activationFailureMessage(code), false);
      window->activate();
    };
    if (admitted == ActivationReplyState::Accepted) {
      const auto diagnostic = ctx.activation->dispatch(local);
      if (!diagnostic.isEmpty()) fail(diagnostic);
    } else {
      fail(admitted == ActivationReplyState::Busy ? QStringLiteral("activation-busy") : QStringLiteral("activation-unavailable"));
    }
  };
  return dispatcher;
}

void installCapture(AppContext &ctx) {
  auto *open = ctx.commands.add(CommandSpec{QStringLiteral("open-xerahs"), QStringLiteral("Open XerahS"),
      CommandGroup::Capture, CommandSurface::Tray});
  QObject::connect(open, &QAction::triggered, &ctx.app, [&ctx] { if (auto *window = commandWindow(ctx)) window->activate(); });

  auto *region = addCapture(ctx, QStringLiteral("capture-region"), QStringLiteral("Capture region"),
      [&ctx] { ctx.invoke(ActivationAction::CaptureRegion, {}, false, {}, QStringLiteral("native-shell")); });
  addCapture(ctx, QStringLiteral("capture-full-screen"), QStringLiteral("Capture full screen"),
      [&ctx] { ctx.invoke(ActivationAction::CaptureFullScreen, {}, false, {}, QStringLiteral("native-shell")); });
  addCapture(ctx, QStringLiteral("capture-monitor"), QStringLiteral("Capture monitor under pointer"),
      [&ctx] { ctx.invoke(ActivationAction::CaptureMonitor, {}, false, {}, QStringLiteral("native-shell")); });
  addCapture(ctx, QStringLiteral("capture-active-window"), QStringLiteral("Capture active window"),
      [&ctx] { ctx.invoke(ActivationAction::CaptureActiveWindow, {}, false, {}, QStringLiteral("native-shell")); });
  addCapture(ctx, QStringLiteral("capture-last-region"), QStringLiteral("Capture last region"), [&ctx] {
    if (ctx.capture.busy() || ctx.recording.busy() || QApplication::activeModalWidget() || (ctx.shortcuts && ctx.shortcuts->pending())) return;
    auto choice = resolveShellWorkflow(ctx);
    if (!choice.workflow) { emit ctx.capture.notify("Capture workflow unavailable", choice.diagnostic, false); return; }
    choice.workflow->capture = QStringLiteral("last-region");
    static NativeWorkflowExecutions executions(NativeWorkflowExecutions::defaultDirectory());
    auto run = ctx.capture.prepareWorkflowCapture(*choice.workflow, QUuid::createUuid().toString(QUuid::WithoutBraces),
        QStringLiteral("native-shell"), executions.writer());
    if (run) run();
  });
  installScrollingCapture(ctx);
  addCapture(ctx, QStringLiteral("capture-region-choose-actions"), QStringLiteral("Capture region and choose actions…"),
      [&ctx] { ctx.invoke(ActivationAction::CaptureRegion, {}, true, {}, QStringLiteral("native-shell")); });
  auto *cancel = addCapture(ctx, QStringLiteral("cancel-capture"), QStringLiteral("Cancel capture"), {});
  cancel->setEnabled(false);
  QObject::connect(cancel, &QAction::triggered, &ctx.capture, &CaptureController::cancel);

  const auto settingsDirectory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const auto lastRegionPath = settingsDirectory.isEmpty() ? QString{} : QDir(settingsDirectory)
      .filePath(QStringLiteral("com.xerahs.app/linux-qt/last-region.json"));
  ctx.capture.setLastRegionReader([lastRegionPath] { return capture::LastRegionStore(lastRegionPath).read().region; });
  ctx.capture.setLastRegionWriter([lastRegionPath](const capture::LastRegion &region) {
    const capture::LastRegionStore store(lastRegionPath);
    const auto previous = store.read();
    return capture::lastRegionStorageDiagnostic(store.save(region, previous));
  });
  ctx.capture.setRegionSettingsResolver([&ctx] { return settings::regionSessionSettings(ctx.settings.current().values); });
  ctx.capture.setDefaultWorkflowResolver([&ctx] { return resolveShellWorkflow(ctx); });
  const auto refreshRegion = [&ctx, region] {
    const auto &catalog = ctx.workflows->store().current().catalog;
    QString label;
    if (catalog)
      for (const auto &workflow : catalog->entries)
        if (workflow.id == catalog->defaultId) label = workflow.name;
    label.replace(QLatin1Char('&'), QStringLiteral("&&"));
    region->setText(ctx.capture.busy() ? QStringLiteral("Capture region (in progress)")
        : label.isEmpty() ? QStringLiteral("Capture region (configure workflows)")
                          : QStringLiteral("Capture region — ") + label);
  };
  QObject::connect(ctx.workflows, &NativeCaptureWorkflows::changed, &ctx.app, refreshRegion);
  QObject::connect(&ctx.capture, &CaptureController::busyChanged, &ctx.app, refreshRegion);
  refreshRegion();

  const QStringList captures = {QStringLiteral("capture-region"), QStringLiteral("capture-full-screen"),
      QStringLiteral("capture-monitor"), QStringLiteral("capture-active-window"), QStringLiteral("capture-last-region"), QStringLiteral("capture-scrolling"), QStringLiteral("capture-region-choose-actions")};
  QObject::connect(&ctx.capture, &CaptureController::busyChanged, &ctx.app, [&ctx, captures, cancel](bool) {
    const bool busy = ctx.capture.busy();
    for (const auto &id : captures)
      if (auto *action = ctx.commands.action(id)) action->setEnabled(!busy);
    cancel->setEnabled(busy);
  });
  QObject::connect(&ctx.capture, &CaptureController::notify, &ctx.tray, [&ctx](const QString &title, const QString &body, bool success) {
    if (success && ctx.settings.current().values.value(QStringLiteral("playCaptureSound")).toBool()) QApplication::beep();
    // Failures are always reported; the setting only quiets successes.
    const bool desktop = !success || ctx.settings.current().values.value(QStringLiteral("showNotification")).toBool();
    postTrayMessage(ctx, title, body, success, 5000, desktop);
  });
  QObject::connect(&ctx.capture, &CaptureController::capturePermissionDenied, &ctx.app, [&ctx] {
    showCapturePermissionHelp(ctx.dialogParent ? ctx.dialogParent() : nullptr);
  });
  QObject::connect(&ctx.capture, &CaptureController::compositionProgressChanged, &ctx.app, [&ctx](int percent) {
    if (auto *window = commandWindow(ctx)) window->showProgress(QStringLiteral("Region capture"), percent);
  });
  QObject::connect(&ctx.capture, &CaptureController::pipelineActivityChanged, &ctx.app, [&ctx](const QString &text) {
    if (auto *window = commandWindow(ctx)) window->showActivity(QStringLiteral("Capture actions"), text);
  });
  QObject::connect(&ctx.capture, &CaptureController::busyChanged, &ctx.app, [&ctx](bool busy) {
    if (!busy)
      if (auto *window = commandWindow(ctx)) window->cancelPendingProgress();
  });
  QObject::connect(&ctx.capture, &CaptureController::choosingActions, &ctx.app, [&ctx] {
    if (auto *window = commandWindow(ctx)) window->activate();
  });
  QObject::connect(&ctx.tray, &QSystemTrayIcon::activated, &ctx.app, [&ctx](QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger)
      if (auto *action = ctx.commands.action(QStringLiteral("capture-region"))) action->trigger();
  });
}

void installRetainedCapture(AppContext &ctx, ApplicationWindow &window, QMenu &tray) {
  auto *recovery = new CaptureRecoveryActions(ctx.capture, &window, [](const QImage &image, QWidget *parent) {
    if (auto *editor = EditorWindow::fromCapturedImage(image, parent)) { editor->show(); return true; }
    return false;
  }, &window);
  recovery->setAnnotationOpener([](const xerahs::editor::AnnotationDocument &document, QWidget *parent) {
    if (auto *editor = EditorWindow::fromCapturedDocument(document, parent)) { editor->show(); return true; }
    return false;
  });
  if (auto *before = ctx.commands.action(QStringLiteral("new-image"))) {
    tray.insertAction(before, recovery->editAction());
    tray.insertAction(before, recovery->reviewAction());
    tray.insertAction(before, recovery->discardAction());
    tray.insertAction(before, recovery->retryAction());
  }
  window.addCaptureRecoveryActions(recovery->editAction(), recovery->discardAction(), recovery->reviewAction(), recovery->retryAction());
  QObject::connect(recovery, &CaptureRecoveryActions::notify, &window, [&window](const QString &body, bool success) {
    window.showResult(QStringLiteral("Retained capture"), body, success);
  });
}

}  // namespace xerahs::app
