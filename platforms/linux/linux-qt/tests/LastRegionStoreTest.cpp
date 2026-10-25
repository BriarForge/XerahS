#include "capture/LastRegionStore.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace xerahs::capture;
using xerahs::storage::StoreStatus;
namespace {
const QByteArray fixture = R"({"version":1,"rectangle":{"left":-20,"top":10,"right":40,"bottom":50,"future-rectangle":true},"displays":[{"id":"left","physical":{"left":-100,"top":0,"right":0,"bottom":100},"future-display":"kept"},{"id":"right","physical":{"left":0,"top":0,"right":100,"bottom":100}}],"future-root":{"keep":1}})";
void put(const QString &path, const QByteArray &bytes) { QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("fixture write failed"); }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
LastRegion region() { return {1, {-10, 20, 60, 70}, {{"left", {-100, 0, 0, 100}}, {"right", {0, 0, 100, 100}}}}; }
}
class LastRegionStoreTest : public QObject {
  Q_OBJECT
private slots:
  void versionOneRoundTripKeepsUnknownFieldsAndExactNegativeCoordinates() {
    QTemporaryDir dir; const auto path = dir.filePath("region.json"); put(path, fixture);
    LastRegionStore store(path); const auto read = store.read(); QCOMPARE(read.file.status, StoreStatus::Ok); QVERIFY(read.region);
    QCOMPARE(read.region->rectangle, (PhysicalRect{-20, 10, 40, 50})); QCOMPARE(read.region->displays.size(), size_t(2));
    QCOMPARE(store.save(region(), read), StoreStatus::Ok);
    const auto reopened = LastRegionStore(path).read(); QVERIFY(reopened.region); QCOMPARE(reopened.region->version, 1);
    QCOMPARE(reopened.region->rectangle, (PhysicalRect{-10, 20, 60, 70}));
    const auto doc = QJsonDocument::fromJson(bytes(path)).object();
    QCOMPARE(doc.value("version"), QJsonValue(1)); QCOMPARE(doc.value("format"), QJsonValue("com.xerahs.app/linux-qt/last-region"));
    QCOMPARE(doc.value("future-root").toObject().value("keep"), QJsonValue(1));
    QCOMPARE(doc.value("rectangle").toObject().value("future-rectangle"), QJsonValue(true));
    QCOMPARE(doc.value("displays").toArray()[0].toObject().value("future-display"), QJsonValue("kept"));
    QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
    const Topology same{{"left", {-100, 0, 0, 100}, {}, 1}, {"right", {0, 0, 100, 100}, {}, 1}};
    QVERIFY(validateLastRegion(*reopened.region, same)); auto moved = same; moved[1].physical.right = 200; QVERIFY(!validateLastRegion(*reopened.region, moved));
    auto missing = same; missing.pop_back(); QVERIFY(!validateLastRegion(*reopened.region, missing));
  }
  void missingAndStaleSnapshotsNeverLoseAnotherWrite() {
    QTemporaryDir dir; const auto path = dir.filePath("nested/region.json"); LastRegionStore store(path);
    const auto missing = store.read(); QCOMPARE(missing.file.status, StoreStatus::Missing);
    QCOMPARE(store.save(region(), missing), StoreStatus::Ok); const auto original = bytes(path);
    QCOMPARE(store.save(region(), missing), StoreStatus::Changed); QCOMPARE(bytes(path), original);
    const auto read = store.read(); auto updated = region(); updated.rectangle.top = 30;
    QCOMPARE(store.save(updated, read), StoreStatus::Ok); const auto changed = bytes(path);
    QCOMPARE(store.save(region(), read), StoreStatus::Changed); QCOMPARE(bytes(path), changed);
    xerahs::storage::PrivateJsonFile file(path, {"com.xerahs.app/linux-qt/last-region", 1});
    xerahs::storage::StoreLock held; QCOMPARE(file.lock(held), StoreStatus::Ok);
    QCOMPARE(store.save(region(), store.read()), StoreStatus::Busy); QCOMPARE(bytes(path), changed);
  }
  void malformedAndNewerRecordsAreReadOnly() {
    QTemporaryDir dir; const auto path = dir.filePath("region.json");
    QList<QByteArray> invalid{"{", R"({"version":1})"};
    auto object = QJsonDocument::fromJson(fixture).object();
    for (const auto key : {"left", "top", "right", "bottom"}) {
      auto bad = object; auto rect = bad.value("rectangle").toObject(); rect.insert(key, "20"); bad.insert("rectangle", rect); invalid.append(QJsonDocument(bad).toJson());
    }
    for (const auto value : {QJsonValue(10.5), QJsonValue(9007199254740992.0), QJsonValue(QJsonValue::Null), QJsonValue(true)}) {
      auto bad = object; auto rect = bad.value("rectangle").toObject(); rect.insert("left", value); bad.insert("rectangle", rect); invalid.append(QJsonDocument(bad).toJson());
    }
    auto reversed = object; auto rect = reversed.value("rectangle").toObject(); rect.insert("right", -20); reversed.insert("rectangle", rect); invalid.append(QJsonDocument(reversed).toJson());
    auto wrongFormat = object; wrongFormat.insert("format", "unrelated-format"); invalid.append(QJsonDocument(wrongFormat).toJson());
    auto duplicate = object; auto displays = duplicate.value("displays").toArray(); displays.append(displays[0]); duplicate.insert("displays", displays); invalid.append(QJsonDocument(duplicate).toJson());
    auto noDisplay = object; noDisplay.insert("displays", QJsonArray{}); invalid.append(QJsonDocument(noDisplay).toJson());
    for (const auto &raw : invalid) {
      put(path, raw); LastRegionStore store(path); const auto read = store.read(); QCOMPARE(read.file.status, StoreStatus::Invalid); QVERIFY(!read.region);
      QCOMPARE(store.save(region(), read), StoreStatus::Invalid); QCOMPARE(bytes(path), raw);
    }
    object.insert("version", 2); const auto newer = QJsonDocument(object).toJson(); put(path, newer); LastRegionStore store(path);
    const auto read = store.read(); QCOMPARE(read.file.status, StoreStatus::NewerVersion); QVERIFY(!read.region);
    QCOMPARE(store.save(region(), read), StoreStatus::NewerVersion); QCOMPARE(bytes(path), newer);
  }
  void invalidNewGeometryAndUnsafePathsDoNotPublish() {
    QTemporaryDir dir; const auto path = dir.filePath("region.json"); LastRegionStore store(path); const auto missing = store.read();
    auto bad = region(); bad.rectangle.right = bad.rectangle.left; QCOMPARE(store.save(bad, missing), StoreStatus::Invalid);
    bad = region(); bad.displays[0].id = ""; QCOMPARE(store.save(bad, missing), StoreStatus::Invalid);
    bad = region(); bad.displays[0].physical.right = -30; QCOMPARE(store.save(bad, missing), StoreStatus::Invalid);
    bad = region(); bad.version = 2; QCOMPARE(store.save(bad, missing), StoreStatus::Invalid); QVERIFY(!QFile::exists(path));
    const auto target = dir.filePath("target.json"); put(target, fixture); QVERIFY(QFile::link(target, path));
    const auto unsafe = store.read(); QCOMPARE(unsafe.file.status, StoreStatus::Unsafe); QCOMPARE(store.save(region(), unsafe), StoreStatus::Unsafe); QCOMPARE(bytes(target), fixture);
    QVERIFY(lastRegionStorageDiagnostic(StoreStatus::Ok).isEmpty());
    QVERIFY(lastRegionStorageDiagnostic(StoreStatus::Changed).startsWith("last-region-storage-changed: "));
    QVERIFY(!lastRegionStorageDiagnostic(StoreStatus::Invalid).contains("left"));
  }
};
QTEST_GUILESS_MAIN(LastRegionStoreTest)
#include "LastRegionStoreTest.moc"
