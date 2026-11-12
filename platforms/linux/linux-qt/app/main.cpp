// XerahS for Linux (linux-qt): tray application with region capture.
// Identity com.xerahs.app / "XerahS" (CORE-002, ROOT-IDENTITY-001).

#include "CaptureController.h"
#include "EditorOpen.h"
#include "EditorTemporaryFiles.h"
#include "CaptureWorkflows.h"
#include "WorkflowDiscovery.h"
#include "NativeWorkflowExecutions.h"
#include "SecretServiceCredentialStore.h"
#include "settings/ApplicationSettings.h"
#include "NativeDesktopAppearance.h"
#include "NativeEditorPreferences.h"
#include "NativeInstance.h"
#include "ApplicationWindow.h"
#include "ApplicationIcon.h"
#include "AppContext.h"
#include "CommandRegistry.h"
#include "features/CommandWindowAttach.h"
#include "features/ApplicationFeature.h"
#include "features/CaptureFeature.h"
#include "features/PreferencesFeature.h"
#include "features/ShellFeatures.h"
#include "shell/ActivationDispatcher.h"
#include "RecordingController.h"
#include "image-editor/EditorPreferences.h"
#include "shell/ErrorPresentation.h"
#include "shell/TerminationSignals.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMenu>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPointer>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include "EditorWindow.h"
#include <QWidget>

#include <cstdio>
#include <algorithm>

using xerahs::app::CaptureController;

namespace {
void writeCommandJson(const QJsonObject &result) {
  const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
  std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
// Every failure prints one "xerahs: <code>: <message>" line (suppressed by --json
// for lookup commands) using the shared presentation table (IFCAT-016).
int reportFailure(const QString &code, bool quiet) {
  if (!quiet) std::fprintf(stderr, "xerahs: %s: %s\n", qPrintable(code), qPrintable(xerahs::shell::errorMessage(code)));
  return xerahs::shell::errorExitCode(code);
}
int reportDiscovery(const QJsonObject &result, bool quiet) {
  writeCommandJson(result);
  if (result.value("ok").toBool()) return 0;
  return reportFailure(result.value("error").toObject().value("code").toString(), quiet);
}

void reportLaunch(const xerahs::shell::ActivationRequest &request, bool json, const QString &delivery = {}, const QString &diagnostic = {}) {
  if (json) {
    writeCommandJson(xerahs::shell::activationLaunchResult(request, delivery, diagnostic));
  }
  if (!diagnostic.isEmpty()) std::fprintf(stderr, "xerahs: %s: %s\n", qPrintable(diagnostic), qPrintable(xerahs::app::activationFailureMessage(
      request.action == xerahs::shell::ActivationAction::RunWorkflow && diagnostic == "activation-unavailable" ? QStringLiteral("workflow-unavailable") : diagnostic)));
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
    if (resultCommand) { const auto error = xerahs::workflows::workflowExecutionError(diagnostic); writeCommandJson(error); return reportFailure(error.value("error").toObject().value("code").toString(), rawArguments.contains("--json")); }
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
      return result.value("ok").toBool() ? 0 : reportFailure(result.value("error").toObject().value("code").toString(), parser.isSet(jsonOption));
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
  QApplication::setWindowIcon(xerahs::app::applicationIcon());

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
    return xerahs::shell::errorExitCode(started.kind == xerahs::app::InstanceStart::Kind::Busy ? QStringLiteral("activation-busy") : started.diagnostic);
  }

  const auto temporaryDirectory = xerahs::app::editorTemporaryDirectory();
  if (!temporaryDirectory.isEmpty()) xerahs::app::sweepEditorTemporaryFiles(temporaryDirectory);

  // IFCAT-008: SIGINT/SIGTERM cancel cleanly. The event loop quits (closing
  // windows and cancelling owned work through the normal teardown) and the
  // process exits 128+signal (130/143). A repeated signal during shutdown is
  // ignored; SIGKILL remains the only forced stop. See app/CLI-CONTRACT.md.
  int terminationSignal = 0;
  xerahs::shell::TerminationSignals termination;
  QObject::connect(&termination, &xerahs::shell::TerminationSignals::requested, &app, [&terminationSignal](int received) {
    if (terminationSignal) return;
    terminationSignal = received;
    QCoreApplication::exit(xerahs::shell::TerminationSignals::exitStatus(received));
  });

  const auto recovery = xerahs::app::recoverNativeSettings();
  if (!recovery.isEmpty()) {
    QMessageBox::warning(nullptr, "Settings recovery", recovery + "\nSettings were kept for recovery. Close other instances and retry."); return 1;
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
  capturePorts.editorPreferences = [] { return xerahs::app::nativeEditorPreferences().values; };
  // The pipeline constructs its own keyring store on its worker thread.
  const auto journal = std::make_shared<xerahs::app::NativePipelineJournal>(xerahs::app::NativePipelineJournal::defaultDirectory());
  CaptureController capture(settings.current().values.value("saveFolder").toString(), nullptr, {}, {}, capturePorts, journal);
  const auto refreshCaptureStyle = [&capture] { capture.setAnnotationStyle(xerahs::editor::drawingStyleFromPreferences(xerahs::app::nativeEditorPreferences())); };
  refreshCaptureStyle();
  QObject::connect(&xerahs::app::nativeEditorPreferenceEvents(), &xerahs::app::NativeEditorPreferenceEvents::drawingDefaultsChanged, &capture, refreshCaptureStyle);

  QSystemTrayIcon tray(xerahs::app::applicationIcon());
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
      if (raiseWindow) window->activate();
    }
    xerahs::app::postTrayMessage(ctx, title, body, ok, 5000);
  };
  auto dispatcher = xerahs::app::installShellFeatures(ctx, request.action == ActivationAction::Activate);
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
    return xerahs::shell::errorExitCode(admitted == ActivationReplyState::Busy ? QStringLiteral("activation-busy") : QStringLiteral("activation-unavailable"));
  }
  reportLaunch(request, json, "started");
  dispatch(request);
  // ES-018: disclose and offer any editor recovery documents left by a crash.
  QTimer::singleShot(0, &window, [] { xerahs::app::EditorWindow::offerRecovery(nullptr); });
  const int result = terminationSignal ? xerahs::shell::TerminationSignals::exitStatus(terminationSignal) : app.exec();
  instance.setAdmission({});
  return terminationSignal ? xerahs::shell::TerminationSignals::exitStatus(terminationSignal) : result;
}
