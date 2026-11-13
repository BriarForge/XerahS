// EDITOR-SESSION-001 ES-018: separate editor recovery documents (store level).
#include "image-editor/EditorRecovery.h"
#include "image-editor/EditorSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include "storage/PrivateJsonFile.h"

#include <sys/stat.h>

using namespace xerahs::editor;

namespace {
const QByteArray kOnePixelPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");

AnnotationDocument document(int rectangles = 1) {
  AnnotationDocument d;
  d.canvasWidth = 10; d.canvasHeight = 10;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-10-08T00:00:00Z");
  d.sourceImagePng = kOnePixelPng;
  for (int i = 0; i < rectangles; ++i) {
    RectangleAnnotation a; a.id = QUuid::createUuid(); a.start = {1.0 + i, 1}; a.end = {5.0 + i, 5}; d.annotations.push_back(a);
  }
  return d;
}
QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
qint64 exitedOwner() {
  QProcess child; child.start(QStringLiteral("/bin/true"));
  if (!child.waitForStarted()) return 0;
  const auto pid = child.processId();
  return child.waitForFinished() ? pid : 0;
}
}  // namespace

class EditorRecoveryTest final : public QObject {
  Q_OBJECT
private slots:
  void writesPrivateSeparateRoundTripWithSourceAndTime() {
    QTemporaryDir dir; QDateTime now(QDate(2026, 11, 7), QTime(12, 0), Qt::UTC);
    EditorRecoveryStore store(dir.filePath("state/recovery"), [&] { return now; });
    const auto id = EditorRecoveryStore::newId(); const auto doc = document(2);
    QCOMPARE(store.write(id, "/home/u/shot.png", doc, 4242), QString());
    const QString file = store.directory() + "/" + id + ".json";
    QVERIFY(QFileInfo(file).isFile()); QCOMPARE(QFileInfo(file).permissions() & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther), QFileDevice::Permissions());
    const auto listing = store.list(); QCOMPARE(listing.records.size(), size_t(1)); QCOMPARE(listing.setAside, 0);
    const auto &r = listing.records.at(0);
    QCOMPARE(r.id, id); QCOMPARE(r.sourcePath, QString("/home/u/shot.png")); QCOMPARE(r.ownerPid, qint64(4242)); QCOMPARE(r.savedAt, now);
    QCOMPARE(serializeDocument(r.document).value("annotations"), serializeDocument(doc).value("annotations"));
    QCOMPARE(r.document.sourceImagePng, doc.sourceImagePng);
    // Rewriting replaces the same file without a stale-snapshot refusal.
    QCOMPARE(store.write(id, "/home/u/shot.png", document(3), 4242), QString());
    QCOMPARE(store.list().records.at(0).document.annotations.size(), size_t(3)); QCOMPARE(store.list().records.size(), size_t(1));
    QCOMPARE(store.remove(id), QString()); QVERIFY(!QFileInfo::exists(file)); QVERIFY(store.list().records.empty());
    QCOMPARE(store.remove(id), QString());  // removing twice is harmless
  }
  void externallyRemovedRecoveryIsRecreatedByItsWriter() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    const auto id = EditorRecoveryStore::newId(), path = store.directory() + '/' + id + ".json";
    QCOMPARE(store.write(id, {}, document(), QCoreApplication::applicationPid()), QString());
    QVERIFY(QFile::remove(path));
    const auto latest = document(3);
    QCOMPARE(store.write(id, {}, latest, QCoreApplication::applicationPid()), QString());
    QVERIFY(QFileInfo::exists(path));
    const auto listing = store.list(); QCOMPARE(listing.records.size(), size_t(1));
    QCOMPARE(listing.records[0].id, id);
    QCOMPARE(serializeDocument(listing.records[0].document).value("annotations"), serializeDocument(latest).value("annotations"));
    QCOMPARE(store.write(id, {}, document(4), QCoreApplication::applicationPid()), QString());
    QCOMPARE(store.list().records[0].document.annotations.size(), size_t(4));
    QCOMPARE(store.remove(id), QString()); QVERIFY(!QFileInfo::exists(path));
  }
  void recreationAtProtectedCapacityRefusesAndKeepsEveryRecord() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    const auto id = EditorRecoveryStore::newId(), path = store.directory() + '/' + id + ".json";
    QCOMPARE(store.write(id, {}, document(), QCoreApplication::applicationPid()), QString());
    for (int i = 0; i < EditorRecoveryStore::kMaxRecords - 1; ++i)
      QCOMPARE(store.write(EditorRecoveryStore::newId(), {}, document(), QCoreApplication::applicationPid()), QString());
    QVERIFY(QFile::remove(path));
    QCOMPARE(store.write(EditorRecoveryStore::newId(), {}, document(), QCoreApplication::applicationPid()), QString());
    QMap<QString, QByteArray> before;
    for (const auto &name : QDir(store.directory()).entryList({"*.json"}, QDir::Files)) {
      const auto file = QDir(store.directory()).filePath(name); before.insert(file, bytes(file));
    }
    QCOMPARE(before.size(), EditorRecoveryStore::kMaxRecords);
    QCOMPARE(store.write(id, {}, document(3), QCoreApplication::applicationPid()), QString("recovery-storage-full"));
    QVERIFY(!QFileInfo::exists(path));
    for (auto it = before.begin(); it != before.end(); ++it) QCOMPARE(bytes(it.key()), it.value());
    // A refused recreation must retain the old snapshot, preventing later takeover.
    auto replacement = QJsonDocument::fromJson(before.first()).object(); replacement.insert("sourcePath", "replacement.png");
    const auto changed = QJsonDocument(replacement).toJson();
    { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(changed), changed.size());
      QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)); }
    QVERIFY(!store.write(id, {}, document(4), QCoreApplication::applicationPid()).isEmpty());
    QCOMPARE(bytes(path), changed);
    for (auto it = before.begin(); it != before.end(); ++it) QCOMPARE(bytes(it.key()), it.value());
  }
  void recreationNeverAdoptsAnInterveningRecord_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"changed", "newer", "corrupt", "symlink"}) QTest::newRow(mode) << QString(mode);
  }
  void recreationNeverAdoptsAnInterveningRecord() {
    QFETCH(QString, mode);
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    const auto id = EditorRecoveryStore::newId(), path = store.directory() + '/' + id + ".json";
    QCOMPARE(store.write(id, {}, document(), QCoreApplication::applicationPid()), QString());
    auto replacement = QJsonDocument::fromJson(bytes(path)).object(); replacement.insert("sourcePath", "replacement.png");
    if (mode == "newer") replacement.insert("version", 2);
    const auto changed = mode == "corrupt" ? QByteArray("corrupt-record") : QJsonDocument(replacement).toJson();
    QVERIFY(QFile::remove(path));
    const auto victim = dir.filePath("private-record.json");
    const auto destination = mode == "symlink" ? victim : path;
    { QFile file(destination); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(changed), changed.size());
      QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)); }
    if (mode == "symlink") QVERIFY(QFile::link(victim, path));
    const auto link = QFileInfo(path).symLinkTarget();
    QVERIFY(!store.write(id, {}, document(3), QCoreApplication::applicationPid()).isEmpty());
    QCOMPARE(bytes(path), changed); QCOMPARE(QFileInfo(path).symLinkTarget(), link);
    QCOMPARE(QFileInfo(path).isSymLink(), mode == "symlink");
    if (mode == "symlink") QCOMPARE(bytes(victim), changed);
  }
  void neverTouchesTheUsersSaveTargetOrSidecar() {
    QTemporaryDir dir; const auto png = dir.filePath("save.png"), xann = png + ".xann";
    QVERIFY(QFile(png).open(QIODevice::WriteOnly)); { QFile f(xann); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("explicit"); }
    { QFile f(png); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("explicit-png"); }
    const auto pngBefore = bytes(png), xannBefore = bytes(xann);
    EditorRecoveryStore store(dir.filePath("recovery"));
    QCOMPARE(store.write(EditorRecoveryStore::newId(), png, document(), 1), QString());
    QCOMPARE(bytes(png), pngBefore); QCOMPARE(bytes(xann), xannBefore);
    QVERIFY(!store.directory().isEmpty() && store.directory() != QFileInfo(png).absolutePath());
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 2);
  }
  void rejectsInvalidIdsAndKeepsTraversalOut() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    for (const auto &bad : {QString(), QString("../x"), QString("a/b"), QString("ABC"), QString("{00000000-0000-0000-0000-000000000000}")}) {
      QCOMPARE(store.write(bad, {}, document(), 1), QString("recovery-id-invalid")); QCOMPARE(store.remove(bad), QString("recovery-id-invalid"));
    }
    QVERIFY(!QFileInfo::exists(dir.filePath("r")));
  }
  void corruptRecoveryIsSetAsideNotFatal() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    const auto good = EditorRecoveryStore::newId(); QCOMPARE(store.write(good, {}, document(), 1), QString());
    for (const auto &content : {QByteArray("not json at all"), QByteArray(R"({"format":"xerahs.editor-recovery","version":1,"savedAt":"x","document":{}})"),
                                QByteArray(R"({"format":"other","version":1})")}) {
      const auto bad = EditorRecoveryStore::newId(); QFile f(store.directory() + "/" + bad + ".json"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(content); f.close();
      const auto listing = store.list();
      QCOMPARE(listing.records.size(), size_t(1)); QCOMPARE(listing.records.at(0).id, good); QCOMPARE(listing.setAside, 1);
      QVERIFY(!QFileInfo::exists(f.fileName())); QVERIFY(QFileInfo::exists(f.fileName() + ".corrupt"));
      QVERIFY(listing.diagnostics.join(' ') == "recovery-corrupt");  // value-free
    }
    QVERIFY(QDir(store.directory()).entryList({"*.corrupt"}, QDir::Files).size() <= EditorRecoveryStore::kMaxSetAside);
    QCOMPARE(store.list().setAside, 0);  // set-aside files are not re-reported
  }
  void countIsBoundedOldestPruned() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r")); QStringList ids;
    const auto owner = exitedOwner(); QVERIFY(owner > 0);
    for (int i = 0; i < EditorRecoveryStore::kMaxRecords + 3; ++i) {
      ids << EditorRecoveryStore::newId(); QCOMPARE(store.write(ids.last(), {}, document(), owner), QString());
      QFile::setPermissions(store.directory() + "/" + ids.last() + ".json", QFile::ReadOwner | QFile::WriteOwner);
      QTest::qWait(5);
    }
    const auto left = QDir(store.directory()).entryList({"*.json"}, QDir::Files);
    QVERIFY(left.size() <= EditorRecoveryStore::kMaxRecords); QVERIFY(left.contains(ids.last() + ".json"));
  }
  void countPruningPreservesProtectedRecords_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("newer-version") << QString("newer");
    QTest::newRow("live-owner") << QString("live");
    QTest::newRow("foreign-format") << QString("foreign");
    QTest::newRow("corrupt-record") << QString("corrupt");
  }
  void countPruningPreservesProtectedRecords() {
    QFETCH(QString, kind);
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    const auto owner = exitedOwner(); QVERIFY(owner > 0);
    const auto id = EditorRecoveryStore::newId(), path = store.directory() + '/' + id + ".json";
    QCOMPARE(store.write(id, {}, document(), kind == "live" ? QCoreApplication::applicationPid() : owner), QString());
    auto payload = QJsonDocument::fromJson(bytes(path)).object();
    if (kind == "newer") payload.insert("version", 2);
    if (kind == "foreign") payload.insert("format", "another.application");
    { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));
      const auto content = kind == "corrupt" ? QByteArray("damaged recovery record") : QJsonDocument(payload).toJson();
      QCOMPARE(f.write(content), content.size());
      QVERIFY(f.flush());
      QVERIFY(f.setFileTime(QDateTime(QDate(2000, 1, 1), QTime(), Qt::UTC), QFileDevice::FileModificationTime)); }
    const auto protectedBytes = bytes(path);
    for (int i = 0; i < EditorRecoveryStore::kMaxRecords; ++i)
      QCOMPARE(store.write(EditorRecoveryStore::newId(), {}, document(), owner), QString());
    QCOMPARE(bytes(path), protectedBytes);
    QVERIFY(QDir(store.directory()).entryList({"*.json"}, QDir::Files).size() <= EditorRecoveryStore::kMaxRecords);
  }
  void fullLiveCollectionRefusesNewRecoveryWithoutEvictingWork() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r")); QMap<QString, QByteArray> original;
    for (int i = 0; i < EditorRecoveryStore::kMaxRecords; ++i) {
      const auto id = EditorRecoveryStore::newId();
      QCOMPARE(store.write(id, {}, document(), QCoreApplication::applicationPid()), QString());
      original.insert(id, bytes(store.directory() + '/' + id + ".json"));
    }
    const auto extra = EditorRecoveryStore::newId();
    QVERIFY(!store.write(extra, {}, document(), QCoreApplication::applicationPid()).isEmpty());
    QVERIFY(!QFile::exists(store.directory() + '/' + extra + ".json"));
    for (auto it = original.cbegin(); it != original.cend(); ++it)
      QCOMPARE(bytes(store.directory() + '/' + it.key() + ".json"), it.value());
    QCOMPARE(store.write(original.firstKey(), {}, document(3), QCoreApplication::applicationPid()), QString());
    QCOMPARE(store.list().records.size(), size_t(EditorRecoveryStore::kMaxRecords));
    auto foreign = QJsonDocument::fromJson(original.first()).object(); foreign.insert("version", 2);
    const auto foreignPath = store.directory() + '/' + extra + ".json";
    { QFile file(foreignPath); QVERIFY(file.open(QIODevice::WriteOnly)); const auto contents = QJsonDocument(foreign).toJson(); QCOMPARE(file.write(contents), contents.size()); }
    const auto foreignBytes = bytes(foreignPath);
    QCOMPARE(store.write(original.firstKey(), {}, document(4), QCoreApplication::applicationPid()), QString());
    QCOMPARE(bytes(foreignPath), foreignBytes);
  }
  void failedPublicationDoesNotPruneExistingRecovery() {
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r")); QMap<QString, QByteArray> original;
    const auto owner = exitedOwner(); QVERIFY(owner > 0);
    for (int i = 0; i < EditorRecoveryStore::kMaxRecords; ++i) {
      const auto id = EditorRecoveryStore::newId(); QCOMPARE(store.write(id, {}, document(), owner), QString());
      original.insert(id, bytes(store.directory() + '/' + id + ".json"));
    }
    const auto extra = EditorRecoveryStore::newId();
    xerahs::storage::StoreOptions options; options.format = "xerahs.editor-recovery";
    xerahs::storage::PrivateJsonFile target(store.directory() + '/' + extra + ".json", options);
    xerahs::storage::StoreLock held; QCOMPARE(target.lock(held), xerahs::storage::StoreStatus::Ok);
    QVERIFY(!store.write(extra, {}, document(), owner).isEmpty());
    for (auto it = original.cbegin(); it != original.cend(); ++it)
      QCOMPARE(bytes(store.directory() + '/' + it.key() + ".json"), it.value());
  }
  void removalPreservesNewerOrChangedRecords_data() {
    QTest::addColumn<bool>("newer");
    QTest::newRow("newer-version") << true;
    QTest::newRow("changed-supported") << false;
  }
  void removalPreservesNewerOrChangedRecords() {
    QFETCH(bool, newer);
    QTemporaryDir dir; EditorRecoveryStore store(dir.filePath("r"));
    const auto id = EditorRecoveryStore::newId(), path = store.directory() + '/' + id + ".json";
    QCOMPARE(store.write(id, {}, document(), QCoreApplication::applicationPid()), QString());
    auto payload = QJsonDocument::fromJson(bytes(path)).object();
    if (newer) payload.insert("version", 2); else payload.insert("sourcePath", "changed.png");
    { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); const auto changed = QJsonDocument(payload).toJson(); QCOMPARE(file.write(changed), changed.size()); }
    const auto changed = bytes(path);
    QVERIFY(!store.remove(id).isEmpty());
    QCOMPARE(bytes(path), changed);
    QVERIFY(!store.write(id, {}, document(3), QCoreApplication::applicationPid()).isEmpty());
    QCOMPARE(bytes(path), changed);
  }
  void ageAndSourceDisclosure() {
    QCOMPARE(describeRecoveryAge(5), QString("less than a minute ago")); QCOMPARE(describeRecoveryAge(60), QString("1 minute ago"));
    QCOMPARE(describeRecoveryAge(7200), QString("2 hours ago")); QCOMPARE(describeRecoveryAge(86400 * 3), QString("3 days ago"));
    QVERIFY(describeRecoveryAge(-1).contains("clock"));
    RecoveryRecord file; file.sourcePath = "/home/u/a.png"; file.savedAt = QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC);
    RecoveryRecord capture; capture.savedAt = file.savedAt;
    const auto now = file.savedAt.addSecs(125);
    const auto a = recoveryDisclosure(file, now), b = recoveryDisclosure(capture, now);
    QVERIFY(a.contains("2 minutes ago") && a.contains("/home/u/a.png") && a.contains("explicit save was not changed"));
    QVERIFY(b.contains("2 minutes ago") && b.contains("unsaved capture") && !b.contains("/home"));
  }
  void defaultDirectoryFollowsXdgState() {
    qputenv("XDG_STATE_HOME", "/tmp/xs-state-test");
    QCOMPARE(EditorRecoveryStore::defaultDirectory(), QString("/tmp/xs-state-test/com.xerahs.app/editor-recovery"));
    qunsetenv("XDG_STATE_HOME");
  }
};

QTEST_GUILESS_MAIN(EditorRecoveryTest)
#include "EditorRecoveryTest.moc"
