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
    if (mode == "future-version") root.insert("version", 3);
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
    w.output.directory = "/private/captures"; w.output.pattern = "%width-%height"; w.output.extra = {{"secret", "must-not-report"}};
    snapshot.result.workflowSnapshot = workflowSnapshot(w); snapshot.result.workflowSnapshot.insert("secret", "must-not-report");
    auto rawOutput = snapshot.result.workflowSnapshot.value("output").toObject(); rawOutput.insert("secret", "must-not-report"); snapshot.result.workflowSnapshot.insert("output", rawOutput);
    const auto bytes = serializePipelineJournal(snapshot); QVERIFY(!bytes.contains("must-not-report"));
    const auto recovered = parsePipelineJournal(bytes, snapshot.result.taskId); QVERIFY(recovered.record);
    const auto metadata = recovered.record->report.value("extensions").toObject().value("com.xerahs.app").toObject().value("capture_workflow").toObject();
    QCOMPARE(metadata.value("id"), QJsonValue(w.id.toString(QUuid::WithoutBraces))); QCOMPARE(metadata.value("actions"), QJsonValue(QJsonArray::fromStringList(w.actions)));
    QCOMPARE(metadata.value("output").toObject().value("directory"), QJsonValue(w.output.directory)); QCOMPARE(metadata.value("output").toObject().value("pattern"), QJsonValue(w.output.pattern));
  }
  void legacyDefinitionsKeepTheirDefaultsAndOnlyUpgradeOnExplicitSave() {
    auto root = QJsonDocument::fromJson(workflowsJson(defaultCaptureWorkflows())).object(); root.insert("version", 1);
    auto entries = root.value("workflows").toArray(); auto entry = entries[0].toObject(); entry.remove("output"); entry.insert("futureField", "keep"); entries[0] = entry; root.insert("workflows", entries);
    const auto original = QJsonDocument(root).toJson(); QTemporaryDir dir; const auto path = dir.filePath("legacy.json");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)); QCOMPARE(file.write(original), original.size()); file.close();
    FilePreferenceStorage storage(path); CaptureWorkflowStore store(storage); QVERIFY(store.current().catalog);
    const auto draft = *store.current().catalog; QVERIFY(draft.entries[0].output.directory.isEmpty()); QCOMPARE(draft.entries[0].output.pattern, CaptureOutput{}.pattern);
    QCOMPARE(storage.read().bytes, original); QVERIFY(store.save(draft, store.revision()).saved);
    const auto upgraded = QJsonDocument::fromJson(storage.read().bytes).object(); QCOMPARE(upgraded.value("version"), QJsonValue(2));
    const auto parsed = parseCaptureWorkflows(storage.read().bytes); QVERIFY(parsed.catalog); QCOMPARE(parsed.catalog->entries[0].extra.value("futureField"), QJsonValue("keep"));
    entry.insert("output", QJsonObject{{"futureAuthority", true}}); entries[0] = entry; root.insert("workflows", entries);
    const auto conflict = parseCaptureWorkflows(QJsonDocument(root).toJson()); QVERIFY(!conflict.catalog); QCOMPARE(conflict.diagnostic, QString("workflow-upgrade-conflict"));
  }
  void outputPreviewUsesProductionRulesWithoutCommittingOrReadingFiles_data() {
    QTest::addColumn<QString>("pattern"); QTest::addColumn<QString>("expected"); QTest::addColumn<QString>("diagnostic");
    QTest::newRow("date-size") << QStringLiteral("shot-%y%mo%d-%h%mi%s-%widthx%height") << QStringLiteral("shot-20260102-150405-1920x1080.png") << QStringLiteral("");
    QTest::newRow("sanitized") << QStringLiteral("CON:<shot>/foo?") << QStringLiteral("CON_shot_foo_.png") << QStringLiteral("");
    QTest::newRow("reserved") << QStringLiteral("CON") << QStringLiteral("_CON.png") << QStringLiteral("");
    QTest::newRow("unknown-literal-percent") << QStringLiteral("%%i_%future") << QStringLiteral("%i_%future.png") << QStringLiteral("");
    QTest::newRow("random") << QStringLiteral("%rn{4}") << QStringLiteral("0123.png") << QStringLiteral("");
    QTest::newRow("blank") << QStringLiteral("   ") << QStringLiteral("_.png") << QStringLiteral("");
    QTest::newRow("invalid-repeat") << QStringLiteral("%ra{0}") << QStringLiteral("") << QStringLiteral("filename-invalid-argument");
    QTest::newRow("unterminated") << QStringLiteral("%ra{4") << QStringLiteral("") << QStringLiteral("filename-unterminated-argument");
    QTest::newRow("random-file-denied") << QStringLiteral("%rf{/private/file}") << QStringLiteral("") << QStringLiteral("filename-file-permission-denied");
    QTest::newRow("counter") << QStringLiteral("%i{3}") << QStringLiteral("001.png") << QStringLiteral("workflow-counter-unavailable");
  }
  void outputPreviewUsesProductionRulesWithoutCommittingOrReadingFiles() {
    QFETCH(QString, pattern); QFETCH(QString, expected); QFETCH(QString, diagnostic);
    CaptureOutput output; output.pattern = pattern; const auto first = previewCaptureOutput(output), second = previewCaptureOutput(output);
    QCOMPARE(first.filename, expected); QCOMPARE(first.diagnostic, diagnostic); QCOMPARE(second.filename, first.filename); QCOMPARE(second.diagnostic, first.diagnostic);
    if (first.error) { QCOMPARE(first.error->offset, qint64(0)); QVERIFY(first.error->token); }
    output.pattern = QString::fromUtf8("😀%ra{0}"); const auto scalar = previewCaptureOutput(output); QVERIFY(scalar.error); QCOMPARE(scalar.error->offset, qint64(1));
  }
  void outputValidationPreservesUnknownFieldsAndRefusesInvalidSave_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"relative", "traversal", "dot", "control", "oversize", "counter", "file", "malformed", "unknown"}) QTest::newRow(mode) << QString(mode);
  }
  void outputValidationPreservesUnknownFieldsAndRefusesInvalidSave() {
    QFETCH(QString, mode); QTemporaryDir dir; FilePreferenceStorage storage(dir.filePath("workflows.json")); CaptureWorkflowStore store(storage); auto draft = *store.current().catalog; auto &output = draft.entries[0].output;
    output.directory = dir.filePath("new/nested"); output.pattern = "shot-%width-%height"; output.extra = {{"futureOutput", QJsonObject{{"private", true}}}};
    if (mode == "relative") output.directory = "relative";
    if (mode == "traversal") output.directory += "/../escape";
    if (mode == "dot") output.directory += "/./child";
    if (mode == "control") output.directory += QChar(0);
    if (mode == "oversize") output.pattern = QString(4097, 'x');
    if (mode == "counter") output.pattern = "%iX";
    if (mode == "file") output.pattern = "%rf{/private/input}";
    if (mode == "malformed") output.pattern = "%ra{257}";
    const auto saved = store.save(draft, store.revision()); QCOMPARE(saved.saved, mode == "unknown");
    QVERIFY(!QFile::exists(dir.filePath("new")));
    if (saved.saved) { CaptureWorkflowStore reopened(storage); QCOMPARE(workflowsJson(*reopened.current().catalog), workflowsJson(draft)); }
    else { QVERIFY(!QFile::exists(dir.filePath("workflows.json"))); QVERIFY(!saved.diagnostic.isEmpty()); }
  }
};
QTEST_GUILESS_MAIN(CaptureWorkflowsTest)
#include "CaptureWorkflowsTest.moc"
