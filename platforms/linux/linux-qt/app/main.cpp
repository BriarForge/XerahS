// XerahS for Linux (linux-qt): tray application with region capture.
// Identity com.xerahs.app / "XerahS" (CORE-002, ROOT-IDENTITY-001).

#include "CaptureController.h"
#include "EditorOpen.h"
#include "CaptureWorkflows.h"
#include "WorkflowDiscovery.h"
#include "NativeWorkflowExecutions.h"
#include "SecretServiceCredentialStore.h"
#include "settings/ApplicationSettings.h"
#include "NativeDesktopAppearance.h"
#include "NativeEditorPreferences.h"
#include "NativeInstance.h"
#include "ApplicationWindow.h"
#include "AppContext.h"
#include "CommandRegistry.h"
#include "features/ApplicationFeature.h"
#include "features/CaptureFeature.h"
#include "features/CommandWindowAttach.h"
#include "features/FilesFeature.h"
#include "features/OnboardingFeature.h"
#include "features/PreferencesFeature.h"
#include "features/RecordingFeature.h"
#include "features/ScheduleFeature.h"
#include "features/ToolsFeature.h"
#include "features/UploadFeature.h"
#include "features/WorkflowFeature.h"
#include "RecordingController.h"
#include "image-editor/EditorPreferences.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMenu>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QWidget>

#include <cstdio>
#include <algorithm>
#include <tuple>

using xerahs::app::CaptureController;

