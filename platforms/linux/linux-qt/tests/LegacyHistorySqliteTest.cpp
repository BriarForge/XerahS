#include "LegacyHistorySqlite.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <sqlite3.h>
#include <sys/stat.h>
using namespace xerahs;
namespace {
struct Db { sqlite3 *value = nullptr; explicit Db(const QString &path) { if (sqlite3_open(QFile::encodeName(path).constData(), &value) != SQLITE_OK) qFatal("fixture database open failed"); } ~Db() { sqlite3_close(value); } };
void sql(sqlite3 *db, const char *query) { if (sqlite3_exec(db, query, nullptr, nullptr, nullptr) != SQLITE_OK) qFatal("fixture query failed"); }
const char *schema = "CREATE TABLE History(Id INTEGER PRIMARY KEY AUTOINCREMENT,FileName TEXT,FilePath TEXT,DateTime TEXT,Type TEXT,Host TEXT,URL TEXT,ThumbnailURL TEXT,DeletionURL TEXT,ShortenedURL TEXT,Tags TEXT)";
void add(sqlite3 *db, sqlite3_int64 id, const char *name = "fixture.png", const char *tags = "{}") {
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(db, "INSERT INTO History VALUES(?,?,?,'2026-08-30T12:34:56.1234567+10:00','Image',NULL,'https://example.invalid/private',NULL,NULL,NULL,?)", -1, &statement, nullptr) != SQLITE_OK) qFatal("fixture insert prepare failed");
  sqlite3_bind_int64(statement, 1, id); sqlite3_bind_text(statement, 2, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, "/missing/private-fixture.png", -1, SQLITE_STATIC); sqlite3_bind_text(statement, 4, tags, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(statement) != SQLITE_DONE) qFatal("fixture insert failed"); sqlite3_finalize(statement);
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QJsonArray records(const app::LegacySqliteRead &result) { return QJsonDocument::fromJson('[' + result.snapshot->records + ']').array(); }
sqlite3_int64 count(sqlite3 *db, const char *query = "SELECT count(*) FROM History") {
  sqlite3_stmt *s = nullptr; if (sqlite3_prepare_v2(db, query, -1, &s, nullptr) != SQLITE_OK || sqlite3_step(s) != SQLITE_ROW) qFatal("fixture count failed");
  const auto value = sqlite3_column_int64(s, 0); sqlite3_finalize(s); return value;
}
}
class LegacyHistorySqliteTest final : public QObject {
  Q_OBJECT
private slots:
  void realReadonlySnapshotKeepsExactIdsUnicodeTagsNullsAndSourceBytes() {
    QTemporaryDir dir; const auto path = dir.filePath("History.db");
    { Db db(path); sql(db.value, schema); add(db.value, 9007199254740993LL, "caf\xc3\xa9.png", "{\"Favorite\":null,\"WindowTitle\":\"private window\"}"); }
    const auto before = bytes(path); int progress = 0;
    const auto result = app::readLegacySqliteHistory(path, {}, [&](int value) { progress = value; });
    QVERIFY2(result.snapshot.has_value(), qPrintable(result.code)); QVERIFY(result.code.isEmpty()); QCOMPARE(progress, 100);
    const auto rows = records(result); QCOMPARE(rows.size(), 1); const auto row = rows.first().toObject();
    QCOMPARE(row.value("Id").toString(), QString("9007199254740993")); QCOMPARE(row.value("FileName").toString(), QString::fromUtf8("café.png"));
    QCOMPARE(row.value("DateTime").toString(), QString("2026-08-30T12:34:56.1234567+10:00")); QCOMPARE(row.value("Host").toString(), QString());
    QVERIFY(row.value("Tags").toObject().value("Favorite").isNull()); QCOMPARE(bytes(path), before);
    const auto again = app::readLegacySqliteHistory(path); QVERIFY(again.snapshot); QCOMPARE(again.snapshot->database, result.snapshot->database); QCOMPARE(again.snapshot->records, result.snapshot->records);
    const auto copied = dir.filePath("snapshot.db"); QFile out(copied); QVERIFY(out.open(QIODevice::WriteOnly)); QCOMPARE(out.write(result.snapshot->database), result.snapshot->database.size()); out.close();
    Db snapshot(copied); QCOMPARE(count(snapshot.value), sqlite3_int64(1));
  }
  void malformedValuesStayTypedAndNeighboursRemainOrdered() {
    QTemporaryDir dir; const auto path = dir.filePath("History.db");
    { Db db(path); sql(db.value, schema); add(db.value, 1); add(db.value, 2, "bad-tags.png", "{broken}"); add(db.value, 3);
      sql(db.value, "UPDATE History SET FileName=x'fffe' WHERE Id=3; ALTER TABLE History ADD COLUMN FutureMetadata TEXT; UPDATE History SET FutureMetadata='retained inertly'"); }
    const auto result = app::readLegacySqliteHistory(path); QVERIFY2(result.snapshot.has_value(), qPrintable(result.code));
    const auto rows = records(result); QCOMPARE(rows.size(), 3); QCOMPARE(rows[0].toObject().value("Id").toString(), QString("1"));
    QCOMPARE(rows[1].toObject().value("Tags").toString(), QString("{broken}")); QVERIFY(rows[2].toObject().value("FileName").isObject());
    QCOMPARE(rows[0].toObject().value("FutureMetadata").toString(), QString("retained inertly"));
  }
  void sourceTriggerNeverRunsAndHistoryViewOrGeneratedSchemaIsRejected() {
    QTemporaryDir dir; const auto path = dir.filePath("History.db");
    { Db db(path); sql(db.value, schema); add(db.value, 1); sql(db.value, "CREATE TABLE Trap(Value INTEGER); CREATE TRIGGER NoWrites AFTER INSERT ON History BEGIN INSERT INTO Trap VALUES(1); END"); }
    const auto before = bytes(path); auto result = app::readLegacySqliteHistory(path); QVERIFY2(result.snapshot.has_value(), qPrintable(result.code)); QCOMPARE(bytes(path), before);
    { Db db(path); QCOMPARE(count(db.value, "SELECT count(*) FROM Trap"), sqlite3_int64(0)); sql(db.value, "DROP TRIGGER NoWrites; ALTER TABLE History RENAME TO RealHistory; CREATE VIEW History AS SELECT load_extension('/must-not-load') AS Id"); }
    result = app::readLegacySqliteHistory(path); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("legacy-history-sqlite-schema-invalid"));
    { Db db(path); sql(db.value, "DROP VIEW History; ALTER TABLE RealHistory RENAME TO History; ALTER TABLE History ADD COLUMN Computed TEXT GENERATED ALWAYS AS (FileName) VIRTUAL"); }
    result = app::readLegacySqliteHistory(path); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("legacy-history-sqlite-schema-invalid"));
  }
  void walAndConcurrentWriterCannotChangeTheReviewedTransaction() {
    QTemporaryDir dir; const auto path = dir.filePath("History.db"); Db writer(path);
    sql(writer.value, "PRAGMA journal_mode=WAL"); sql(writer.value, schema); add(writer.value, 1);
    bool changed = false;
    const auto result = app::readLegacySqliteHistory(path, {}, [&](int value) { if (value >= 20 && !changed) { changed = true; add(writer.value, 2); } });
    QVERIFY2(result.snapshot.has_value(), qPrintable(result.code)); QVERIFY(changed); QCOMPARE(count(writer.value), sqlite3_int64(2));
    QCOMPARE(records(result).size(), 1); QCOMPARE(records(result)[0].toObject().value("Id").toString(), QString("1"));
    const auto copied = dir.filePath("snapshot.db"); QFile file(copied); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(result.snapshot->database); file.close();
    Db snapshot(copied); QCOMPARE(count(snapshot.value), sqlite3_int64(1));
    const auto later = app::readLegacySqliteHistory(path); QVERIFY2(later.snapshot.has_value(), qPrintable(later.code)); QCOMPARE(records(later).size(), 2);
  }
  void missingCorruptWrongSchemaBusyAndCancellationAreValueFreeFailures() {
    QTemporaryDir dir; const auto path = dir.filePath("private-path.db");
    auto result = app::readLegacySqliteHistory(path); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("legacy-history-sqlite-unreadable"));
    { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a database private secret"); }
    result = app::readLegacySqliteHistory(path); QVERIFY(!result.snapshot); QVERIFY(!result.code.contains("private"));
    QFile::remove(path); { Db db(path); sql(db.value, "CREATE TABLE Wrong(Value TEXT)"); }
    result = app::readLegacySqliteHistory(path); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("legacy-history-sqlite-schema-invalid"));
    QFile::remove(path); Db db(path); sql(db.value, schema); add(db.value, 1);
    result = app::readLegacySqliteHistory(path, [] { return true; }); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("cancelled"));
    bool stop = false; result = app::readLegacySqliteHistory(path, [&] { return stop; }, [&](int) { stop = true; }); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("cancelled"));
    sql(db.value, "BEGIN EXCLUSIVE"); result = app::readLegacySqliteHistory(path); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("legacy-history-sqlite-busy")); sql(db.value, "ROLLBACK");
    const auto fifo = dir.filePath("fifo.db"); QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    result = app::readLegacySqliteHistory(fifo); QVERIFY(!result.snapshot); QCOMPARE(result.code, QString("legacy-history-sqlite-unreadable"));
  }
};
QTEST_GUILESS_MAIN(LegacyHistorySqliteTest)
#include "LegacyHistorySqliteTest.moc"
