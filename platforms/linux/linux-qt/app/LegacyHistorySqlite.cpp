#include "LegacyHistorySqlite.h"
#include "storage/JsonBoundary.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QMap>
#include <QStringDecoder>
#include <sqlite3.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <utility>

namespace xerahs::app {
namespace {
struct Database { sqlite3 *value = nullptr; ~Database() { if (value) sqlite3_close_v2(value); } };
struct Statement { sqlite3_stmt *value = nullptr; ~Statement() { if (value) sqlite3_finalize(value); } };
struct Backup { sqlite3_backup *value = nullptr; ~Backup() { if (value) sqlite3_backup_finish(value); } };
struct Descriptor { int value = -1; ~Descriptor() { if (value >= 0) ::close(value); } };
struct State {
  const std::function<bool()> &cancelled;
  const std::function<void(int)> &progress;
  QElapsedTimer clock;
  bool stopped = false, timedOut = false, callbackFailed = false;
  bool abort() noexcept {
    try { stopped |= cancelled && cancelled(); } catch (...) { callbackFailed = true; }
    timedOut |= clock.elapsed() >= 10000;
    return stopped || timedOut || callbackFailed;
  }
  void report(int percent) {
    try { if (progress) progress(percent); } catch (...) { callbackFailed = true; }
  }
  QString failure(int status, const char *fallback = "legacy-history-sqlite-transaction-failed") const {
    if (stopped) return "cancelled";
    if (timedOut) return "legacy-history-sqlite-timeout";
    switch (status & 0xff) {
      case SQLITE_BUSY: case SQLITE_LOCKED: return "legacy-history-sqlite-busy";
      case SQLITE_CORRUPT: case SQLITE_NOTADB: return "legacy-history-sqlite-integrity-failed";
      case SQLITE_TOOBIG: case SQLITE_NOMEM: return "legacy-history-sqlite-too-large";
      default: return QString::fromLatin1(fallback);
    }
  }
};
struct Transaction {
  sqlite3 *db;
  bool active = false;
  ~Transaction() { if (active) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
};
int authorizer(void *, int action, const char *first, const char *second, const char *database, const char *origin) {
  if (origin) return SQLITE_DENY;
  switch (action) {
    case SQLITE_SELECT: return SQLITE_OK;
    case SQLITE_READ:
      // SQLite's count(*) optimization reports an empty column and no schema.
      if (!database && first && !qstricmp(first, "History") && second && !*second) return SQLITE_OK;
      return database && !qstricmp(database, "main") && first &&
          (!qstricmp(first, "History") || !qstricmp(first, "sqlite_master") || !qstricmp(first, "sqlite_schema")) ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_TRANSACTION:
      return first && (!qstricmp(first, "BEGIN") || !qstricmp(first, "ROLLBACK")) ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_FUNCTION: return second && !qstricmp(second, "count") ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_PRAGMA:
      if (!first) return SQLITE_DENY;
      if (!qstricmp(first, "quick_check") || !qstricmp(first, "page_count") || !qstricmp(first, "page_size")) return SQLITE_OK;
      return !qstricmp(first, "table_xinfo") && second && !qstricmp(second, "History") ? SQLITE_OK : SQLITE_DENY;
    default: return SQLITE_DENY;
  }
}
bool configure(sqlite3 *db, State &state) {
  for (const auto &option : {std::pair{SQLITE_DBCONFIG_DEFENSIVE, 1}, {SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0},
      {SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0}, {SQLITE_DBCONFIG_ENABLE_TRIGGER, 0}, {SQLITE_DBCONFIG_ENABLE_VIEW, 0},
      {SQLITE_DBCONFIG_DQS_DML, 0}, {SQLITE_DBCONFIG_DQS_DDL, 0}})
    if (sqlite3_db_config(db, option.first, option.second, nullptr) != SQLITE_OK) return false;
  if (sqlite3_enable_load_extension(db, 0) != SQLITE_OK || sqlite3_busy_timeout(db, 0) != SQLITE_OK) return false;
  sqlite3_limit(db, SQLITE_LIMIT_LENGTH, int(history::kLegacySqliteRowsMaxBytes));
  sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, 100000);
  sqlite3_limit(db, SQLITE_LIMIT_COLUMN, 100);
  sqlite3_limit(db, SQLITE_LIMIT_EXPR_DEPTH, 32);
  sqlite3_limit(db, SQLITE_LIMIT_COMPOUND_SELECT, 4);
  sqlite3_limit(db, SQLITE_LIMIT_VDBE_OP, 1000000);
  sqlite3_limit(db, SQLITE_LIMIT_FUNCTION_ARG, 8);
  sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
  sqlite3_limit(db, SQLITE_LIMIT_LIKE_PATTERN_LENGTH, 256);
  sqlite3_limit(db, SQLITE_LIMIT_VARIABLE_NUMBER, 32);
  sqlite3_limit(db, SQLITE_LIMIT_TRIGGER_DEPTH, 0);
  sqlite3_int64 mmap = 0;
  if (sqlite3_file_control(db, "main", SQLITE_FCNTL_MMAP_SIZE, &mmap) != SQLITE_OK) return false;
  sqlite3_progress_handler(db, 1000, [](void *opaque) { return static_cast<State *>(opaque)->abort() ? 1 : 0; }, &state);
  return true;
}
int prepare(sqlite3 *db, Statement &statement, const char *sql) { return sqlite3_prepare_v2(db, sql, -1, &statement.value, nullptr); }
std::optional<QString> text(sqlite3_stmt *statement, int column) {
  const auto *data = sqlite3_column_text(statement, column); const int length = sqlite3_column_bytes(statement, column);
  if (!data) return std::nullopt;
  QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
  const auto value = decoder(QByteArrayView(reinterpret_cast<const char *>(data), length));
  return decoder.hasError() ? std::nullopt : std::optional<QString>(value);
}
int scalar(sqlite3 *db, const char *sql, sqlite3_int64 &value) {
  Statement statement; int status = prepare(db, statement, sql);
  if (status != SQLITE_OK) return status;
  status = sqlite3_step(statement.value);
  if (status != SQLITE_ROW || sqlite3_column_type(statement.value, 0) != SQLITE_INTEGER) return status == SQLITE_ROW ? SQLITE_MISMATCH : status;
  value = sqlite3_column_int64(statement.value, 0);
  return sqlite3_step(statement.value) == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
}
QJsonValue rawValue(sqlite3_stmt *statement, int column) {
  const auto type = sqlite3_column_type(statement, column);
  if (type == SQLITE_NULL) return QJsonValue::Null;
  if (type == SQLITE_INTEGER) return QJsonObject{{"sqliteType", "integer"}, {"value", QString::number(sqlite3_column_int64(statement, column))}};
  if (type == SQLITE_FLOAT) return QJsonObject{{"sqliteType", "real"}, {"value", QString::number(sqlite3_column_double(statement, column), 'g', 17)}};
  if (type == SQLITE_TEXT) if (auto value = text(statement, column)) return *value;
  const auto *bytes = static_cast<const char *>(sqlite3_column_blob(statement, column));
  const int length = sqlite3_column_bytes(statement, column);
  return QJsonObject{{"sqliteType", type == SQLITE_TEXT ? "invalid-utf8" : "blob"},
      {"base64", QString::fromLatin1(QByteArray(bytes, length).toBase64())}};
}
const QStringList stringFields{"FileName", "FilePath", "DateTime", "Type", "Host", "URL", "ThumbnailURL", "DeletionURL", "ShortenedURL"};
}

LegacySqliteRead readLegacySqliteHistory(const QString &path, const std::function<bool()> &cancelled,
    const std::function<void(int)> &progress) {
  State state{cancelled, progress, {}}; state.clock.start();
  const auto fail = [&](int status, const char *fallback = "legacy-history-sqlite-transaction-failed") {
    return LegacySqliteRead{{}, state.failure(status, fallback)};
  };
  if (state.abort()) return fail(SQLITE_INTERRUPT);
  if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null)) return fail(SQLITE_CANTOPEN, "legacy-history-sqlite-unreadable");
  Descriptor selected{::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
  struct stat file{}; char header[16]{};
  if (selected.value < 0 || ::fstat(selected.value, &file) != 0 || !S_ISREG(file.st_mode)) return fail(SQLITE_CANTOPEN, "legacy-history-sqlite-unreadable");
  if (file.st_size > history::kLegacySqliteMaxBytes) return fail(SQLITE_TOOBIG);
  if (::pread(selected.value, header, sizeof(header), 0) != sizeof(header) || std::memcmp(header, "SQLite format 3\0", 16)) return fail(SQLITE_NOTADB);
  Database source;
  int status = sqlite3_open_v2(QFile::encodeName(path).constData(), &source.value,
      SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW | SQLITE_OPEN_NOMUTEX, nullptr);
  if (status != SQLITE_OK) return fail(status, "legacy-history-sqlite-unreadable");
  if (!configure(source.value, state) || sqlite3_set_authorizer(source.value, authorizer, nullptr) != SQLITE_OK)
    return fail(SQLITE_ERROR, "legacy-history-sqlite-configuration-unavailable");
  Transaction transaction{source.value};
  if (sqlite3_exec(source.value, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return fail(sqlite3_errcode(source.value));
  transaction.active = true;
  {
    Statement integrity; status = prepare(source.value, integrity, "PRAGMA quick_check");
    if (status != SQLITE_OK) return fail(status);
    status = sqlite3_step(integrity.value);
    if (status != SQLITE_ROW || text(integrity.value, 0) != std::optional<QString>(QStringLiteral("ok"))) return fail(status, "legacy-history-sqlite-integrity-failed");
    if (sqlite3_step(integrity.value) != SQLITE_DONE) return fail(SQLITE_CORRUPT);
  }
  sqlite3_int64 pages = 0, pageSize = 0;
  status = scalar(source.value, "PRAGMA page_count", pages);
  if (status == SQLITE_OK) status = scalar(source.value, "PRAGMA page_size", pageSize);
  if (status != SQLITE_OK) return fail(status);
  if (pages <= 0 || pageSize < 512 || pageSize > 65536 || (pageSize & (pageSize - 1))) return fail(SQLITE_CORRUPT);
  if (pages > history::kLegacySqliteMaxBytes / pageSize) return fail(SQLITE_TOOBIG);
  {
    Statement schema; status = prepare(source.value, schema, "SELECT type,name,sql FROM sqlite_schema");
    if (status != SQLITE_OK) return fail(status);
    bool found = false;
    while ((status = sqlite3_step(schema.value)) == SQLITE_ROW) {
      const auto name = text(schema.value, 1); if (!name || name->compare("History", Qt::CaseInsensitive)) continue;
      const auto type = text(schema.value, 0), sql = text(schema.value, 2);
      if (found || type != std::optional<QString>(QStringLiteral("table")) || !sql || !sql->trimmed().startsWith("CREATE TABLE", Qt::CaseInsensitive))
        return fail(SQLITE_SCHEMA, "legacy-history-sqlite-schema-invalid");
      found = true;
    }
    if (status != SQLITE_DONE) return fail(status);
    if (!found) return fail(SQLITE_SCHEMA, "legacy-history-sqlite-schema-invalid");
  }
  QStringList columns; QMap<QString, QString> declared;
  {
    Statement schema; status = prepare(source.value, schema, "PRAGMA table_xinfo(History)");
    if (status != SQLITE_OK) return fail(status, "legacy-history-sqlite-schema-invalid");
    while ((status = sqlite3_step(schema.value)) == SQLITE_ROW) {
      const auto name = text(schema.value, 1), type = text(schema.value, 2);
      if (!name || !type || name->contains(QChar::Null) || declared.contains(*name) || sqlite3_column_int(schema.value, 6) != 0 ||
          (*name == "Id" && sqlite3_column_int(schema.value, 5) != 1)) return fail(SQLITE_SCHEMA, "legacy-history-sqlite-schema-invalid");
      columns.append(*name); declared.insert(*name, type->toUpper());
    }
    if (status != SQLITE_DONE) return fail(status);
  }
  if (declared.value("Id") != "INTEGER" || declared.value("Tags") != "TEXT") return fail(SQLITE_SCHEMA, "legacy-history-sqlite-schema-invalid");
  for (const auto &name : stringFields) if (declared.value(name) != "TEXT") return fail(SQLITE_SCHEMA, "legacy-history-sqlite-schema-invalid");
  state.report(20); if (state.abort()) return fail(SQLITE_INTERRUPT);
  Database memory;
  if (sqlite3_open_v2(":memory:", &memory.value, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK)
    return fail(SQLITE_NOMEM);
  // In-memory destinations must use the source page size before backup starts.
  static const char *pageStatements[]{"PRAGMA page_size=512", "PRAGMA page_size=1024", "PRAGMA page_size=2048", "PRAGMA page_size=4096",
      "PRAGMA page_size=8192", "PRAGMA page_size=16384", "PRAGMA page_size=32768", "PRAGMA page_size=65536"};
  int pageIndex = 0; for (auto size = pageSize; size > 512; size >>= 1) ++pageIndex;
  if (sqlite3_exec(memory.value, pageStatements[pageIndex], nullptr, nullptr, nullptr) != SQLITE_OK) return fail(sqlite3_errcode(memory.value));
  {
    Backup backup{sqlite3_backup_init(memory.value, "main", source.value, "main")};
    if (!backup.value) return fail(sqlite3_errcode(memory.value));
    do {
      if (state.abort()) return fail(SQLITE_INTERRUPT);
      status = sqlite3_backup_step(backup.value, 128);
      const int total = sqlite3_backup_pagecount(backup.value), remaining = sqlite3_backup_remaining(backup.value);
      if (total > 0) state.report(20 + int(qint64(total - remaining) * 30 / total));
    } while (status == SQLITE_OK);
    if (status != SQLITE_DONE) return fail(status);
    status = sqlite3_backup_finish(std::exchange(backup.value, nullptr)); if (status != SQLITE_OK) return fail(status);
  }
  sqlite3_int64 count = 0; status = scalar(source.value, "SELECT COUNT(*) FROM History", count);
  if (status != SQLITE_OK || count < 0) return fail(status);
  QByteArray records;
  {
    Statement rows; status = prepare(source.value, rows, "SELECT * FROM History ORDER BY Id");
    if (status != SQLITE_OK) return fail(status);
    if (sqlite3_column_count(rows.value) != columns.size()) return fail(SQLITE_SCHEMA, "legacy-history-sqlite-schema-invalid");
    sqlite3_int64 processed = 0;
    while ((status = sqlite3_step(rows.value)) == SQLITE_ROW) {
      if (state.abort()) return fail(SQLITE_INTERRUPT);
      QJsonObject row;
      for (int col = 0; col < columns.size(); ++col) {
        const auto &name = columns[col]; const int type = sqlite3_column_type(rows.value, col);
        if (name == "Id" && type == SQLITE_INTEGER) row.insert(name, QString::number(sqlite3_column_int64(rows.value, col)));
        else if (stringFields.contains(name) && type == SQLITE_NULL) row.insert(name, QString{});
        else if (name == "Tags") {
          if (type == SQLITE_NULL) { row.insert(name, QJsonObject{}); continue; }
          const auto tag = type == SQLITE_TEXT ? text(rows.value, col) : std::nullopt;
          if (tag && tag->trimmed().isEmpty()) { row.insert(name, QJsonObject{}); continue; }
          if (tag) {
            const auto parsed = compatibility::readJson(tag->toUtf8(), {history::kLegacySqliteRowsMaxBytes, 64, 1000000});
            if (parsed.ok() && parsed.document.isObject()) row.insert(name, parsed.document.object());
            else if (tag->trimmed() == "null") row.insert(name, QJsonObject{});
            else row.insert(name, *tag); // wrong shape remains a corrupt record for Core quarantine
          } else row.insert(name, QJsonArray{rawValue(rows.value, col)});
        } else row.insert(name, rawValue(rows.value, col));
      }
      const auto encoded = QJsonDocument(row).toJson(QJsonDocument::Compact);
      if (records.size() + encoded.size() + 1 > history::kLegacySqliteRowsMaxBytes) return fail(SQLITE_TOOBIG);
      if (!records.isEmpty()) records += ',';
      records += encoded; ++processed;
      if (count > 0) state.report(50 + int(qMin(processed, count) * 45 / count));
    }
    if (status != SQLITE_DONE) return fail(status);
    if (processed != count) return fail(SQLITE_ERROR);
  }
  if (state.abort()) return fail(SQLITE_INTERRUPT);
  sqlite3_int64 length = 0;
  auto *serialized = sqlite3_serialize(memory.value, "main", &length, 0);
  if (!serialized) return fail(SQLITE_NOMEM);
  QByteArray database;
  if (length > 0 && length <= history::kLegacySqliteMaxBytes) database = QByteArray(reinterpret_cast<const char *>(serialized), qsizetype(length));
  sqlite3_free(serialized);
  if (database.isEmpty()) return fail(SQLITE_TOOBIG);
  status = sqlite3_exec(source.value, "ROLLBACK", nullptr, nullptr, nullptr);
  if (status != SQLITE_OK) return fail(status);
  transaction.active = false;
  if (state.abort()) return fail(SQLITE_INTERRUPT);
  state.report(100);
  return {history::SqliteHistorySnapshot{std::move(database), std::move(records)}, {}};
}
}
