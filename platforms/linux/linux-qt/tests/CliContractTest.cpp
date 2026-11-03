// Actual-process tests of the CLI contract in app/CLI-CONTRACT.md (IFCAT-008).
#include "NativeInstance.h"
#include "shell/ErrorPresentation.h"
#include "workflows/CaptureWorkflows.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <csignal>
#include <memory>
#include <QUuid>

using namespace xerahs::app;
using namespace xerahs::shell;

namespace {
struct Run { int exitCode = -1; QProcess::ExitStatus status = QProcess::CrashExit; QByteArray out, err; };
QJsonObject json(const QByteArray &bytes) { return QJsonDocument::fromJson(bytes.trimmed()).object(); }
}

class CliContractTest final : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> temporary;

  QProcessEnvironment environment(bool headless) const {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("XDG_RUNTIME_DIR", temporary->path());
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) env.insert(key, temporary->filePath(key));
    env.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + temporary->filePath("no-host-bus"));
    env.insert("QT_QPA_PLATFORM", headless ? "xcb" : "offscreen");
    env.insert("QT_QPA_PLATFORMTHEME", "");
    if (headless) { env.remove("DISPLAY"); env.remove("WAYLAND_DISPLAY"); }
    return env;
  }
  Run run(const QStringList &arguments, const QProcessEnvironment &env) const {
    QProcess process; process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationDirPath() + "/xerahs", arguments);
    Run result; if (!process.waitForFinished(8000)) { process.kill(); process.waitForFinished(2000); return result; }
    result.exitCode = process.exitCode(); result.status = process.exitStatus();
    result.out = process.readAllStandardOutput(); result.err = process.readAllStandardError(); return result;
  }
  bool writeCorruptWorkflowProfile() const {
    const auto parent = temporary->filePath("XDG_CONFIG_HOME/com.xerahs.app/linux-qt");
    if (!QDir().mkpath(parent)) return false;
    QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile file(parent + "/capture-workflows.json");
    if (!file.open(QIODevice::WriteOnly) || file.write("private-token malformed") < 0) return false;
    file.close(); return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  }