namespace {
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
  if (!diagnostic.isEmpty()) std::fprintf(stderr, "xerahs: %s: %s\n", qPrintable(diagnostic), qPrintable(xerahs::app::activationFailureMessage(
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
  const QCommandLineOption captureActiveWindow(QStringLiteral("capture-active-window"), QStringLiteral("Capture the active window with the saved default workflow's actions (in the running instance if any)."));
  const QCommandLineOption edit(QStringLiteral("edit"), QStringLiteral("Open an image in the editor."), QStringLiteral("file"));
  const QCommandLineOption runWorkflow(QStringLiteral("run-workflow"), QStringLiteral("Start a saved region workflow by full UUID from --list-workflows or Capture workflows."), QStringLiteral("id"));
  const QCommandLineOption listWorkflows(QStringLiteral("list-workflows"), QStringLiteral("List saved workflow IDs and definition availability as JSON without a GUI or capture."));
  const QCommandLineOption workflowResult(QStringLiteral("workflow-result"), QStringLiteral("Read a private execution summary by full launch request UUID without a GUI. Unfinished records have an unconfirmed outcome."), QStringLiteral("request-id"));
  const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Print versioned JSON; launch acknowledgements are not capture/output completion."));
  const QCommandLineOption nonInteractive(QStringLiteral("non-interactive"), QStringLiteral("Refuse operations requiring graphical interaction without starting them."));
  const QCommandLineOption startRegionRecording("start-region-recording", "Select and record a region (graphical setup and portal consent required).");
  const QCommandLineOption startRecording("start-recording", "Start screen recording (explicit audio inputs in Settings; graphical setup and portal consent required).");
  const QCommandLineOption stopRecording("stop-recording", "Stop recording and finalize output in the running instance.");
  parser.addOptions({startRegionRecording, startRecording, stopRecording, captureRegion, captureFullScreen, captureMonitor, captureActiveWindow, edit, runWorkflow, listWorkflows, workflowResult, jsonOption, nonInteractive});
  const auto requested = [&]() -> ParsedActivation {
    if (!parser.positionalArguments().isEmpty() || int(parser.isSet(startRegionRecording)) + int(parser.isSet(startRecording)) + int(parser.isSet(stopRecording)) + int(parser.isSet(edit)) + int(parser.isSet(captureRegion)) + int(parser.isSet(captureFullScreen)) + int(parser.isSet(captureMonitor)) + int(parser.isSet(captureActiveWindow)) + int(parser.isSet(runWorkflow)) + int(parser.isSet(listWorkflows)) + int(parser.isSet(workflowResult)) > 1) return {{}, "usage-invalid"};
    if (parser.isSet(startRegionRecording)) return {makeActivationRequest(ActivationAction::StartRegionRecording), {}};
    if (parser.isSet(startRecording)) return {makeActivationRequest(ActivationAction::StartRecording), {}};
    if (parser.isSet(stopRecording)) return {makeActivationRequest(ActivationAction::StopRecording), {}};
    if (parser.isSet(runWorkflow)) {
      const QUuid id(parser.value(runWorkflow)); if (id.isNull()) return {{}, "workflow-id-invalid"};
      return {makeActivationRequest(ActivationAction::RunWorkflow, {}, id.toString(QUuid::WithoutBraces)), {}};
    }
    if (parser.isSet(captureFullScreen)) return {makeActivationRequest(ActivationAction::CaptureFullScreen), {}};
    if (parser.isSet(captureMonitor)) return {makeActivationRequest(ActivationAction::CaptureMonitor), {}};
    if (parser.isSet(captureActiveWindow)) return {makeActivationRequest(ActivationAction::CaptureActiveWindow), {}};
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
  // Native profile defaults; saved workflow output overrides these preferences.
  const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  const QString defaultFolder = QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath(QStringLiteral("XerahS"));
  xerahs::settings::ApplicationSettingsStore settings(QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("application-settings.json"), defaultFolder);
  const auto uploadCatalogPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("upload-destinations.json");
  auto uploadCredentials = std::make_shared<xerahs::app::SecretServiceCredentialStore>();
  xerahs::app::CaptureActionPorts capturePorts; capturePorts.uploadCatalogPath = uploadCatalogPath;
  // The pipeline constructs its own keyring store on its worker thread.
  const auto journal = std::make_shared<xerahs::app::NativePipelineJournal>(xerahs::app::NativePipelineJournal::defaultDirectory());
  CaptureController capture(settings.current().values.value("saveFolder").toString(), nullptr, {}, {}, capturePorts, journal);
  const auto refreshCaptureStyle = [&capture] { capture.setAnnotationStyle(xerahs::editor::drawingStyleFromPreferences(xerahs::app::nativeEditorPreferences())); };
  refreshCaptureStyle();
  QObject::connect(&xerahs::app::nativeEditorPreferenceEvents(), &xerahs::app::NativeEditorPreferenceEvents::drawingDefaultsChanged, &capture, refreshCaptureStyle);

  QSystemTrayIcon tray(appIcon());
  tray.setToolTip(QStringLiteral("XerahS"));
  QMenu menu;
  xerahs::app::RecordingController recording;
  xerahs::app::CommandRegistry commands(&app);
  QPointer<xerahs::app::ApplicationWindow> home;
  xerahs::app::AppContext ctx{commands, app, tray, capture, recording, settings, uploadCatalogPath, uploadCredentials};
  ctx.workflows = &workflows;
  ctx.journal = journal;
  ctx.defaultCaptureFolder = defaultFolder;
  ctx.dialogParent = [&home]() -> QWidget * { return home.data(); };
  ctx.notify = [&ctx](const QString &title, const QString &body, bool ok, bool raiseWindow) {
    if (auto *window = qobject_cast<xerahs::app::ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr)) {
      window->showResult(title, body, ok);
      if (raiseWindow) window->activate();
    }
    ctx.tray.showMessage(title, body, ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, 5000);
  };
  auto dispatcher = xerahs::app::createShellDispatcher(ctx);
  xerahs::app::installCapture(ctx);
  xerahs::app::installSchedule(ctx);
  xerahs::app::installRecording(ctx);
  xerahs::app::installWorkflows(ctx);
  xerahs::app::installTools(ctx);
  xerahs::app::installFiles(ctx);
  xerahs::app::installUpload(ctx);
  xerahs::app::installPreferences(ctx);
  xerahs::app::installOnboarding(ctx, request.action == ActivationAction::Activate);
  xerahs::app::installApplication(ctx);
  xerahs::app::ApplicationWindow window(commands);
  home = &window;
  commands.populate(&menu, xerahs::app::CommandSurface::Tray);
  tray.setContextMenu(&menu);
  xerahs::app::attachCommandWindow(ctx, window, menu);
  if (!settings.current().diagnostics.isEmpty())
    window.showResult(QStringLiteral("Application settings"), settings.current().diagnostics.join('\n'), false);
  const auto dispatch = [&ctx](const ActivationRequest &incoming) {
    const auto diagnostic = ctx.activation->dispatch(incoming);
    if (diagnostic.isEmpty()) return;
    if (auto *shell = qobject_cast<xerahs::app::ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr)) {
      shell->showResult(QStringLiteral("Activation"), xerahs::app::activationFailureMessage(diagnostic), false);
      shell->activate();
    }
  };
  instance.setAdmission([&ctx](const ActivationRequest &incoming) {
    const QScopedValueRollback<QString> origin(ctx.invocationOrigin, QStringLiteral("external-activation"));
    return ctx.activation->admit(incoming);
  });
  QObject::connect(&instance, &xerahs::app::NativeInstance::activationRequested, &window, dispatch);
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    std::fprintf(stderr, "xerahs: no system tray host; use the XerahS window\n");
    window.show();
  }
  xerahs::app::applyShellVisibility(ctx);
  const auto admitted = ctx.activation->admit(request);
  if (admitted != ActivationReplyState::Accepted) {
    reportLaunch(request, json, {}, admitted == ActivationReplyState::Busy ? "activation-busy" : "activation-unavailable");
    instance.setAdmission({});
    return admitted == ActivationReplyState::Busy ? 2 : 1;
  }
  reportLaunch(request, json, "started");
  dispatch(request);
  const int result = app.exec();
  instance.setAdmission({});
  return result;
}
