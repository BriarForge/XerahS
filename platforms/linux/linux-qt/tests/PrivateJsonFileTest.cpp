// SETCAT-005, FN-016, CORE-012, CORE-013: private JSON refusal and atomic writes.
#include "storage/PrivateJsonFile.h"
#include "naming/CounterStore.h"
#include "settings/ApplicationSettings.h"
#include "uploads/DestinationCatalog.h"
#include "watch/WatchFolders.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <sys/stat.h>
#include <unistd.h>
using namespace xerahs::storage;
namespace {
StoreOptions options() { return {QStringLiteral("test.private-state"), 1, 1024}; }
bool put(const QString &path, const QByteArray &bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString &path) {
  QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
const QByteArray valid = R"({"format":"test.private-state","version":1,"value":7})";
struct RestorePermissions {
  QString path; QFile::Permissions permissions;
  ~RestorePermissions() { QFile::setPermissions(path, permissions); }
};
}
class PrivateJsonFileTest final : public QObject {
  Q_OBJECT
private slots:
  void missingAndAtomicPrivateRoundTrip() {
    QTemporaryDir dir; const auto path = dir.filePath("nested/state.json"); PrivateJsonFile store(path, options());
    QCOMPARE(store.path(), path); const auto missing = store.read(); QCOMPARE(missing.status, StoreStatus::Missing);
    QVERIFY(missing.document.isEmpty()); QVERIFY(missing.snapshot.isEmpty());
    QVERIFY(!QFileInfo::exists(dir.filePath("nested")));
    QByteArray snapshot;
    QCOMPARE(store.write({{"value", 7}, {"format", "ignored"}, {"version", 99}}, 1, missing.snapshot, &snapshot), StoreStatus::Ok);
    QCOMPARE(snapshot, get(path)); auto read = store.read();
    QCOMPARE(read.status, StoreStatus::Ok); QCOMPARE(read.version, 1);
    QCOMPARE(read.document.value("format").toString(), options().format); QCOMPARE(read.document.value("value").toInt(), 7);
    struct stat info {}; QCOMPARE(::stat(QFile::encodeName(path).constData(), &info), 0);
    QCOMPARE(info.st_mode & 0777, mode_t(0600)); QCOMPARE(info.st_uid, ::geteuid());
    QCOMPARE(store.write({{"value", 8}}, 1, snapshot, &snapshot), StoreStatus::Ok);
    QCOMPARE(store.read().document.value("value").toInt(), 8); QVERIFY(!QFileInfo::exists(path + ".lock"));
  }
  void refusesInvalidAndNewerWithoutRepair_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<StoreStatus>("status");
    QTest::newRow("bad-json") << QByteArray("{broken") << StoreStatus::Invalid;
    QTest::newRow("array") << QByteArray("[]") << StoreStatus::Invalid;
    QTest::newRow("wrong-format") << QByteArray(R"({"format":"wrong","version":1})") << StoreStatus::Invalid;
    QTest::newRow("missing-format") << QByteArray(R"({"version":1})") << StoreStatus::Invalid;
    QTest::newRow("null-format") << QByteArray(R"({"format":null,"version":1})") << StoreStatus::Invalid;
    QTest::newRow("missing-version") << QByteArray(R"({"format":"test.private-state"})") << StoreStatus::Invalid;
    QTest::newRow("fractional-version") << QByteArray(R"({"format":"test.private-state","version":1.5})") << StoreStatus::Invalid;
    QTest::newRow("newer") << QByteArray(R"({"format":"test.private-state","version":2})") << StoreStatus::NewerVersion;
    QTest::newRow("too-large") << QByteArray(1025, ' ') << StoreStatus::TooLarge;
  }
  void refusesInvalidAndNewerWithoutRepair() {
    QFETCH(QByteArray, bytes); QFETCH(StoreStatus, status); QTemporaryDir dir; const auto path = dir.filePath("state.json");
    QVERIFY(put(path, bytes)); PrivateJsonFile store(path, options()); const auto read = store.read(); QCOMPARE(read.status, status);
    QByteArray output("unchanged"); QCOMPARE(store.write({}, 1, read.snapshot, &output), status);
    QCOMPARE(output, QByteArray("unchanged")); QCOMPARE(get(path), bytes);
  }
  void legacyRequiresOptInAndGainsFormatOnWrite() {
    QTemporaryDir dir; const auto path = dir.filePath("state.json"); QVERIFY(put(path, R"({"version":1,"future":{"keep":true}})"));
    QCOMPARE(PrivateJsonFile(path, options()).read().status, StoreStatus::Invalid);
    auto legacy = options(); legacy.acceptLegacyWithoutFormat = true; PrivateJsonFile store(path, legacy); auto read = store.read();
    QCOMPARE(read.status, StoreStatus::Ok); QVERIFY(!read.document.contains("format")); QCOMPARE(read.snapshot, get(path));
    QCOMPARE(store.write(read.document, 1, read.snapshot, nullptr), StoreStatus::Ok); read = PrivateJsonFile(path, options()).read();
    QCOMPARE(read.status, StoreStatus::Ok); QCOMPARE(read.document.value("format").toString(), legacy.format);
    QVERIFY(read.document.value("future").toObject().value("keep").toBool());
    QVERIFY(put(path, R"({"format":"wrong","version":1})")); QCOMPARE(store.read().status, StoreStatus::Invalid);
  }
  void unsafePaths_data() {
    QTest::addColumn<QString>("kind");
    for (const auto &kind : {"file-link", "dangling-link", "directory", "fifo"}) QTest::newRow(kind) << QString(kind);
  }
  void unsafePaths() {
    QFETCH(QString, kind); QTemporaryDir dir; const auto target = dir.filePath("target.json"); QVERIFY(put(target, valid));
    auto path = dir.filePath("state.json");
    if (kind == "file-link") QVERIFY(QFile::link(target, path));
    else if (kind == "dangling-link") QVERIFY(QFile::link(dir.filePath("absent"), path));
    else if (kind == "directory") QVERIFY(QDir().mkdir(path));
    else QCOMPARE(::mkfifo(QFile::encodeName(path).constData(), 0600), 0);
    PrivateJsonFile store(path, options()); QCOMPARE(store.read().status, StoreStatus::Unsafe);
    QCOMPARE(store.write({}, 1, {}, nullptr), StoreStatus::Unsafe); QCOMPARE(get(target), valid);
  }
  void symlinkedUserOwnedDirectoriesAreAllowed() {
    // Fedora Atomic's /home -> /var/home and stowed ~/.config dirs are normal.
    QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("real/sub")));
    QVERIFY(QFile::link(dir.filePath("real"), dir.filePath("alias")));
    for (const auto &relative : {QStringLiteral("alias/state.json"), QStringLiteral("alias/sub/missing/state.json")}) {
      PrivateJsonFile store(dir.filePath(relative), options());
      QCOMPARE(store.read().status, StoreStatus::Missing);
      QByteArray snapshot;
      QCOMPARE(store.write({}, 1, {}, &snapshot), StoreStatus::Ok);
      QCOMPARE(store.read().status, StoreStatus::Ok);
    }
    QVERIFY(QFileInfo::exists(dir.filePath("real/sub/missing/state.json")));
  }
  void directoryOwnedByAnotherUserIsUnsafe() {
    if (::geteuid() == 0 || QFileInfo(QStringLiteral("/etc")).ownerId() == uint(::geteuid())) QSKIP("Needs a non-root user and a root-owned /etc.");
    PrivateJsonFile store(QStringLiteral("/etc/xerahs-private-json-test.json"), options());
    QCOMPARE(store.read().status, StoreStatus::Unsafe);
    QCOMPARE(store.write({}, 1, {}, nullptr), StoreStatus::Unsafe);
  }
  void otherOwnerIsUnsafe() {
    const QString path = QStringLiteral("/etc/passwd");
    if (!QFileInfo(path).isFile() || QFileInfo(path).ownerId() == uint(::geteuid()))
      QSKIP("No accessible regular file owned by another UID; chown requires privileges.");
    PrivateJsonFile store(path, options()); QCOMPARE(store.read().status, StoreStatus::Unsafe);
    QCOMPARE(store.write({}, 1, {}, nullptr), StoreStatus::Unsafe);
  }
  void unreadableFileAndReadOnlyDirectory() {
    QTemporaryDir dir; const auto path = dir.filePath("state.json"); QVERIFY(put(path, valid));
    RestorePermissions restore{path, QFile::permissions(path)}; QVERIFY(QFile::setPermissions(path, QFile::WriteOwner));
    PrivateJsonFile store(path, options()); QCOMPARE(store.read().status, StoreStatus::ReadFailed);
    QCOMPARE(store.write({}, 1, valid, nullptr), StoreStatus::ReadFailed);
    QVERIFY(QFile::setPermissions(path, restore.permissions));
    RestorePermissions restoreDir{dir.path(), QFile::permissions(dir.path())};
    QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner));
    QCOMPARE(store.write({}, 1, valid, nullptr), StoreStatus::WriteFailed); QCOMPARE(get(path), valid);
  }
  void heldLockWriteFailurePreservesSnapshot() {
    QTemporaryDir dir; const auto path = dir.filePath("state.json"); QVERIFY(put(path, valid));
    PrivateJsonFile store(path, options()); StoreLock lock; QCOMPARE(store.lock(lock), StoreStatus::Ok);
    RestorePermissions restore{dir.path(), QFile::permissions(dir.path())};
    QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner));
    QByteArray snapshot("unchanged");
    QCOMPARE(store.write({{"value", 8}}, 1, valid, &snapshot, &lock), StoreStatus::WriteFailed);
    QCOMPARE(snapshot, QByteArray("unchanged")); QCOMPARE(get(path), valid); QVERIFY(lock.held());
  }
  void heldLockWriteAndRelease() {
    QTemporaryDir dir; const auto path = dir.filePath("state.json"); PrivateJsonFile first(path, options()), second(path, options());
    {
      StoreLock lock; QVERIFY(!lock.held()); QCOMPARE(first.lock(lock), StoreStatus::Ok); QVERIFY(lock.held());
      const auto read = first.read(); QCOMPARE(read.status, StoreStatus::Missing);
      StoreLock competitor; QCOMPARE(second.lock(competitor), StoreStatus::Busy); QVERIFY(!competitor.held());
      QCOMPARE(second.write({}, 1, {}, nullptr), StoreStatus::Busy); QByteArray snapshot;
      QCOMPARE(first.write({{"value", 4}}, 1, read.snapshot, &snapshot, &lock), StoreStatus::Ok);
      QVERIFY(lock.held()); QCOMPARE(snapshot, get(path)); QCOMPARE(second.write({}, 1, snapshot, nullptr), StoreStatus::Busy);
    }
    StoreLock next; QCOMPARE(second.lock(next), StoreStatus::Ok);
    QCOMPARE(second.write({}, 1, second.read().snapshot, nullptr, &next), StoreStatus::Ok);
  }
  void changedSnapshotAndOversizedWriteLeaveOriginal() {
    QTemporaryDir dir; const auto path = dir.filePath("state.json"); PrivateJsonFile first(path, options()), second(path, options());
    const auto missing = second.read(); QByteArray snapshot;
    QCOMPARE(first.write({{"value", 1}}, 1, {}, &snapshot), StoreStatus::Ok);
    QCOMPARE(second.write({{"value", 2}}, 1, missing.snapshot, nullptr), StoreStatus::Changed);
    QCOMPARE(first.write({{"value", QString(2048, 'x')}}, 1, snapshot, nullptr), StoreStatus::TooLarge); QCOMPARE(get(path), snapshot);
    QVERIFY(put(path, valid)); QCOMPARE(first.write({}, 1, snapshot, nullptr), StoreStatus::Changed); QCOMPARE(get(path), valid);
    QVERIFY(QFile::remove(path)); QCOMPARE(first.write({}, 1, snapshot, nullptr), StoreStatus::Changed); QVERIFY(!QFile::exists(path));
  }
  void migratedStoresKeepLegacyAndConflictDiagnostics() {
    using namespace xerahs;
    QTemporaryDir dir; const auto catalogPath = dir.filePath("destinations.json");
    QVERIFY(put(catalogPath, R"({"version":1,"destinations":[],"note":"keep"})")); uploads::DestinationCatalog catalog(catalogPath);
    QVERIFY(catalog.writable()); uploads::UploadDestination destination;
    destination.id = QUuid::createUuid(); destination.name = "test"; destination.requestUrl = "https://example.com/";
    QCOMPARE(catalog.add(destination), QString()); const auto savedCatalog = QJsonDocument::fromJson(get(catalogPath)).object();
    QVERIFY(savedCatalog.contains("format")); QCOMPARE(savedCatalog.value("note").toString(), QString("keep"));
    const auto settingsPath = dir.filePath("settings.json");
    QVERIFY(put(settingsPath, QJsonDocument(QJsonObject{{"version", 1}, {"values", settings::defaultSettings(dir.path())}, {"note", "keep"}}).toJson()));
    settings::ApplicationSettingsStore settings(settingsPath, dir.path());
    QVERIFY(settings.current().writable); QCOMPARE(settings.save(settings.current().values), QString());
    const auto savedSettings = QJsonDocument::fromJson(get(settingsPath)).object();
    QVERIFY(savedSettings.contains("format")); QCOMPARE(savedSettings.value("note").toString(), QString("keep"));
    watch::WatchFolders first(dir.path()), stale(dir.path()); QCOMPARE(first.remember("one"), QString());
    QCOMPARE(stale.remember("two"), QString("watch-store-write-failed"));
    watch::WatchFolders reopened(dir.path()); QVERIFY(reopened.processed("one")); QVERIFY(!reopened.processed("two"));
    const auto counterPath = dir.filePath("counter.json"); naming::CounterStore counter(counterPath, 0);
    auto reservation = counter.reserve("p"); QCOMPARE(reservation.error, naming::CounterError::None);
    QCOMPARE(counter.reserve("p").error, naming::CounterError::Busy);
    QCOMPARE(counter.commit(reservation.reservation, 1), naming::CounterError::None); QCOMPARE(counter.peek("p").value, qint64(1));
  }
  void legacyInvalidVersionDiagnosticsStayStable_data() {
    QTest::addColumn<QByteArray>("version");
    for (const auto &v : {"", "0", "2", "1.5", "null", "\"1\""}) QTest::newRow(v[0] ? v : "missing") << QByteArray(v);
  }
  void legacyInvalidVersionDiagnosticsStayStable() {
    using namespace xerahs;
    QFETCH(QByteArray, version); QTemporaryDir dir;
    const auto catalog = dir.filePath("catalog.json"), settings = dir.filePath("settings.json");
    const auto key = version.isEmpty() ? QByteArray{} : QByteArray("\"version\":") + version + ',';
    QVERIFY(put(catalog, "{" + key + "\"destinations\":[]}"));
    QCOMPARE(uploads::DestinationCatalog(catalog).loadCode(), QString("catalog-version-unsupported"));
    QVERIFY(put(settings, "{" + key + "\"values\":{}}"));
    const settings::ApplicationSettingsStore store(settings, dir.path());
    QCOMPARE(store.current().diagnostics, QStringList{"settings-version-unsupported"}); QVERIFY(!store.current().writable);
  }
  void stableStatusNames() {
    const QStringList expected{"ok", "missing", "unsafe", "too-large", "invalid", "newer-version", "busy", "changed", "read-failed", "write-failed"};
    for (int i = 0; i < expected.size(); ++i) QCOMPARE(storeStatusName(static_cast<StoreStatus>(i)), expected.at(i));
  }
};
QTEST_GUILESS_MAIN(PrivateJsonFileTest)
#include "PrivateJsonFileTest.moc"
