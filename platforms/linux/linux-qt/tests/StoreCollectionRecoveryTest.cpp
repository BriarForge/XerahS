#include "workflows/CaptureWorkflows.h"
#include "uploads/DestinationCatalog.h"
#include "watch/WatchFolders.h"
#include "image-editor/EditorPreferences.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs;
namespace {
bool write(const QString &path, const QJsonObject &object) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
  const auto bytes = QJsonDocument(object).toJson(); return file.write(bytes) == bytes.size();
}
QJsonObject read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{}; }
uploads::UploadDestination destination(const QString &name) {
  uploads::UploadDestination d; d.id = QUuid::createUuid(); d.name = name; d.requestUrl = "https://example.invalid/upload"; d.body = uploads::BodyType::Binary; return d;
}
}
class StoreCollectionRecoveryTest : public QObject {
  Q_OBJECT
private slots:
  void workflowsIsolateItemsPreserveIdsOrderingAndMissingDefault() {
    QTemporaryDir dir; const auto path = dir.filePath("workflows.json");
    auto source = workflows::defaultCaptureWorkflows();
    auto second = source.entries.front(); second.id = QUuid::createUuid(); second.name = "Second"; source.entries.append(second);
    auto root = QJsonDocument::fromJson(workflows::workflowsJson(source)).object();
    auto entries = root.value("workflows").toArray(); entries.insert(1, 42); entries.append(entries.at(0)); root.insert("workflows", entries);
    QVERIFY(write(path, root));
    editor::FilePreferenceStorage storage(path); workflows::CaptureWorkflowStore store(storage);
    QVERIFY(store.current().catalog); QCOMPARE(store.current().catalog->entries.size(), 2);
    QCOMPARE(store.current().catalog->unreadable.size(), 2);
    QCOMPARE(store.current().catalog->entries[1].id, second.id);
    QCOMPARE(store.resolve(second.id, workflows::implementedCaptureActions()).workflow->name, second.name);
    auto draft = *store.current().catalog; draft.entries[0].name = "Repaired name";
    QVERIFY(store.save(draft, store.revision()).saved);
    workflows::CaptureWorkflowStore reopened(storage); QCOMPARE(reopened.current().catalog->entries.size(), 2);
    QCOMPARE(reopened.current().catalog->unreadable, draft.unreadable);
    QCOMPARE(read(path).value("workflows").toArray().size(), 4);
    root = read(path); root.insert("defaultId", QUuid::createUuid().toString()); QVERIFY(write(path, root)); reopened.reload();
    QCOMPARE(reopened.current().diagnostic, QString("workflow-default-missing"));
    QVERIFY(!reopened.resolve({}, workflows::implementedCaptureActions()).workflow);
    draft = *reopened.current().catalog; draft.defaultId = second.id;
    QVERIFY(reopened.save(draft, reopened.revision()).saved);
    QCOMPARE(reopened.resolve({}, workflows::implementedCaptureActions()).workflow->id, second.id);
    draft = *reopened.current().catalog; const auto removedId = draft.entries[0].id; draft.entries.removeFirst();
    QVERIFY(reopened.save(draft, reopened.revision()).saved); reopened.reload();
    QVERIFY(!reopened.resolve(removedId, workflows::implementedCaptureActions()).workflow);
    QCOMPARE(reopened.current().catalog->entries.size(), 1);
  }
  void destinationsIsolateDuplicatesKeepOrderAndBoundTheList() {
    QTemporaryDir dir; const auto path = dir.filePath("destinations.json");
    const auto first = destination("First"), second = destination("Second");
    auto a = QJsonDocument::fromJson(uploads::writeDestination(first)).object();
    auto b = QJsonDocument::fromJson(uploads::writeDestination(second)).object();
    QJsonObject root{{"format", "com.xerahs.app/linux-qt/upload-destinations"}, {"version", 1},
        {"default", first.id.toString()}, {"destinations", QJsonArray{a, 42, a, b}}};
    QVERIFY(write(path, root)); uploads::DestinationCatalog catalog(path);
    QCOMPARE(catalog.entries().size(), 2); QCOMPARE(catalog.unreadableCount(), 2);
    QCOMPARE(catalog.entries()[0].destination.id, first.id); QCOMPARE(catalog.entries()[1].destination.id, second.id);
    QCOMPARE(catalog.add(first), QString("duplicate-id"));
    QVERIFY(catalog.setDefault(second.id).isEmpty());
    uploads::DestinationCatalog reopened(path); QCOMPARE(reopened.defaultId(), second.id); QCOMPARE(reopened.unreadableCount(), 2);
    uploads::MemoryCredentialStore secrets; QVERIFY(reopened.remove(second.id, secrets).isEmpty());
    QVERIFY(!reopened.find(second.id)); QCOMPARE(reopened.defaultId(), first.id);
    QVERIFY(reopened.remove(first.id, secrets).isEmpty());
    uploads::DestinationCatalog noResurrection(path);
    QVERIFY(noResurrection.entries().isEmpty()); QCOMPARE(noResurrection.unreadableCount(), 2);
    QVERIFY(noResurrection.add(second).isEmpty()); QVERIFY(noResurrection.defaultId().isNull());
    QVERIFY(noResurrection.setDefault(second.id).isEmpty()); QCOMPARE(noResurrection.defaultId(), second.id);
    QJsonArray full; for (int i = 0; i < uploads::maximumDestinations; ++i) { auto item = a; item.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces)); full.append(item); }
    root.insert("destinations", full); QVERIFY(write(path, root)); uploads::DestinationCatalog bounded(path);
    QCOMPARE(bounded.entries().size(), uploads::maximumDestinations);
    QCOMPARE(bounded.add(second), QString("catalog-too-large"));
    full.append(b); root.insert("destinations", full); QVERIFY(write(path, root));
    uploads::DestinationCatalog oversized(path); QVERIFY(!oversized.writable());
  }
  void watchRulesRetainHealthyEntriesStableIdsAndMissingFolders() {
    QTemporaryDir dir; const auto input = dir.filePath("input"); QVERIFY(QDir().mkpath(input));
    const auto path = dir.filePath("watch-folders.json");
    QJsonObject rule{{"path", input}, {"filters", QJsonArray{"*.png"}}, {"enabled", true}, {"recursive", false}, {"action", 0}};
    QJsonObject root{{"format", "com.xerahs.app/watch-folders"}, {"version", 1}, {"items", QJsonArray{rule, 42, rule}}};
    QVERIFY(write(path, root)); watch::WatchFolders store(dir.path());
    QVERIFY(store.loadError().isEmpty()); QCOMPARE(store.rules().size(), 1); QCOMPARE(store.diagnostics().size(), 2);
    const auto id = store.rules()[0].id; QVERIFY(!id.isNull());
    watch::WatchFolders beforeSave(dir.path()); QCOMPARE(beforeSave.rules()[0].id, id);
    QVERIFY(store.saveRules(store.rules(), dir.filePath("captures")).isEmpty());
    watch::WatchFolders reopened(dir.path()); QCOMPARE(reopened.rules()[0].id, id); QCOMPARE(read(path).value("items").toArray().size(), 3);
    QVERIFY(QDir().rmdir(input)); watch::WatchFolders missing(dir.path()); QCOMPARE(missing.rules().size(), 1);
    QCOMPARE(missing.rules()[0].id, id); QString canonical;
    QCOMPARE(watch::authorizeRoot(missing.rules()[0].path, dir.filePath("captures"), &canonical), QString("watch-directory-required"));
    auto disabled = missing.rules(); disabled[0].enabled = false;
    QVERIFY(missing.saveRules(disabled, dir.filePath("captures")).isEmpty());
    watch::WatchFolders disabledMissing(dir.path()); QVERIFY(!disabledMissing.rules()[0].enabled); QCOMPARE(disabledMissing.rules()[0].id, id);
    QVERIFY(QDir().mkpath(input)); QVERIFY(missing.saveRules(missing.rules(), dir.filePath("captures")).isEmpty());
    QVector<watch::Rule> full(watch::maximumWatchRules + 1);
    QCOMPARE(missing.saveRules(full, dir.filePath("captures")), QString("watch-store-full"));
  }
  void recentItemsKeepHealthyPathsAndDeterministicIdentity() {
    const QJsonObject input{{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{"/missing/new.png", 42, "/missing/old.png", "relative", "/missing/new.png"}}};
    const auto prefs = editor::loadEditorPreferences(input);
    QCOMPARE(prefs.recent.paths(), QStringList({"/missing/new.png", "/missing/old.png"}));
    QVERIFY(prefs.diagnostics.contains("invalid-RecentImageFiles"));
    const auto reopened = editor::loadEditorPreferences(editor::serializeEditorPreferences(prefs));
    QCOMPARE(reopened.recent.paths(), prefs.recent.paths());
    auto recent = reopened.recent; QVERIFY(recent.remove("/missing/new.png"));
    QCOMPARE(recent.paths(), QStringList{"/missing/old.png"});
  }
};
QTEST_GUILESS_MAIN(StoreCollectionRecoveryTest)
#include "StoreCollectionRecoveryTest.moc"
