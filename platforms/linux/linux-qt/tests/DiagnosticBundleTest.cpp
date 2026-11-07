#include "diagnostics/DiagnosticBundle.h"
#include "shell/ErrorPresentation.h"
#include "actions/PostCapturePipeline.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <sys/stat.h>

using namespace xerahs;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray contents(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class DiagnosticBundleTest final : public QObject {
  Q_OBJECT
private slots:
  void declaredStatesAndStableErrorsExcludeValuesAndUnknownNames() {
    const auto previous = QCoreApplication::applicationVersion();
    const auto restore = qScopeGuard([&] { QCoreApplication::setApplicationVersion(previous); });
    QCoreApplication::setApplicationVersion("1.2.3");
    auto read = settings::repairSettings({{"saveFolder", "/SYNTHETIC_PRIVATE_VALUE"}, {"SYNTHETIC_PRIVATE_FIELD", "SYNTHETIC_PRIVATE_VALUE"}}, "/unused");
    read.fields["saveFolder"] = settings::FieldState::Invalid;
    read.diagnostics.append("SYNTHETIC_PRIVATE_DIAGNOSTIC"); read.writable = false;
    diagnostics::DiagnosticLog log;
    const auto at = QDateTime::fromString("2026-11-05T12:00:00Z", Qt::ISODate); const auto correlation = id();
    QVERIFY(log.append("activation-busy", correlation, diagnostics::Severity::Warning, at));
    QVERIFY(log.append("SYNTHETIC_PRIVATE_CODE", id(), diagnostics::Severity::Error, at.addSecs(1)));
    const auto bundle = diagnostics::diagnosticBundle(read, log, at);
    const auto bytes = bundle.bytes();
    QVERIFY(!bytes.contains("SYNTHETIC_PRIVATE")); QVERIFY(!bytes.contains("/unused"));
    const auto object = bundle.document();
    QCOMPARE(object.value("format").toString(), QString("com.xerahs.diagnostics")); QCOMPARE(object.value("version").toInt(), 1);
    QCOMPARE(object.value("runtime").toObject().value("applicationVersion").toString(), QString("1.2.3"));
    QCOMPARE(object.value("runtime").toObject().value("qtVersion").toString(), QString::fromLatin1(qVersion()));
    QVERIFY(!object.value("settings").toObject().value("writable").toBool());
    const auto fields = object.value("settings").toObject().value("fields").toArray();
    QCOMPARE(fields.size(), settings::settingFields().size());
    bool invalid = false;
    for (const auto &entry : fields) {
      const auto field = entry.toObject(); QCOMPARE(field.keys(), QStringList({"field", "state"}));
      if (field.value("field") == "saveFolder") { invalid = true; QCOMPARE(field.value("state").toString(), QString("invalid")); }
    }
    QVERIFY(invalid);
    const auto events = object.value("recentErrors").toArray(); QCOMPARE(events.size(), 2);
    QCOMPARE(events[0].toObject().value("code").toString(), QString("operation-attention-required"));
    QCOMPARE(events[0].toObject().value("severity").toString(), QString("error"));
    QCOMPARE(events[1].toObject().value("correlationId").toString(), correlation);
    for (const auto &entry : events)
      QCOMPARE(entry.toObject().keys(), QStringList({"code", "correlationId", "severity", "timestamp"}));
    for (const auto *key : {"paths", "values", "credentials", "environment", "captures", "clipboard", "hostname", "machineId"})
      QVERIFY(!object.contains(key) && !object.value("runtime").toObject().contains(key));
  }
  void latestPerCodeIsBoundedAndInvalidCorrelationsAreRejected() {
    diagnostics::DiagnosticLog log;
    const auto at = QDateTime::fromString("2026-11-05T12:00:00Z", Qt::ISODate); QString latest;
    for (int i = 0; i < 500; ++i) {
      latest = id(); QVERIFY(log.append("activation-busy", latest, diagnostics::Severity::Warning, at.addSecs(i)));
      QVERIFY(log.append("SYNTHETIC_PRIVATE_CODE_" + QString::number(i), id(), diagnostics::Severity::Error, at.addSecs(i)));
    }
    QCOMPARE(log.recent().size(), 2);
    bool found = false;
    for (const auto &entry : log.recent()) if (entry.toObject().value("code") == "activation-busy") {
      found = true; QCOMPARE(entry.toObject().value("correlationId").toString(), latest);
    }
    QVERIFY(found);
    QVERIFY(!log.append("activation-busy", "SYNTHETIC_PRIVATE_ID", diagnostics::Severity::Error, at));
    QVERIFY(!log.append("activation-busy", id(), diagnostics::Severity::Error, {}));
    QCOMPARE(log.recent().size(), 2); QVERIFY(!QJsonDocument(log.recent()).toJson().contains("SYNTHETIC_PRIVATE"));
    QVERIFY(log.recent().size() <= qsizetype(shell::errorPresentations().size() + 1));
  }
  void actualPipelineFailuresKeepOnlyStableCodeSeverityAndTaskCorrelation() {
    class FailedSave final : public actions::ActionExecutor {
    public:
      actions::ActionOutcome run(const actions::ActionRequest &) override {
        return {actions::ActionState::Failed, QString("SYNTHETIC_PRIVATE_DIAGNOSTIC"), {}};
      }
      bool createTemporaryUploadFile() override { return false; }
      void removeTemporaryUploadFile() override {}
    } executor;
    actions::CancellationToken cancellation;
    auto result = actions::runPipeline(id(), {"save"}, {}, executor, cancellation);
    QCOMPARE(result.orderedResults.size(), size_t(1)); QCOMPARE(result.orderedResults.front().state, actions::ActionState::Failed);
    diagnostics::DiagnosticLog log; diagnostics::recordPipelineErrors(log, result);
    const auto events = log.recent(); QCOMPARE(events.size(), 1);
    QCOMPARE(events[0].toObject().value("code").toString(), QString("permanent-failure"));
    QCOMPARE(events[0].toObject().value("severity").toString(), QString("error"));
    QCOMPARE(events[0].toObject().value("correlationId").toString(), result.taskId);
    QVERIFY(!QJsonDocument(events).toJson().contains("SYNTHETIC_PRIVATE"));
    result.orderedResults.front().state = actions::ActionState::Cancelled;
    diagnostics::DiagnosticLog cancelled; diagnostics::recordPipelineErrors(cancelled, result); QVERIFY(cancelled.recent().isEmpty());
  }

  void reviewedSnapshotWritesPrivatelyWithoutOverwritingAnyExistingTarget() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    diagnostics::DiagnosticLog log; settings::SettingsRead read;
    const auto bundle = diagnostics::diagnosticBundle(read, log);
    const auto preview = bundle.bytes();
    read.values.insert("late", "SYNTHETIC_PRIVATE_VALUE"); read.fields["saveFolder"] = settings::FieldState::Invalid;
    QVERIFY(log.append("activation-busy", id(), diagnostics::Severity::Warning));
    const auto path = dir.filePath("reviewed.json");
    QCOMPARE(bundle.save(path), storage::StoreStatus::Ok); QCOMPARE(contents(path), preview);
    struct stat info{}; QCOMPARE(::stat(QFile::encodeName(path).constData(), &info), 0); QCOMPARE(info.st_mode & 0777, mode_t(0600));
    QCOMPARE(bundle.save(path), storage::StoreStatus::Changed); QCOMPARE(contents(path), preview);
    { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("ORIGINAL SYNTHETIC CONTENT"); }
    QCOMPARE(bundle.save(path), storage::StoreStatus::Invalid); QCOMPARE(contents(path), QByteArray("ORIGINAL SYNTHETIC CONTENT"));
    const auto lockedPath = dir.filePath("locked.json"); QLockFile lock(lockedPath + ".lock"); QVERIFY(lock.tryLock());
    QCOMPARE(bundle.save(lockedPath), storage::StoreStatus::Busy); QVERIFY(!QFile::exists(lockedPath)); lock.unlock();
    const auto alias = dir.filePath("alias.json"); QVERIFY(QFile::link(path, alias));
    QCOMPARE(bundle.save(alias), storage::StoreStatus::Unsafe); QCOMPARE(contents(path), QByteArray("ORIGINAL SYNTHETIC CONTENT"));
    QCOMPARE(bundle.save("relative.json"), storage::StoreStatus::Unsafe);
  }
  void unassignedApplicationVersionIsExplicitlyUnknown() {
    const auto previous = QCoreApplication::applicationVersion();
    const auto restore = qScopeGuard([&] { QCoreApplication::setApplicationVersion(previous); });
    QCoreApplication::setApplicationVersion({}); diagnostics::DiagnosticLog log;
    const auto runtime = diagnostics::diagnosticBundle({}, log).document().value("runtime").toObject();
    QVERIFY(runtime.value("applicationVersion").isNull());
    QCOMPARE(runtime.value("applicationVersionState").toString(), QString("unversioned-development-build"));
  }
};
QTEST_GUILESS_MAIN(DiagnosticBundleTest)
#include "DiagnosticBundleTest.moc"
