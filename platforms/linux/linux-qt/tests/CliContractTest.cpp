// Actual-process tests of the CLI contract in app/CLI-CONTRACT.md (IFCAT-008).
#include "NativeInstance.h"
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
    QTest::newRow("result-error") << QStringList{"--workflow-result", "private-secret"} << false << 1 << QString("json") << QString("xerahs: Execution could not be read");
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
};
QTEST_GUILESS_MAIN(CliContractTest)
#include "CliContractTest.moc"