private slots:
  void init() { temporary = std::make_unique<QTemporaryDir>(); QVERIFY(temporary->isValid()); }
  void cleanup() { temporary.reset(); }

  // IFCAT-008 signal handling: SIGINT/SIGTERM cancel the running primary
  // cleanly with the documented 128+signal status, and release the instance.
  void primaryExitsWithDocumentedStatusOnSigintAndSigterm_data() {
    QTest::addColumn<int>("signal"); QTest::addColumn<int>("exitCode");
    QTest::newRow("SIGINT") << int(SIGINT) << 130; QTest::newRow("SIGTERM") << int(SIGTERM) << 143;
  }
  void primaryExitsWithDocumentedStatusOnSigintAndSigterm() {
    QFETCH(int, signal); QFETCH(int, exitCode);
    QProcess app; app.setProcessEnvironment(environment(false));
    app.start(QCoreApplication::applicationDirPath() + "/xerahs", {"--json"});
    QTRY_VERIFY_WITH_TIMEOUT(app.canReadLine() || app.state() == QProcess::NotRunning, 20000);
    QVERIFY2(app.canReadLine(), app.readAllStandardError().constData());
    const auto started = json(app.readLine());
    QCOMPARE(started.value("delivery"), QJsonValue("started"));
    QCOMPARE(::kill(pid_t(app.processId()), signal), 0);
    QVERIFY(app.waitForFinished(15000));
    QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    QCOMPARE(app.exitCode(), exitCode);
    // Clean cancel: the lifetime lock is released, so a new process can be primary.
    NativeInstance next(temporary->filePath("com.xerahs.app"));
    QCOMPARE(next.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
  }

  // IFCAT-008 argument/environment precedence: arguments alone choose the
  // operation and output mode. The environment only locates the profile and
  // the display session; no XERAHS_* variable can change either.
  void environmentCannotSelectOperationOrOutputMode() {
    const QStringList arguments{"--list-workflows", "--non-interactive"};
    const auto baseline = run(arguments, environment(true));
    QCOMPARE(baseline.status, QProcess::NormalExit); QCOMPARE(baseline.exitCode, 0);
    QCOMPARE(baseline.out.count('\n'), 1); QVERIFY(baseline.err.isEmpty());
    auto noisy = environment(true);
    for (const auto *key : {"XERAHS_JSON", "XERAHS_NON_INTERACTIVE", "XERAHS_OPERATION", "XERAHS_ACTION", "XERAHS_CAPTURE_REGION", "XERAHS_RUN_WORKFLOW", "XERAHS_VERBOSE", "XERAHS_DEBUG", "XERAHS_OUTPUT"})
      noisy.insert(key, "1");
    noisy.insert("LC_ALL", "C"); noisy.insert("LANG", "de_DE.UTF-8"); noisy.insert("TERM", "dumb"); noisy.insert("NO_COLOR", "1");
    const auto withNoise = run(arguments, noisy);
    QCOMPARE(withNoise.exitCode, baseline.exitCode); QCOMPARE(withNoise.out, baseline.out); QCOMPARE(withNoise.err, baseline.err);
    // The same noise cannot turn a refusal into an operation, or change its mode.
    const auto refusal = run({"--capture-region", "--non-interactive", "--json"}, noisy);
    QCOMPARE(refusal.exitCode, 1);
    const auto result = json(refusal.out); QCOMPARE(result.value("error").toObject().value("code"), QJsonValue("presentation-required"));
    // The environment does choose the profile directory (the only effect it has).
    QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_CONFIG_HOME")));
    QVERIFY(writeCorruptWorkflowProfile());
    const auto located = run({"--list-workflows", "--non-interactive", "--json"}, environment(true));
    QCOMPARE(located.exitCode, 1); QVERIFY(!json(located.out).value("ok").toBool());
  }

  // IFCAT-008 stdout/stderr ownership (table in app/CLI-CONTRACT.md).
  void standardStreamsFollowTheOwnershipTable_data() {
    QTest::addColumn<QStringList>("arguments"); QTest::addColumn<bool>("corrupt"); QTest::addColumn<int>("exitCode");
    QTest::addColumn<QString>("stdoutKind"); QTest::addColumn<QString>("stderrPrefix");
    QTest::newRow("help") << QStringList{"--help"} << false << 0 << QString("help") << QString();
    QTest::newRow("list-ok") << QStringList{"--list-workflows"} << false << 0 << QString("json") << QString();
    QTest::newRow("list-ok-json") << QStringList{"--list-workflows", "--json"} << false << 0 << QString("json") << QString();
    QTest::newRow("list-error") << QStringList{"--list-workflows"} << true << 1 << QString("json") << QString("xerahs: workflow-");
    QTest::newRow("list-error-json") << QStringList{"--list-workflows", "--json"} << true << 1 << QString("json") << QString();
    QTest::newRow("result-error") << QStringList{"--workflow-result", "private-secret"} << false << 1 << QString("json") << QString("xerahs: workflow-execution-id-invalid: ");
    QTest::newRow("result-error-json") << QStringList{"--workflow-result", "private-secret", "--json"} << false << 1 << QString("json") << QString();
    QTest::newRow("refusal") << QStringList{"--capture-region", "--non-interactive"} << false << 1 << QString("empty") << QString("xerahs: presentation-required: ");
    QTest::newRow("refusal-json") << QStringList{"--capture-region", "--non-interactive", "--json"} << false << 1 << QString("json") << QString("xerahs: presentation-required: ");
    QTest::newRow("usage-invalid") << QStringList{"--private-secret", "--non-interactive"} << false << 1 << QString("empty") << QString("xerahs: usage-invalid: ");
    QTest::newRow("usage-invalid-json") << QStringList{"--private-secret", "--non-interactive", "--json"} << false << 1 << QString("json") << QString("xerahs: usage-invalid: ");
  }
  void standardStreamsFollowTheOwnershipTable() {
    QFETCH(QStringList, arguments); QFETCH(bool, corrupt); QFETCH(int, exitCode); QFETCH(QString, stdoutKind); QFETCH(QString, stderrPrefix);
    if (corrupt) QVERIFY(writeCorruptWorkflowProfile());
    const auto result = run(arguments, environment(true));
    QCOMPARE(result.status, QProcess::NormalExit); QCOMPARE(result.exitCode, exitCode);
    if (stdoutKind == u"help") {  // Stable names: every documented flag is listed by --help.
      for (const auto *flag : {"--capture-region", "--capture-full-screen", "--capture-monitor", "--capture-active-window", "--edit", "--run-workflow", "--list-workflows", "--workflow-result", "--start-recording", "--start-region-recording", "--stop-recording", "--json", "--non-interactive"})
        QVERIFY2(result.out.contains(flag), flag);
    }
    else if (stdoutKind == u"json") { QCOMPARE(result.out.count('\n'), 1); QVERIFY(!json(result.out).isEmpty()); QCOMPARE(json(result.out).value("schemaVersion"), QJsonValue(1)); }
    else QVERIFY(result.out.isEmpty());
    if (stderrPrefix.isEmpty()) QVERIFY2(result.err.isEmpty(), result.err.constData());
    else { QCOMPARE(result.err.count('\n'), 1); QVERIFY2(result.err.startsWith(stderrPrefix.toUtf8()), result.err.constData()); }
  }

  // IFCAT-010: no CLI mode, including every diagnostic or "verbose" setting
  // that exists, prints secrets, tokens, saved profile content or private paths.
  // The CLI has no verbose/diagnostic flag; such flags are refused as usage
  // errors, and the only diagnostic channels (Qt logging rules, ambient
  // environment) are exercised to prove they cannot weaken redaction.
  void redactionHoldsInEveryCliModeEvenWithDiagnosticSettings_data() {
    QTest::addColumn<QStringList>("arguments"); QTest::addColumn<QString>("profile"); QTest::addColumn<QString>("owner");
    const QString id = "01234567-89ab-4cde-8123-456789abcdef";
    QTest::newRow("help") << QStringList{"--help"} << QString("valid") << QString();
    QTest::newRow("list-ok") << QStringList{"--list-workflows"} << QString("valid") << QString();
    QTest::newRow("list-ok-json") << QStringList{"--list-workflows", "--json", "--non-interactive"} << QString("valid") << QString();
    QTest::newRow("list-corrupt") << QStringList{"--list-workflows"} << QString("corrupt") << QString();
    QTest::newRow("list-corrupt-json") << QStringList{"--list-workflows", "--json"} << QString("corrupt") << QString();
    QTest::newRow("result-invalid-id") << QStringList{"--workflow-result", "secret-arg-token"} << QString("valid") << QString();
    QTest::newRow("result-invalid-id-json") << QStringList{"--workflow-result", "secret-arg-token", "--json"} << QString("valid") << QString();
    QTest::newRow("result-not-found") << QStringList{"--workflow-result", id, "--json"} << QString("valid") << QString();
    QTest::newRow("result-corrupt-store") << QStringList{"--workflow-result", id} << QString("corrupt") << QString();
    for (const auto *flag : {"--capture-region", "--capture-full-screen", "--capture-monitor", "--capture-active-window", "--start-recording", "--start-region-recording", "--stop-recording"}) {
      QTest::newRow((QByteArray("refused") + flag).constData()) << QStringList{flag, "--non-interactive"} << QString("valid") << QString();
      QTest::newRow((QByteArray("refused-json") + flag).constData()) << QStringList{flag, "--non-interactive", "--json"} << QString("valid") << QString();
    }
    QTest::newRow("refused-edit") << QStringList{"--edit", "/private-name/secret-arg.png", "--non-interactive"} << QString("valid") << QString();
    QTest::newRow("refused-edit-json") << QStringList{"--edit", "/private-name/secret-arg.png", "--non-interactive", "--json"} << QString("valid") << QString();
    QTest::newRow("refused-workflow-json") << QStringList{"--run-workflow", id, "--non-interactive", "--json"} << QString("valid") << QString();
    QTest::newRow("usage-secret-flag") << QStringList{"--secret-arg-flag=secret-arg-token", "--non-interactive", "--json"} << QString("valid") << QString();
    QTest::newRow("usage-secret-positional") << QStringList{"secret-arg-token", "--non-interactive"} << QString("valid") << QString();
    for (const auto *flag : {"--verbose", "--debug", "--diagnostics", "--log=secret-arg-token", "-v", "-d"})
      QTest::newRow((QByteArray("no-verbose-mode") + flag).constData()) << QStringList{flag, "--non-interactive", "--json"} << QString("valid") << QString();
    for (const auto *owner : {"accept", "busy", "reject", "ack-drop"}) {
      QTest::newRow((QByteArray("forward-edit-") + owner).constData()) << QStringList{"--edit", "/private-name/secret-arg.png", "--json"} << QString("valid") << QString(owner);
      QTest::newRow((QByteArray("forward-workflow-") + owner).constData()) << QStringList{"--run-workflow", id} << QString("valid") << QString(owner);
    }
  }
  void redactionHoldsInEveryCliModeEvenWithDiagnosticSettings() {
    QFETCH(QStringList, arguments); QFETCH(QString, profile); QFETCH(QString, owner);
    if (profile == u"corrupt") QVERIFY(writeCorruptWorkflowProfile());
    else {
      using namespace xerahs::workflows;
      auto catalog = defaultCaptureWorkflows(); auto &entry = catalog.entries[0];
      entry.name = "Workflow"; entry.output.directory = "/private-destination"; entry.output.pattern = "private-pattern"; entry.extra = {{"secret", "private-token"}};
      const auto parent = temporary->filePath("XDG_CONFIG_HOME/com.xerahs.app/linux-qt"); QVERIFY(QDir().mkpath(parent));
      QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
      QFile file(parent + "/capture-workflows.json"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(workflowsJson(catalog)); file.close();
      QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
    }
    std::unique_ptr<QProcess> fixture;
    if (!owner.isEmpty()) {
      fixture = std::make_unique<QProcess>();
      fixture->start(QCoreApplication::applicationDirPath() + "/native-instance-fixture", {temporary->filePath("com.xerahs.app"), owner, QString::fromUtf8(serializeActivation(makeActivationRequest(ActivationAction::Activate)))});
      QTRY_VERIFY(fixture->canReadLine()); QCOMPARE(json(fixture->readLine()).value("event"), QJsonValue("primary"));
    }
    const bool headless = owner.isEmpty();
    for (const bool diagnostic : {false, true}) {
      auto env = environment(headless);
      env.insert("XERAHS_TOKEN", "secret-env-token"); env.insert("XERAHS_API_KEY", "secret-env-token"); env.insert("AWS_SECRET_ACCESS_KEY", "secret-env-token");
      if (diagnostic) { env.insert("QT_LOGGING_RULES", "*.debug=true;*.info=true;*.warning=true"); env.insert("XERAHS_VERBOSE", "1"); env.insert("XERAHS_DEBUG", "1"); }
      const auto result = run(arguments, env);
      QCOMPARE(result.status, QProcess::NormalExit);
      for (const auto &stream : {result.out, result.err}) {
        for (const auto *forbidden : {"secret-arg", "secret-env", "private-token", "private-pattern", "private-destination", "/private-", "Traceback", "backtrace"})
          QVERIFY2(!stream.contains(forbidden), qPrintable(QString("%1 leaked (diagnostic=%2): %3").arg(forbidden).arg(diagnostic).arg(QString::fromUtf8(stream))));
        QVERIFY2(!stream.contains(temporary->path().toUtf8()), stream.constData());
        auto withoutInvocation = stream; withoutInvocation.replace((QCoreApplication::applicationDirPath() + "/xerahs").toUtf8(), "xerahs");  // Help echoes argv[0], which the user passed.
        QVERIFY2(!withoutInvocation.contains(QDir::homePath().toUtf8()) || QDir::homePath() == "/", stream.constData());
      }
      if (arguments.contains("--json") && !arguments.contains("--help")) {  // Each JSON line is one versioned object with only stable fields.
        for (const auto &line : result.out.trimmed().split('\n')) {
          const auto object = json(line); QCOMPARE(object.value("schemaVersion"), QJsonValue(1));
          const QStringList allowed{"schemaVersion", "ok", "requestId", "workflowId", "delivery", "state", "error", "scope", "source", "runtimeStatus", "defaultWorkflowId", "workflows", "origin", "startedAt", "terminal", "lastRecordedState", "steps", "finishedAt", "pipelineTaskId", "diagnostic", "definitionVersion", "migration"};
          for (const auto &key : object.keys()) QVERIFY2(allowed.contains(key), qPrintable(key));
        }
      }
      if (arguments.first() == u"--verbose" || arguments.first() == u"--debug" || arguments.first() == u"--diagnostics" || arguments.first().startsWith("--log") || arguments.first() == u"-v" || arguments.first() == u"-d") {
        QCOMPARE(result.exitCode, 1); QCOMPARE(json(result.out).value("error").toObject().value("code"), QJsonValue("usage-invalid"));
      }
    }
  }

  // IFCAT-016: the stderr line and exit status of a real failing process come
  // from the shared presentation table, keyed by the code in the JSON result.
  void stderrAndExitStatusComeFromThePresentationTable_data() {
    QTest::addColumn<QStringList>("arguments"); QTest::addColumn<QString>("profile"); QTest::addColumn<QString>("owner"); QTest::addColumn<QString>("code");
    QTest::newRow("presentation-required") << QStringList{"--capture-region", "--non-interactive", "--json"} << QString() << QString() << QString("presentation-required");
    QTest::newRow("usage-invalid") << QStringList{"--private-secret", "--non-interactive", "--json"} << QString() << QString() << QString("usage-invalid");
    QTest::newRow("workflow-id-invalid") << QStringList{"--run-workflow", "not-a-uuid", "--json"} << QString() << QString() << QString("workflow-id-invalid");
    QTest::newRow("workflow-invalid") << QStringList{"--list-workflows"} << QString("corrupt") << QString() << QString("workflow-invalid");
    QTest::newRow("execution-id-invalid") << QStringList{"--workflow-result", "not-a-uuid"} << QString() << QString() << QString("workflow-execution-id-invalid");
    QTest::newRow("busy") << QStringList{"--capture-region", "--json"} << QString() << QString("busy") << QString("activation-busy");
    QTest::newRow("rejected") << QStringList{"--capture-region", "--json"} << QString() << QString("reject") << QString("activation-unavailable");
    QTest::newRow("handler-failed") << QStringList{"--capture-region", "--json"} << QString() << QString("throw") << QString("activation-handler-failed");
    QTest::newRow("delivery-unknown") << QStringList{"--capture-region", "--json"} << QString() << QString("ack-drop") << QString("activation-delivery-unknown");
    QTest::newRow("instance-unavailable") << QStringList{"--capture-region", "--json"} << QString() << QString("no-listen") << QString("instance-unavailable");
  }
  void stderrAndExitStatusComeFromThePresentationTable() {
    QFETCH(QStringList, arguments); QFETCH(QString, profile); QFETCH(QString, owner); QFETCH(QString, code);
    if (profile == u"corrupt") QVERIFY(writeCorruptWorkflowProfile());
    std::unique_ptr<QProcess> fixture;
    if (!owner.isEmpty()) {
      fixture = std::make_unique<QProcess>();
      fixture->start(QCoreApplication::applicationDirPath() + "/native-instance-fixture", {temporary->filePath("com.xerahs.app"), owner, QString::fromUtf8(serializeActivation(makeActivationRequest(ActivationAction::Activate)))});
      QTRY_VERIFY(fixture->canReadLine()); QCOMPARE(json(fixture->readLine()).value("event"), QJsonValue("primary"));
    }
    const auto result = run(arguments, environment(owner.isEmpty()));
    QCOMPARE(result.status, QProcess::NormalExit);
    QCOMPARE(json(result.out).value("error").toObject().value("code").toString(), code);
    QCOMPARE(result.exitCode, xerahs::shell::errorExitCode(code));
    const QString expected = QString("xerahs: %1: %2\n").arg(code, xerahs::shell::errorMessage(code));
    if (arguments.contains("--json") && (arguments.contains("--list-workflows") || arguments.contains("--workflow-result"))) QVERIFY(result.err.isEmpty());  // lookup commands stay quiet under --json
    else QCOMPARE(QString::fromUtf8(result.err), expected);
    if (code == u"activation-busy") QCOMPARE(result.exitCode, 2);
    if (json(result.out).contains("requestId")) QVERIFY(QUuid(json(result.out).value("requestId").toString()).toString(QUuid::WithoutBraces) == json(result.out).value("requestId").toString());
  }
};
QTEST_GUILESS_MAIN(CliContractTest)
#include "CliContractTest.moc"
