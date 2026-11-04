#include "settings/ReferenceMigration.h"
#include "settings/ApplicationSettings.h"
#include "uploads/DestinationCatalog.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace xerahs;
namespace {
QByteArray get(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool put(const QString &p, const QByteArray &b) { QFile f(p); return f.open(QIODevice::WriteOnly) && f.write(b) == b.size(); }
settings::ConfigurationPaths paths(const QTemporaryDir &d) { return {d.filePath("app.json"), d.filePath("editor.json"), d.filePath("workflows.json"), d.filePath("destinations.json"), d.path()}; }
void fixtures(const QTemporaryDir &d) { for (const auto &name : QDir(REFERENCE_FIXTURES).entryList({"*.json"}, QDir::Files)) QFile::copy(QDir(REFERENCE_FIXTURES).filePath(name), d.filePath(name)); }
QList<QByteArray> snapshot(const settings::ConfigurationPaths &p) { return {get(p.application), get(p.editor), get(p.workflows), get(p.destinations)}; }
}
class ReferenceMigrationTest : public QObject {
  Q_OBJECT
private slots:
  void secretsStoreIsNeverInspectedAndSourceSymlinksAreRefused() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ReferenceMigration migration(p);
    QVERIFY(put(source.filePath("SecretsStore.json"), "fixture-secret-one"));
    const auto first = migration.inspectFolder(source.path()); QVERIFY(first.valid());
    QVERIFY(put(source.filePath("SecretsStore.json"), "fixture-secret-two"));
    const auto second = migration.inspectFolder(source.path()); QVERIFY(second.valid()); QCOMPARE(second.fingerprint, first.fingerprint);
    QVERIFY(!second.configuration.preview().contains("fixture-secret"));
    QVERIFY(QFile::remove(source.filePath("ApplicationConfig.json")));
    QVERIFY(QFile::link(source.filePath("SecretsStore.json"), source.filePath("ApplicationConfig.json")));
    const auto unsafe = migration.inspectFolder(source.path()); QVERIFY(!unsafe.valid()); QVERIFY(unsafe.configuration.code.contains("unsafe")); QVERIFY(!migration.backupAvailable());
  }
  void staleNativePreviewIsRejectedBeforeCreatingBackup() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ReferenceMigration migration(p);
    const auto plan = migration.inspectFolder(source.path()); QVERIFY(plan.valid());
    settings::ApplicationSettingsStore current(p.application, native.path()); auto values = current.current().values; values.insert("showNotification", false); QVERIFY(current.save(values).isEmpty());
    const auto before = snapshot(p); QCOMPARE(migration.apply(source.path(), plan, true), QString("migration-native-settings-changed")); QCOMPARE(snapshot(p), before); QVERIFY(!migration.backupAvailable());
  }

  void dryRunCancelAndConfirmedMigrationPreserveSource() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ReferenceMigration migration(p);
    QMap<QString, QByteArray> originals; for (const auto &n : QDir(source.path()).entryList(QDir::Files)) originals.insert(n, get(source.filePath(n)));
    auto plan = migration.inspectFolder(source.path()); QVERIFY2(plan.valid(), qPrintable(plan.configuration.code));
    QVERIFY(plan.configuration.preview().contains("secret omitted")); QVERIFY(plan.configuration.preview().contains("unsupported"));
    QVERIFY(!plan.configuration.preview().contains("fixture-secret-never-export"));
    QVERIFY(!QFile::exists(p.application)); QVERIFY(!migration.backupAvailable());
    QCOMPARE(migration.apply(source.path(), plan, false), QString("migration-cancelled")); QVERIFY(!migration.backupAvailable());
    auto result = migration.apply(source.path(), plan, true); QVERIFY2(result.isEmpty(), qPrintable(result)); QVERIFY(migration.backupAvailable());
    settings::ApplicationSettingsStore store(p.application, native.path()); QVERIFY(!store.current().values["showTrayIcon"].toBool()); QVERIFY(!store.current().values["showNotification"].toBool()); QVERIFY(store.current().values["playCaptureSound"].toBool());
    auto wf = workflows::parseCaptureWorkflows(get(p.workflows)); QVERIFY(wf.catalog); QCOMPARE(wf.catalog->entries.size(), 2); QCOMPARE(wf.catalog->entries.last().name, QString("Reference region"));
    QCOMPARE(wf.catalog->entries.last().output.directory, QString("/tmp/reference-captures"));
    uploads::DestinationCatalog dest(p.destinations); QCOMPARE(dest.entries().size(), 1); QVERIFY(get(p.destinations).contains("{secret:"));
    for (const auto &path : {p.application, p.editor, p.workflows, p.destinations, p.application + ".reference-migration"}) QVERIFY(!get(path).contains("fixture-secret-never-export"));
    for (auto it = originals.cbegin(); it != originals.cend(); ++it) QCOMPARE(get(source.filePath(it.key())), it.value());
  }
  void retryHasNoDuplicatesAndBackupRestoresExactPriorState() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ConfigurationTransfer transfer(p);
    QVERIFY(transfer.apply(transfer.inspect(QJsonDocument(transfer.exportPlan().candidate).toJson()), true).isEmpty());
    const auto before = snapshot(p); const auto history = native.filePath("history.json"); QVERIFY(put(history, "private native history"));
    settings::ReferenceMigration migration(p); auto plan = migration.inspectFolder(source.path()); QVERIFY(plan.valid()); QVERIFY(migration.apply(source.path(), plan, true).isEmpty());
    const auto after = snapshot(p); plan = migration.inspectFolder(source.path()); QVERIFY(plan.completed); QCOMPARE(migration.apply(source.path(), plan, true), QString("migration-already-completed")); QCOMPARE(snapshot(p), after);
    QCOMPARE(migration.restore(false), QString("migration-cancelled")); QCOMPARE(snapshot(p), after);
    QVERIFY(migration.restore(true).isEmpty()); QCOMPARE(snapshot(p), before); QCOMPARE(get(history), QByteArray("private native history"));
    plan = migration.inspectFolder(source.path()); QVERIFY(!plan.completed); QVERIFY(migration.apply(source.path(), plan, true).isEmpty()); QCOMPARE(snapshot(p), after);
    const auto backupRevision = migration.backupRevision();
    QVERIFY(migration.dismissBackup(true).isEmpty()); QVERIFY(!migration.backupAvailable()); QVERIFY(migration.inspectFolder(source.path()).completed);
    const auto dismissed = snapshot(p); QCOMPARE(migration.restore(true, backupRevision), QString("migration-backup-changed")); QCOMPARE(snapshot(p), dismissed);
  }
  void middleFailureLeavesAllStoresAndReferenceFilesUntouched() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ReferenceMigration migration(p);
    auto plan = migration.inspectFolder(source.path()); const auto before = snapshot(p); const auto original = get(source.filePath("ApplicationConfig.json"));
    auto result = migration.apply(source.path(), plan, true, [](const QString &phase, int n) { return phase == "publish" && n == 1; });
    QVERIFY(result.contains("rolled-back")); QCOMPARE(snapshot(p), before); QCOMPARE(get(source.filePath("ApplicationConfig.json")), original); QVERIFY(migration.backupAvailable());
    plan = migration.inspectFolder(source.path()); QVERIFY(!plan.completed); QVERIFY(migration.apply(source.path(), plan, true).isEmpty());
    QVERIFY(migration.restore(true).isEmpty()); QCOMPARE(snapshot(p), before);
  }
  void changedSourceAndMalformedNestedInputBlockBeforeMutation() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ReferenceMigration migration(p);
    const auto plan = migration.inspectFolder(source.path()); QVERIFY(plan.valid()); QVERIFY(put(source.filePath("ApplicationConfig.json"), "{\"ShowTray\":true}"));
    QCOMPARE(migration.apply(source.path(), plan, true), QString("migration-source-changed")); QVERIFY(!QFile::exists(p.application));
    QVERIFY(put(source.filePath("WorkflowsConfig.json"), "{\"Hotkeys\":\"invalid-nested\"}"));
    QVERIFY(!migration.inspectFolder(source.path()).valid()); QVERIFY(!migration.backupAvailable());
  }
  void migrationKeepsUnrelatedNativeDestinationsAndWorkflowPaths() {
    QTemporaryDir source, native; fixtures(source); auto p = paths(native); settings::ConfigurationTransfer transfer(p);
    auto root = transfer.exportPlan().candidate; auto stores = root["stores"].toObject(); auto wf = stores["workflows"].toObject(); auto entries = wf["workflows"].toArray(); auto item = entries[0].toObject(); auto output = item["output"].toObject(); output.insert("directory", native.filePath("captures")); item.insert("output", output); entries[0] = item; wf.insert("workflows", entries); stores.insert("workflows", wf); root.insert("stores", stores);
    QVERIFY(transfer.apply(transfer.inspect(QJsonDocument(root).toJson()), true).isEmpty());
    uploads::DestinationCatalog catalog(p.destinations); uploads::UploadDestination dest; dest.id = QUuid::createUuid(); dest.name = "Existing"; dest.requestUrl = "https://example.org"; dest.data = "existing-body"; QVERIFY(catalog.add(dest).isEmpty());
    settings::ReferenceMigration migration(p); auto plan = migration.inspectFolder(source.path()); QVERIFY2(plan.valid(), qPrintable(plan.configuration.code)); QVERIFY(migration.apply(source.path(), plan, true).isEmpty());
    uploads::DestinationCatalog restart(p.destinations); QCOMPARE(restart.find(dest.id)->data, QString("existing-body")); QCOMPARE(restart.entries().size(), 2);
    const auto workflows = workflows::parseCaptureWorkflows(get(p.workflows)); QCOMPARE(workflows.catalog->entries.first().output.directory, native.filePath("captures"));
  }
};
QTEST_GUILESS_MAIN(ReferenceMigrationTest)
#include "ReferenceMigrationTest.moc"
