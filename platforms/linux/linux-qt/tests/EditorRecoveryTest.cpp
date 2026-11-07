// EDITOR-SESSION-001 ES-018: separate editor recovery documents (store level).
#include "image-editor/EditorRecovery.h"
#include "image-editor/EditorSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

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
    for (int i = 0; i < EditorRecoveryStore::kMaxRecords + 3; ++i) {
      ids << EditorRecoveryStore::newId(); QCOMPARE(store.write(ids.last(), {}, document(), 1), QString());
      QFile::setPermissions(store.directory() + "/" + ids.last() + ".json", QFile::ReadOwner | QFile::WriteOwner);
      QTest::qWait(5);
    }
    const auto left = QDir(store.directory()).entryList({"*.json"}, QDir::Files);
    QVERIFY(left.size() <= EditorRecoveryStore::kMaxRecords); QVERIFY(left.contains(ids.last() + ".json"));
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
