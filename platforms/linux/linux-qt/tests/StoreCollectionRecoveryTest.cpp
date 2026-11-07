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
  // SETCAT-014: a malformed *object* item (not only a non-object) is isolated, order is preserved,
  // the corrupt bytes are kept for repair, and no diagnostic carries the item's values.
  void corruptObjectItemsAreSetAsideValueFreeAndNeighboursKeepOrder() {
    const QString secret = "SECRET-MARKER-9f3a";
    {
      QTemporaryDir dir; const auto path = dir.filePath("workflows.json");
      auto source = workflows::defaultCaptureWorkflows();
      auto b = source.entries.front(); b.id = QUuid::createUuid(); b.name = "B"; auto c = b; c.id = QUuid::createUuid(); c.name = "C";
      source.entries.append(b); source.entries.append(c);
      auto root = QJsonDocument::fromJson(workflows::workflowsJson(source)).object();
      auto entries = root.value("workflows").toArray();
      entries.insert(1, QJsonObject{{"id", QUuid::createUuid().toString()}, {"name", secret}, {"actions", 7}, {"folder", secret}});
      root.insert("workflows", entries); QVERIFY(write(path, root));
      editor::FilePreferenceStorage storage(path); workflows::CaptureWorkflowStore store(storage);
      QVERIFY(store.current().catalog); const auto cat = *store.current().catalog;
      QCOMPARE(cat.entries.size(), 3); QCOMPARE(cat.entries[1].id, b.id); QCOMPARE(cat.entries[2].id, c.id);
      QCOMPARE(cat.unreadable.size(), 1); QVERIFY(!store.current().diagnostic.contains(secret));
      auto draft = cat; draft.entries[2].name = "C2"; QVERIFY(store.save(draft, store.revision()).saved);
      QVERIFY(QJsonDocument(read(path)).toJson().contains(secret.toUtf8()));
    }
    {
      QTemporaryDir dir; const auto path = dir.filePath("destinations.json");
      const auto first = destination("First"), second = destination("Second");
      QJsonObject bad{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"name", secret}, {"requestUrl", 5}, {"body", secret}};
      QJsonObject root{{"format", "com.xerahs.app/linux-qt/upload-destinations"}, {"version", 1}, {"default", first.id.toString()},
          {"destinations", QJsonArray{QJsonDocument::fromJson(uploads::writeDestination(first)).object(), bad, QJsonDocument::fromJson(uploads::writeDestination(second)).object()}}};
      QVERIFY(write(path, root)); uploads::DestinationCatalog catalog(path);
      QCOMPARE(catalog.entries().size(), 2); QCOMPARE(catalog.unreadableCount(), 1);
      QCOMPARE(catalog.entries()[0].destination.id, first.id); QCOMPARE(catalog.entries()[1].destination.id, second.id);
      QVERIFY(catalog.setDefault(second.id).isEmpty());
      QVERIFY(QJsonDocument(read(path)).toJson().contains(secret.toUtf8()));
    }
    {
      QTemporaryDir dir; const auto a = dir.filePath("a"), c = dir.filePath("c"); QVERIFY(QDir().mkpath(a)); QVERIFY(QDir().mkpath(c));
      const auto path = dir.filePath("watch-folders.json");
      auto rule = [](const QString &p) { return QJsonObject{{"path", p}, {"filters", QJsonArray{"*.png"}}, {"enabled", true}, {"recursive", false}, {"action", 0}}; };
      QJsonObject bad{{"path", "/" + secret}, {"filters", 9}, {"enabled", secret}, {"action", secret}};
      QJsonObject root{{"format", "com.xerahs.app/watch-folders"}, {"version", 1}, {"items", QJsonArray{rule(a), bad, rule(c)}}};
      QVERIFY(write(path, root)); watch::WatchFolders store(dir.path());
      QCOMPARE(store.rules().size(), 2); QCOMPARE(store.rules()[0].path, a); QCOMPARE(store.rules()[1].path, c);
      QCOMPARE(store.diagnostics().size(), 1); QVERIFY(!store.diagnostics().join(' ').contains(secret));
      QVERIFY(store.saveRules(store.rules(), dir.filePath("captures")).isEmpty());
      QVERIFY(QJsonDocument(read(path)).toJson().contains(secret.toUtf8()));
    }
    {
      const QJsonObject input{{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{"/m/one.png", QJsonObject{{"p", secret}}, "/m/two.png"}}};
      const auto prefs = editor::loadEditorPreferences(input);
      QCOMPARE(prefs.recent.paths(), QStringList({"/m/one.png", "/m/two.png"})); QVERIFY(!prefs.diagnostics.join(' ').contains(secret));
    }
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
