#include "workflows/CaptureWorkflows.h"
#include "actions/PipelineJournal.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::workflows;
using namespace xerahs::editor;
using namespace xerahs::actions;
class CaptureWorkflowsTest final : public QObject {
  Q_OBJECT
private slots:
  void defaultsAndUnknownFieldsRoundTripWithoutActivatingUnknownActions() {
    auto catalog = defaultCaptureWorkflows(); catalog.extra = {{"futureRoot", true}}; auto &w = catalog.entries[0];
    w.extra = {{"futureSecretReference", "private-fixture"}}; w.actions.append("future-action");
    const auto parsed = parseCaptureWorkflows(workflowsJson(catalog)); QVERIFY(parsed.catalog);
    QCOMPARE(workflowsJson(*parsed.catalog), workflowsJson(catalog));
    QCOMPARE(workflowAvailability(w, {"save", "image-clipboard"}), QString("workflow-action-unavailable"));
    const auto snapshot = workflowSnapshot(w); QVERIFY(!snapshot.contains("futureSecretReference")); QCOMPARE(snapshot.value("name"), QJsonValue(w.name));
    w.actions = QStringList{"save"}; w.chooseActions = true; QCOMPARE(workflowActions(w), QStringList({"after-capture-window", "save"}));
    QVERIFY(workflowAvailability(w, {"save"}).isEmpty()); w.capture = "recording"; QCOMPARE(workflowAvailability(w, {"save"}), QString("workflow-capture-unavailable"));
  }
  void invalidDefinitionsAreRejected_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"json", "future-version", "duplicate-id", "duplicate-action", "empty-name", "control-name", "default-missing", "action-type", "choose-type", "capacity"}) QTest::newRow(mode) << QString(mode);
  }
  void invalidDefinitionsAreRejected() {
    QFETCH(QString, mode); auto root = QJsonDocument::fromJson(workflowsJson(defaultCaptureWorkflows())).object(); auto array = root.value("workflows").toArray(); auto w = array[0].toObject();
    if (mode == "json") { QVERIFY(!parseCaptureWorkflows("broken").catalog); return; }
    if (mode == "future-version") root.insert("version", 2);
    if (mode == "duplicate-id") array.append(w);
    if (mode == "duplicate-action") w.insert("actions", QJsonArray{"save", "save"});
    if (mode == "empty-name") w.insert("name", " ");
    if (mode == "control-name") w.insert("name", "private\nname");
    if (mode == "default-missing") root.insert("defaultId", QUuid::createUuid().toString());
    if (mode == "action-type") w.insert("actions", QJsonArray{42});
    if (mode == "choose-type") w.insert("chooseActions", "true");
    if (mode == "capacity") for (int i = 0; i < 64; ++i) { auto extra = w; extra.insert("id", QUuid::createUuid().toString()); array.append(extra); }
    array.replace(0, w); root.insert("workflows", array); const auto parsed = parseCaptureWorkflows(QJsonDocument(root).toJson()); QVERIFY(!parsed.catalog); QVERIFY(!parsed.diagnostic.isEmpty());
  }
  void privateAtomicSaveRestartStaleAndLockedChanges() {
    QTemporaryDir dir; const auto path = dir.filePath("workflows.json"); FilePreferenceStorage storage(path); CaptureWorkflowStore store(storage);
    QVERIFY(store.current().catalog); QVERIFY(!QFile::exists(path)); auto draft = *store.current().catalog; auto second = draft.entries[0]; second.id = QUuid::createUuid(); second.name = "Quick copy"; second.actions = QStringList{"image-clipboard"};
    draft.entries.append(second); draft.defaultId = second.id; const auto revision = store.revision(); QVERIFY(store.save(draft, revision).saved);
    FilePreferenceStorage other(path); CaptureWorkflowStore restarted(other); QVERIFY(restarted.current().catalog); QCOMPARE(restarted.current().catalog->defaultId, second.id); QCOMPARE(restarted.current().catalog->entries[1].actions, second.actions);
    QVERIFY(!(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)));
    QVERIFY(!store.save(draft, revision).saved); QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0)); draft.entries[0].name = "Renamed";
    QVERIFY(!store.save(draft, store.revision()).saved); QCOMPARE(store.current().catalog->entries[0].name, QString("Save and copy image")); lock.unlock();
    QVERIFY(restarted.save(draft, restarted.revision()).saved); QCOMPARE(store.unchanged(), QString("workflow-changed")); QVERIFY(!store.save(draft, store.revision()).saved);
    store.reload(); QVERIFY(store.unchanged().isEmpty()); QCOMPARE(store.current().catalog->entries[0].name, QString("Renamed"));
  }
  void corruptFileCannotBeOverwritten() {
    QTemporaryDir dir; const auto path = dir.filePath("workflows.json"); QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.setPermissions(QFile::ReadOwner | QFile::WriteOwner); f.write("broken"); f.close();
    FilePreferenceStorage storage(path); CaptureWorkflowStore store(storage); QVERIFY(!store.current().catalog); QVERIFY(!store.save(defaultCaptureWorkflows(), store.revision()).saved);
    QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), QByteArray("broken"));
  }
  void workflowSelectionSurvivesReportAndJournalRecoveryWithoutUnknownFields() {
    auto w = defaultCaptureWorkflows().entries[0]; w.extra = {{"secret", "must-not-report"}};
    PipelineJournalSnapshot snapshot; snapshot.result.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces); snapshot.createdAt = QDateTime::currentDateTimeUtc(); snapshot.terminal = true;
    snapshot.result.workflowSnapshot = workflowSnapshot(w); snapshot.result.workflowSnapshot.insert("secret", "must-not-report");
    const auto bytes = serializePipelineJournal(snapshot); QVERIFY(!bytes.contains("must-not-report"));
    const auto recovered = parsePipelineJournal(bytes, snapshot.result.taskId); QVERIFY(recovered.record);
    const auto metadata = recovered.record->report.value("extensions").toObject().value("com.xerahs.app").toObject().value("capture_workflow").toObject();
    QCOMPARE(metadata.value("id"), QJsonValue(w.id.toString(QUuid::WithoutBraces))); QCOMPARE(metadata.value("actions"), QJsonValue(QJsonArray::fromStringList(w.actions)));
  }
};
QTEST_GUILESS_MAIN(CaptureWorkflowsTest)
#include "CaptureWorkflowsTest.moc"
