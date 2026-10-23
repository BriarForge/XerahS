// XerahS for Linux (linux-qt): tray application with region capture.
// Identity com.xerahs.app / "XerahS" (CORE-002, ROOT-IDENTITY-001).

#include "CaptureController.h"
#include "CaptureActions.h"
#include "CaptureWorkflows.h"
#include "WorkflowDiscovery.h"
#include "NativeWorkflowExecutions.h"
#include <QScopedValueRollback>
#include "WorkflowShortcutActions.h"
#include "CaptureRecoveryActions.h"
#include "EditorWindow.h"
#include "EditorOpen.h"
#include "RecentImageMenu.h"
#include "NativeDesktopAppearance.h"
#include "AppearanceDialog.h"
#include "NativeInstance.h"
#include "ApplicationWindow.h"
#include "PipelineHistoryDialog.h"
#include "shell/ActivationDispatcher.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QJsonDocument>
#include <QUuid>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QUrl>

#include <cstdio>
#include <algorithm>
#include <tuple>

using xerahs::app::CaptureController;

namespace {
QString activationFailureMessage(const QString &diagnostic) {
  if (diagnostic == u"usage-invalid") return QStringLiteral("Choose one operation and use --help for supported arguments.");
  if (diagnostic == u"workflow-id-invalid") return QStringLiteral("Use a full workflow UUID from Capture workflows.");
  if (diagnostic == u"workflow-unavailable") return QStringLiteral("The saved workflow is missing or unavailable, or private execution storage could not be written. Review Capture workflows and private workflow storage before retrying.");
  if (diagnostic == u"presentation-required") return QStringLiteral("This operation needs the graphical XerahS application for capture, consent or editing. No operation was started.");
  if (diagnostic == u"activation-unavailable") return QStringLiteral("The operation is unavailable. Review the saved workflow and private execution storage ownership, permissions and capacity before retrying.");
  if (diagnostic == u"activation-busy") return QStringLiteral("XerahS is busy. Try again after the current capture or dialog finishes.");
  if (diagnostic == u"instance-unavailable") return QStringLiteral("The running XerahS instance is not ready. Try again after startup finishes.");
  if (diagnostic == u"activation-delivery-unknown") return QStringLiteral("XerahS did not acknowledge the request. Check the running instance before retrying.");
  if (diagnostic == u"instance-storage-denied") return QStringLiteral("XerahS could not secure its runtime files. Check the ownership and permissions of XDG_RUNTIME_DIR.");
  if (diagnostic == u"instance-endpoint-conflict") return QStringLiteral("The XerahS runtime endpoint is in use or unsafe. Close conflicting instances and check runtime permissions.");
  if (diagnostic == u"activation-peer-denied") return QStringLiteral("XerahS could not authenticate the running instance. Check your per-user runtime environment.");
  if (diagnostic == u"activation-invalid" || diagnostic == u"activation-version-unsupported") return QStringLiteral("The activation request is invalid or incompatible. Check the command and installed XerahS version.");
  return QStringLiteral("XerahS could not accept the activation. Check the running instance and try again.");
}

void writeCommandJson(const QJsonObject &result) {
  const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
  std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
int reportDiscovery(const QJsonObject &result, bool quiet) {
  writeCommandJson(result);
  if (result.value("ok").toBool()) return 0;
  if (!quiet) std::fprintf(stderr, "xerahs: %s: Saved workflows could not be listed. Check the command, or open Capture workflows to review and reload the saved configuration.\n",
      qPrintable(result.value("error").toObject().value("code").toString()));
  return 1;
}

void reportLaunch(const xerahs::shell::ActivationRequest &request, bool json, const QString &delivery = {}, const QString &diagnostic = {}) {
  if (json) {
    writeCommandJson(xerahs::shell::activationLaunchResult(request, delivery, diagnostic));
  }
  if (!diagnostic.isEmpty()) std::fprintf(stderr, "xerahs: %s: %s\n", qPrintable(diagnostic), qPrintable(activationFailureMessage(
      request.action == xerahs::shell::ActivationAction::RunWorkflow && diagnostic == "activation-unavailable" ? QStringLiteral("workflow-unavailable") : diagnostic)));
}

// Monochrome capture-frame mark, drawn so no asset pipeline is needed yet.
QIcon appIcon() {
  QIcon icon;
  for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const double s = size;
    QPainterPath body;
    body.addRoundedRect(QRectF(s * 0.06, s * 0.06, s * 0.88, s * 0.88), s * 0.2, s * 0.2);
    p.fillPath(body, QColor(24, 24, 27));
    QPen pen(QColor(245, 245, 245), std::max(1.5, s * 0.085), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.setPen(pen);
    const double a = s * 0.27;
    const double b = s * 0.73;
    const double l = s * 0.15;
    for (const auto &[x, y, sx, sy] : {std::tuple{a, a, 1.0, 1.0}, std::tuple{b, a, -1.0, 1.0},
                                       std::tuple{a, b, 1.0, -1.0}, std::tuple{b, b, -1.0, -1.0}}) {
      p.drawLine(QPointF(x, y), QPointF(x + sx * l, y));
      p.drawLine(QPointF(x, y), QPointF(x, y + sy * l));
    }
    p.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

}  // namespace

int main(int argc, char *argv[]) {
  using namespace xerahs::shell;
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("XerahS workflow discovery and screen capture. Capture requests require the graphical application; acknowledgement is not output completion."));
  parser.addHelpOption();
  const QCommandLineOption captureRegion(QStringLiteral("capture-region"), QStringLiteral("Start the saved default region workflow (in the running instance if any)."));
  const QCommandLineOption captureFullScreen(QStringLiteral("capture-full-screen"), QStringLiteral("Capture the full screen with the saved default workflow's actions (in the running instance if any)."));
  const QCommandLineOption captureMonitor(QStringLiteral("capture-monitor"), QStringLiteral("Capture the monitor under the pointer with the saved default workflow's actions (in the running instance if any)."));
  const QCommandLineOption edit(QStringLiteral("edit"), QStringLiteral("Open an image in the editor."), QStringLiteral("file"));
  const QCommandLineOption runWorkflow(QStringLiteral("run-workflow"), QStringLiteral("Start a saved region workflow by full UUID from --list-workflows or Capture workflows."), QStringLiteral("id"));
  const QCommandLineOption listWorkflows(QStringLiteral("list-workflows"), QStringLiteral("List saved workflow IDs and definition availability as JSON without a GUI or capture."));
  const QCommandLineOption workflowResult(QStringLiteral("workflow-result"), QStringLiteral("Read a private execution summary by full launch request UUID without a GUI. Unfinished records have an unconfirmed outcome."), QStringLiteral("request-id"));
  const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Print versioned JSON; launch acknowledgements are not capture/output completion."));
  const QCommandLineOption nonInteractive(QStringLiteral("non-interactive"), QStringLiteral("Refuse operations requiring graphical interaction without starting them."));
  parser.addOptions({captureRegion, captureFullScreen, captureMonitor, edit, runWorkflow, listWorkflows, workflowResult, jsonOption, nonInteractive});
  const auto requested = [&]() -> ParsedActivation {
    if (!parser.positionalArguments().isEmpty() || int(parser.isSet(edit)) + int(parser.isSet(captureRegion)) + int(parser.isSet(captureFullScreen)) + int(parser.isSet(captureMonitor)) + int(parser.isSet(runWorkflow)) + int(parser.isSet(listWorkflows)) + int(parser.isSet(workflowResult)) > 1) return {{}, "usage-invalid"};
    if (parser.isSet(runWorkflow)) {
      const QUuid id(parser.value(runWorkflow)); if (id.isNull()) return {{}, "workflow-id-invalid"};
      return {makeActivationRequest(ActivationAction::RunWorkflow, {}, id.toString(QUuid::WithoutBraces)), {}};
    }
    if (parser.isSet(captureFullScreen)) return {makeActivationRequest(ActivationAction::CaptureFullScreen), {}};
    if (parser.isSet(captureMonitor)) return {makeActivationRequest(ActivationAction::CaptureMonitor), {}};
    return {makeActivationRequest(parser.isSet(edit) ? ActivationAction::EditImage : parser.isSet(captureRegion) ? ActivationAction::CaptureRegion : ActivationAction::Activate,
        parser.isSet(edit) ? xerahs::app::absoluteImagePath(parser.value(edit)) : QString{}), {}};
  };
  // A headless refusal must not instantiate QApplication, connect to a display,
  // elect an instance, initialize preferences or ask a portal for consent.
  QStringList rawArguments; for (int i = 0; i < argc; ++i) rawArguments.append(QString::fromLocal8Bit(argv[i]));
  const bool discoveryCommand = std::any_of(rawArguments.cbegin(), rawArguments.cend(), [](const QString &argument) { return argument == "--list-workflows" || argument.startsWith("--list-workflows="); });
  const bool resultCommand = std::any_of(rawArguments.cbegin(), rawArguments.cend(), [](const QString &argument) { return argument == "--workflow-result" || argument.startsWith("--workflow-result="); });
  const auto reportUsage = [&](const QString &diagnostic) {
    if (resultCommand) { writeCommandJson(xerahs::workflows::workflowExecutionError(diagnostic)); return 1; }
    if (discoveryCommand) return reportDiscovery(xerahs::workflows::workflowDiscoveryError(diagnostic), rawArguments.contains("--json"));
    reportLaunch(makeActivationRequest(ActivationAction::Activate), rawArguments.contains("--json"), {}, diagnostic); return 1;
  };
  const bool earlyCommand = resultCommand || discoveryCommand || rawArguments.contains("--help") || rawArguments.contains("--help-all") || rawArguments.contains("-h") || rawArguments.contains("--json") || rawArguments.contains("--non-interactive") ||
      std::any_of(rawArguments.cbegin(), rawArguments.cend(), [](const QString &argument) { return argument == "--run-workflow" || argument.startsWith("--run-workflow="); });
  ParsedActivation launch;
  if (earlyCommand) {
    if (!parser.parse(rawArguments)) return reportUsage("usage-invalid");
    if (parser.isSet("help") || parser.isSet("help-all")) {
      QCoreApplication cli(argc, argv); cli.setApplicationName("XerahS"); std::fputs(qPrintable(parser.helpText()), stdout); return 0;
    }
    launch = requested();
    if (!launch.request) return reportUsage(launch.diagnostic);
    if (parser.isSet(workflowResult)) {
      QCoreApplication cli(argc, argv); cli.setApplicationName("XerahS");
      const auto result = xerahs::app::NativeWorkflowExecutions(xerahs::app::NativeWorkflowExecutions::defaultDirectory()).result(parser.value(workflowResult));
      writeCommandJson(result);
      if (!result.value("ok").toBool() && !parser.isSet(jsonOption)) std::fprintf(stderr, "xerahs: Execution could not be read. Use the full launch request UUID and check private workflow storage.\n");
      return result.value("ok").toBool() ? 0 : 1;
    }
    if (parser.isSet(listWorkflows)) {
      QCoreApplication cli(argc, argv); cli.setApplicationName("XerahS");
      return reportDiscovery(xerahs::app::discoverNativeWorkflows(), parser.isSet(jsonOption));
    }
    if (parser.isSet(nonInteractive)) { reportLaunch(*launch.request, parser.isSet(jsonOption), {}, "presentation-required"); return 1; }
  }
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("XerahS"));
  QApplication::setApplicationDisplayName(QStringLiteral("XerahS"));
  QApplication::setDesktopFileName(QStringLiteral("com.xerahs.app"));
  QApplication::setOrganizationDomain(QStringLiteral("xerahs.com"));
  QApplication::setQuitOnLastWindowClosed(false);
  QApplication::setWindowIcon(appIcon());

  if (!earlyCommand && !parser.parse(app.arguments())) { reportLaunch(makeActivationRequest(ActivationAction::Activate), app.arguments().contains("--json"), {}, "usage-invalid"); return 1; }
  if (parser.isSet("help") || parser.isSet("help-all")) parser.showHelp();
  const bool json = parser.isSet(jsonOption); if (!earlyCommand) launch = requested();
  if (!launch.request) { reportLaunch(makeActivationRequest(ActivationAction::Activate), json, {}, launch.diagnostic); return 1; }
  const auto request = *launch.request;
  xerahs::app::NativeInstance instance(xerahs::app::NativeInstance::runtimeDirectory());
  const auto started = instance.start(request);
  if (started.kind == xerahs::app::InstanceStart::Kind::Forwarded) { reportLaunch(request, json, "running-instance"); return 0; }
  if (started.kind != xerahs::app::InstanceStart::Kind::Primary) {
    reportLaunch(request, json, {}, started.diagnostic);
    return started.kind == xerahs::app::InstanceStart::Kind::Busy ? 2 : 1;
  }

  xerahs::app::NativeCaptureWorkflows workflows(xerahs::app::NativeCaptureWorkflows::defaultPath());
  if (request.action == ActivationAction::RunWorkflow && !workflows.resolve(QUuid(request.workflowId)).workflow) {
    reportLaunch(request, json, {}, "activation-unavailable"); return 1;
  }

  // Initialize only in the winning native instance, before any editor opens.
  // The Avalonia configuration is never imported implicitly.
  const auto preferenceDiagnostics = xerahs::app::initializeNativeEditorPreferences(xerahs::app::nativeEditorPreferencePath());
  xerahs::app::nativeDesktopAppearance().start();
  if (!preferenceDiagnostics.isEmpty()) {
    QStringList messages;
    for (const auto &code : preferenceDiagnostics) messages.append(xerahs::app::editorPreferenceDiagnosticMessage(code));
    QMessageBox::warning(nullptr, QStringLiteral("Editor preferences"), messages.join(QLatin1Char('\n')));
  }
  // Capture destination preferences remain Required; the current destination
  // is the XDG pictures directory confirmed by the product owner.
  const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  const auto journal = std::make_shared<xerahs::app::NativePipelineJournal>(xerahs::app::NativePipelineJournal::defaultDirectory());
  CaptureController capture(QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath(QStringLiteral("XerahS")), nullptr, {}, {}, {}, journal);
  capture.setDefaultWorkflowResolver([&workflows] { return workflows.resolve(); });
  const auto refreshCaptureStyle = [&capture] { capture.setAnnotationStyle(xerahs::editor::drawingStyleFromPreferences(xerahs::app::nativeEditorPreferences())); };
  refreshCaptureStyle();
  QObject::connect(&xerahs::app::nativeEditorPreferenceEvents(), &xerahs::app::NativeEditorPreferenceEvents::drawingDefaultsChanged, &capture, refreshCaptureStyle);

  xerahs::app::WorkflowShortcutActions shortcutActions([&workflows] {
    const auto diagnostic = workflows.store().unchanged();
    if (!diagnostic.isEmpty()) return xerahs::workflows::WorkflowShortcutPlan{{}, 0, diagnostic};
    return xerahs::workflows::workflowShortcutPlan(*workflows.store().current().catalog, xerahs::app::CaptureActions::selectableActions());
  }, [&capture] { return !capture.busy() && !QApplication::activeModalWidget(); });
  QSystemTrayIcon tray(appIcon());
  tray.setToolTip(QStringLiteral("XerahS"));
  QMenu menu;
  QAction *region = menu.addAction(QStringLiteral("Capture region"));
  QAction *fullScreen = menu.addAction(QStringLiteral("Capture full screen"));
  QAction *monitor = menu.addAction(QStringLiteral("Capture monitor under pointer"));
  QAction *chooseCapture = menu.addAction(QStringLiteral("Capture region and choose actions…"));
  QAction *configureWorkflows = menu.addAction(QStringLiteral("Capture workflows…"));
  menu.addAction(shortcutActions.enableAction()); menu.addAction(shortcutActions.disableAction());
  QAction *cancelCapture = menu.addAction(QStringLiteral("Cancel capture"));
  cancelCapture->setEnabled(false);
  QObject::connect(cancelCapture, &QAction::triggered, &capture, &CaptureController::cancel);
  QAction *newImage = menu.addAction(QStringLiteral("New image…"));
  QAction *openImage = menu.addAction(QStringLiteral("Open image…"));
  xerahs::app::addRecentImagesMenu(&menu, [](const QString &path) {
    if (auto *window = xerahs::app::EditorWindow::open(path)) window->show();
  }, [] { xerahs::app::manageRecentImages(); });
  QAction *folder = menu.addAction(QStringLiteral("Open captures folder"));
  QAction *history = menu.addAction(QStringLiteral("Capture result history…"));
  QAction *appearance = menu.addAction(QStringLiteral("Editor appearance…"));
  menu.addSeparator();
  QAction *quit = menu.addAction(QStringLiteral("Quit XerahS"));
  tray.setContextMenu(&menu);
  xerahs::app::ApplicationWindow home({{"capture-region", region}, {"capture-full-screen", fullScreen}, {"capture-monitor", monitor}, {"capture-region-choose-actions", chooseCapture}, {"cancel-capture", cancelCapture},
      {"capture-workflows", configureWorkflows}, {"enable-workflow-shortcuts", shortcutActions.enableAction()}, {"disable-workflow-shortcuts", shortcutActions.disableAction()},
      {"new-image", newImage}, {"open-image", openImage}, {"captures-folder", folder}, {"capture-history", history}, {"editor-appearance", appearance}, {"quit", quit}});
  // Leave a focus-capable native window behind the chooser. In particular,
  // Wayland clipboard dispatch must not follow closure of our last window.
  capture.setActionSelectionParent(&home);
  QObject::connect(&shortcutActions, &xerahs::app::WorkflowShortcutActions::notify, &home, [&home](const QString &body, bool success) { home.showResult("Workflow shortcuts", body, success); });
  QObject::connect(&workflows, &xerahs::app::NativeCaptureWorkflows::changed, &shortcutActions, &xerahs::app::WorkflowShortcutActions::invalidate);
  const auto refreshWorkflow = [&] {
    const auto &catalog = workflows.store().current().catalog; QString label;
    if (catalog) for (const auto &w : catalog->entries) if (w.id == catalog->defaultId) label = w.name;
    label.replace('&', "&&");
    region->setText(capture.busy() ? "Capture region (in progress)" : label.isEmpty() ? "Capture region (configure workflows)" : "Capture region — " + label);
  };
  QObject::connect(&workflows, &xerahs::app::NativeCaptureWorkflows::changed, &home, refreshWorkflow);
  QObject::connect(&capture, &CaptureController::busyChanged, &home, refreshWorkflow); refreshWorkflow();
  QObject::connect(&capture, &CaptureController::choosingActions, &home, &xerahs::app::ApplicationWindow::activate);
  QPointer<xerahs::app::PipelineHistoryDialog> historyDialog;
  QObject::connect(history, &QAction::triggered, &home, [&] {
    if (historyDialog) { historyDialog->show(); historyDialog->raise(); historyDialog->activateWindow(); return; }
    historyDialog = new xerahs::app::PipelineHistoryDialog(journal, [&capture] { return capture.busy(); }, &home);
    historyDialog->show();
  });
  auto *showHome = new QAction(QStringLiteral("Open XerahS"), &menu); menu.insertAction(region, showHome);
  QObject::connect(showHome, &QAction::triggered, &home, &xerahs::app::ApplicationWindow::activate);
  auto *recovery = new xerahs::app::CaptureRecoveryActions(capture, &home, [](const QImage &image, QWidget *parent) {
    if (auto *window = xerahs::app::EditorWindow::fromCapturedImage(image, parent)) { window->show(); return true; }
    return false;
  }, &home);
  menu.insertAction(newImage, recovery->editAction()); menu.insertAction(newImage, recovery->reviewAction()); menu.insertAction(newImage, recovery->discardAction());
  menu.insertAction(newImage, recovery->retryAction());
  recovery->setAnnotationOpener([](const xerahs::editor::AnnotationDocument &document, QWidget *parent) {
    if (auto *window = xerahs::app::EditorWindow::fromCapturedDocument(document, parent)) { window->show(); return true; }
    return false;
  });
  home.addCaptureRecoveryActions(recovery->editAction(), recovery->discardAction(), recovery->reviewAction(), recovery->retryAction());
  QObject::connect(recovery, &xerahs::app::CaptureRecoveryActions::notify, &home, [&home](const QString &body, bool success) {
    home.showResult(QStringLiteral("Retained capture"), body, success);
  });

  QObject::connect(newImage, &QAction::triggered, [] {
    if (auto *window = xerahs::app::EditorWindow::createNew()) window->show();
  });
  QObject::connect(folder, &QAction::triggered, &home, [&] {
    const auto choice = workflows.resolve();
    if (!choice.workflow) { home.showResult("Capture folder unavailable", xerahs::app::workflowMessage(choice.diagnostic), false); return; }
    const auto directory = choice.workflow->output.directory.isEmpty() ? capture.saveDirectory() : choice.workflow->output.directory;
    if (!QDir().mkpath(directory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
      home.showResult("Capture folder unavailable", "The saved default workflow's folder could not be opened. Review its save folder in Capture workflows.", false);
  });
  QObject::connect(quit, &QAction::triggered, &app, &QApplication::quit);
  QObject::connect(appearance, &QAction::triggered, [] { xerahs::app::manageEditorAppearance(); });
  // CORE-007: availability is shown, not faked.
  QObject::connect(&capture, &CaptureController::busyChanged, region, [region, fullScreen, monitor, chooseCapture, cancelCapture, &capture](bool) {
    const bool busy = capture.busy();
    region->setEnabled(!busy);
    fullScreen->setEnabled(!busy);
    monitor->setEnabled(!busy);
    chooseCapture->setEnabled(!busy);
    cancelCapture->setEnabled(busy);
  });
  QObject::connect(&capture, &CaptureController::notify, &tray,
                   [&tray](const QString &title, const QString &body, bool success) {
                     tray.showMessage(title, body, success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning,
                                      5000);
                   });
  QObject::connect(&capture, &CaptureController::notify, &home, &xerahs::app::ApplicationWindow::showResult);
  QObject::connect(&capture, &CaptureController::compositionProgressChanged, &home, [&home](int percent) {
    home.showProgress(QStringLiteral("Region capture"), percent);
  });
  QObject::connect(&capture, &CaptureController::pipelineActivityChanged, &home, [&home](const QString &text) {
    home.showActivity(QStringLiteral("Capture actions"), text);
  });
  QObject::connect(&capture, &CaptureController::busyChanged, &home, [&home](bool busy) {
    if (!busy) home.cancelPendingProgress();
  });
  const xerahs::app::NativeWorkflowExecutions executions(xerahs::app::NativeWorkflowExecutions::defaultDirectory());
  QString invocationOrigin = QStringLiteral("command-line");
  ActivationDispatcher dispatcher({[&home] { home.activate(); }, [&capture] { capture.captureRegion(); },
      [](const QString &path) { if (auto *window = xerahs::app::EditorWindow::open(path)) window->show(); },
      [&capture] { return capture.busy(); }, [&shortcutActions] { return QApplication::activeModalWidget() != nullptr || shortcutActions.pending(); },
      {}, {}, [&workflows, &capture, &home, &executions, &invocationOrigin](const ActivationRequest &incoming) -> std::function<void()> {
        auto choice = incoming.action == ActivationAction::RunWorkflow ? workflows.resolve(QUuid(incoming.workflowId)) : workflows.resolve();
        if (!choice.workflow) return {};
        choice.workflow->chooseActions |= incoming.action == ActivationAction::CaptureRegionChooseActions;
        // CORE-014: source commands reuse the default workflow's actions and output.
        if (incoming.action == ActivationAction::CaptureFullScreen) choice.workflow->capture = QStringLiteral("full-screen");
        else if (incoming.action == ActivationAction::CaptureMonitor) choice.workflow->capture = QStringLiteral("monitor");
        else if (incoming.action != ActivationAction::RunWorkflow) choice.workflow->capture = QStringLiteral("region");
        auto run = capture.prepareWorkflowCapture(*choice.workflow, incoming.id, invocationOrigin, executions.writer());
        if (!run) return {};
        return [&home, run = std::move(run)] { home.activate(); run(); };
      }});
  instance.setAdmission([&dispatcher, &invocationOrigin](const ActivationRequest &incoming) {
    const QScopedValueRollback<QString> origin(invocationOrigin, QStringLiteral("external-activation")); return dispatcher.admit(incoming);
  });
  const auto dispatch = [&dispatcher, &home](const ActivationRequest &incoming) {
    const auto diagnostic = dispatcher.dispatch(incoming);
    if (!diagnostic.isEmpty()) { home.showResult(QStringLiteral("Activation"), activationFailureMessage(diagnostic), false); home.activate(); }
  };
  QObject::connect(&instance, &xerahs::app::NativeInstance::activationRequested, &home, dispatch);
  const auto invokeLocal = [&dispatcher, &dispatch, &home, &invocationOrigin](ActivationAction action, const QString &path = QString{}, bool chooseActions = false, const QString &workflowId = QString{}, const QString &source = QStringLiteral("native-shell")) {
    const QScopedValueRollback<QString> origin(invocationOrigin, source);
    const auto local = makeActivationRequest(chooseActions ? ActivationAction::CaptureRegionChooseActions : action, path, workflowId);
    const auto admitted = dispatcher.admit(local);
    if (admitted == ActivationReplyState::Accepted) {
      dispatch(local);
    }
    else {
      home.showResult(QStringLiteral("Activation"), activationFailureMessage(admitted == ActivationReplyState::Busy ? QStringLiteral("activation-busy") : QStringLiteral("activation-unavailable")), false);
      home.activate();
    }
  };
  QObject::connect(&shortcutActions, &xerahs::app::WorkflowShortcutActions::workflowRequested, &home, [&invokeLocal](const QString &id) { invokeLocal(ActivationAction::RunWorkflow, {}, false, id, "global-shortcut"); });
  QObject::connect(configureWorkflows, &QAction::triggered, &home, [&] {
    const auto selected = xerahs::app::manageCaptureWorkflows(workflows, &home, [&capture] { return !capture.busy(); });
    if (selected) invokeLocal(ActivationAction::RunWorkflow, {}, false, selected->id.toString(QUuid::WithoutBraces));
  });
  QObject::connect(region, &QAction::triggered, &home, [&invokeLocal] { invokeLocal(ActivationAction::CaptureRegion); });
  QObject::connect(chooseCapture, &QAction::triggered, &home, [&invokeLocal] { invokeLocal(ActivationAction::CaptureRegion, {}, true); });
  QObject::connect(fullScreen, &QAction::triggered, &home, [&invokeLocal] { invokeLocal(ActivationAction::CaptureFullScreen); });
  QObject::connect(monitor, &QAction::triggered, &home, [&invokeLocal] { invokeLocal(ActivationAction::CaptureMonitor); });
  QObject::connect(openImage, &QAction::triggered, &home, [&invokeLocal, &capture, &home] {
    const auto path = QFileDialog::getOpenFileName(&home, QStringLiteral("Open image"), capture.saveDirectory(), xerahs::app::imageFileFilter());
    if (!path.isEmpty()) invokeLocal(ActivationAction::EditImage, path);
  });
  QObject::connect(&tray, &QSystemTrayIcon::activated, region, [region](QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) region->trigger();
  });

  const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
  if (!trayAvailable) {
    std::fprintf(stderr, "xerahs: no system tray host; use the XerahS window\n");
    home.show();
  }
  app.setQuitOnLastWindowClosed(!trayAvailable);
  tray.show();
  const auto admitted = dispatcher.admit(request);
  if (admitted != ActivationReplyState::Accepted) {
    reportLaunch(request, json, {}, admitted == ActivationReplyState::Busy ? "activation-busy" : "activation-unavailable");
    instance.setAdmission({}); return admitted == ActivationReplyState::Busy ? 2 : 1;
  }
  reportLaunch(request, json, "started"); dispatch(request);
  const int result = app.exec(); instance.setAdmission({}); return result;
}
