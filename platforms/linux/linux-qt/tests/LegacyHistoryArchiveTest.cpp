#include "history/LegacyHistoryArchive.h"
#include "storage/PrivateJsonFile.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs;
namespace {
QJsonObject record(const QString &name) { return {{"FileName", name}, {"FilePath", "/missing/" + name}, {"DateTime", "2026-08-30T12:34:56+10:00"}, {"URL", "https://example.invalid/private"}}; }
QByteArray source(const QJsonArray &records) { const auto bytes = QJsonDocument(records).toJson(QJsonDocument::Compact); return bytes.mid(1, bytes.size() - 2); }
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class LegacyHistoryArchiveTest final : public QObject {
  Q_OBJECT
private slots:
  void explicitImportSurvivesRestartAndRetainsExactSourceAndQuarantine() {
    QTemporaryDir dir; const auto path = dir.filePath("history.json");
    const auto bytes = source({record("first.png"), 17, record("second.png")});
    history::LegacyHistoryArchive store(path);
    QCOMPARE(store.importSnapshot(bytes, {}, false), QString("cancelled")); QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(store.importSnapshot(bytes, {}, true), QString());
    const auto reopened = history::LegacyHistoryArchive(path).read(); QVERIFY(reopened.code.isEmpty());
    QCOMPARE(reopened.preview.records.size(), size_t(2)); QCOMPARE(reopened.preview.quarantine.size(), 1);
    QCOMPARE(reopened.preview.records[0].fields, record("first.png")); QCOMPARE(reopened.preview.records[1].fields, record("second.png"));
    const auto envelope = QJsonDocument::fromJson(read(path)).object();
    QCOMPARE(envelope.value("format").toString(), QString("com.xerahs.app/linux-qt/legacy-history"));
    const auto saved = envelope.value("imports").toArray().first().toObject();
    QCOMPARE(saved.value("sourceFormat").toString(), QString("legacy-json-fragments"));
    QCOMPARE(saved.value("sourceVersion").toString(), QString("unversioned"));
    QCOMPARE(QByteArray::fromBase64(saved.value("source").toString().toLatin1()), bytes);
    QVERIFY(!(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
  }
  void exactRetriesAndDistinctSnapshotsKeepEveryRecordAndUndoPreservesOthers() {
    QTemporaryDir dir; const auto path = dir.filePath("history.json"); history::LegacyHistoryArchive store(path);
    const auto first = source({record("repeat.png"), record("repeat.png"), record("other.png")});
    QCOMPARE(store.importSnapshot(first, {}, true), QString()); auto state = store.read(); const auto firstId = state.lastImportId;
    QCOMPARE(state.preview.records.size(), size_t(3)); const auto snapshot = state.snapshot;
    QCOMPARE(store.importSnapshot(first, snapshot, true), QString()); QCOMPARE(store.read().snapshot, snapshot);
    const auto second = source({record("repeat.png"), record("repeat.png"), record("other.png"), record("new.png")});
    QCOMPARE(store.importSnapshot(second, state.snapshot, true), QString()); state = store.read();
    QCOMPARE(state.preview.records.size(), size_t(7));
    QCOMPARE(state.preview.records.front().archiveImportId, firstId);
    QVERIFY(state.preview.records.back().archiveImportId != firstId);
    QCOMPARE(store.undo(state.lastImportId, state.snapshot, false), QString("cancelled")); QCOMPARE(store.read().snapshot, state.snapshot);
    QCOMPARE(store.undo(state.lastImportId, state.snapshot, true), QString()); state = store.read();
    QCOMPARE(state.preview.records.size(), size_t(3)); QCOMPARE(state.lastImportId, firstId);
    QCOMPARE(store.undo(firstId, state.snapshot, true), QString()); state = store.read(); QVERIFY(state.preview.records.empty());
    QCOMPARE(QJsonDocument::fromJson(state.snapshot).object().value("imports").toArray().size(), 2);
    QCOMPARE(store.importSnapshot(second, state.snapshot, true), QString()); QCOMPARE(store.read().preview.records.size(), size_t(4));
  }
  void changedLockedCancelledAndInvalidWritesPreserveTheArchive() {
    QTemporaryDir dir; const auto path = dir.filePath("history.json"); history::LegacyHistoryArchive store(path);
    const auto bytes = source({record("first.png")});
    QCOMPARE(store.importSnapshot(bytes, {}, true, [] { return true; }), QString("cancelled")); QVERIFY(!QFileInfo::exists(path));
    int checks = 0;
    QCOMPARE(store.importSnapshot(bytes, {}, true, [&] { return ++checks > 1; }), QString("cancelled")); QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(store.importSnapshot("{broken}", {}, true), QString("legacy-history-invalid-json")); QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(store.importSnapshot(bytes, {}, true), QString()); const auto state = store.read();
    QCOMPARE(store.importSnapshot(source({record("second.png")}), {}, true), QString("legacy-history-storage-changed"));
    QCOMPARE(store.undo(state.lastImportId, {}, true), QString("legacy-history-storage-changed")); QCOMPARE(read(path), state.snapshot);
    storage::PrivateJsonFile file(path, {"com.xerahs.app/linux-qt/legacy-history", 1, 16 * 1024 * 1024});
    storage::StoreLock lock; QCOMPARE(file.lock(lock), storage::StoreStatus::Ok);
    QCOMPARE(store.importSnapshot(source({record("second.png")}), state.snapshot, true), QString("legacy-history-storage-busy"));
    QCOMPARE(read(path), state.snapshot);
  }
  void onlyExplicitRemovalDropsAnUndoneBackupAndNeverAnActiveImport() {
    QTemporaryDir dir; const auto path = dir.filePath("history.json"); history::LegacyHistoryArchive store(path);
    const auto bytes = source({record("private.png")}); QCOMPARE(store.importSnapshot(bytes, {}, true), QString());
    auto state = store.read(); const auto id = state.lastImportId;
    QCOMPARE(store.removeRecovery(id, state.snapshot, true), QString("legacy-history-recovery-not-available"));
    QCOMPARE(store.read().snapshot, state.snapshot);
    QCOMPARE(store.undo(id, state.snapshot, true), QString()); state = store.read(); QVERIFY(state.recoverySource); QCOMPARE(history::legacyHistorySnapshotBytes(*state.recoverySource), bytes);
    QCOMPARE(store.removeRecovery(id, state.snapshot, false), QString("cancelled")); QCOMPARE(store.read().snapshot, state.snapshot);
    QCOMPARE(store.removeRecovery(id, state.snapshot, true), QString()); state = store.read();
    QVERIFY(state.preview.records.empty()); QVERIFY(!state.recoverySource); QVERIFY(state.recoveryImportId.isEmpty());
    QCOMPARE(QJsonDocument::fromJson(state.snapshot).object().value("imports").toArray().size(), 0);
  }
  void versionOneUpgradesOnlyOnExplicitXmlImportAndRecoveryKeepsBothFormats() {
    QTemporaryDir dir; const auto path = dir.filePath("history.json"); history::LegacyHistoryArchive store(path);
    const auto json = source({record("json.png")});
    const QByteArray xml = "<HistoryItem><Filename>xml.png</Filename><Filepath>/missing/xml.png</Filepath><DateTimeUtc>2026-08-30T12:34:56Z</DateTimeUtc></HistoryItem>";
    QCOMPARE(store.importSnapshot(json, {}, true), QString()); auto state = store.read(); const auto v1 = state.snapshot;
    QCOMPARE(QJsonDocument::fromJson(v1).object().value("version").toInt(), 1);
    QCOMPARE(history::LegacyHistoryArchive(path).read().snapshot, v1);
    QCOMPARE(store.importSnapshot(xml, v1, false), QString("cancelled")); QCOMPARE(read(path), v1);
    QCOMPARE(store.importSnapshot(xml, v1, true), QString()); state = history::LegacyHistoryArchive(path).read();
    QVERIFY2(state.code.isEmpty(), qPrintable(state.code)); QCOMPARE(state.preview.records.size(), size_t(2));
    QCOMPARE(state.preview.format, history::LegacyHistoryFormat::Mixed);
    QCOMPARE(QJsonDocument::fromJson(state.snapshot).object().value("version").toInt(), 2);
    QCOMPARE(state.preview.records.front().fields, record("json.png"));
    const auto id = state.lastImportId;
    QCOMPARE(store.undo(id, state.snapshot, true), QString()); state = store.read();
    QVERIFY(state.recoverySource); QCOMPARE(history::legacyHistorySnapshotBytes(*state.recoverySource), xml);
    QCOMPARE(state.preview.records.size(), size_t(1));
    QCOMPARE(store.importSnapshot(*state.recoverySource, state.snapshot, true), QString());
    QCOMPARE(store.read().preview.records.size(), size_t(2));
  }
  void corruptOrFutureArchivesAreNeverReplaced() {
    QTemporaryDir dir; const auto path = dir.filePath("history.json"); history::LegacyHistoryArchive store(path);
    for (const auto &bytes : {QByteArray("{broken}"), QByteArray("{\"format\":\"com.xerahs.app/linux-qt/legacy-history\",\"version\":3,\"imports\":[]}"),
          QByteArray("{\"format\":\"com.xerahs.app/linux-qt/legacy-history\",\"version\":1,\"imports\":[{}]}")}) {
      QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)); file.close();
      QVERIFY(!store.read().code.isEmpty()); QVERIFY(!store.importSnapshot(source({record("new.png")}), bytes, true).isEmpty()); QCOMPARE(read(path), bytes);
    }
  }
};
QTEST_GUILESS_MAIN(LegacyHistoryArchiveTest)
#include "LegacyHistoryArchiveTest.moc"
