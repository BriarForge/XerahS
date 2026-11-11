#include "LegacyHistoryArchive.h"
#include "storage/PrivateJsonFile.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QSet>

namespace xerahs::history {
namespace {
storage::PrivateJsonFile archive(const QString &path) {
  return {path, {"com.xerahs.app/linux-qt/legacy-history", 2, 128 * 1024 * 1024}};
}
QString digest(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
QString failure(storage::StoreStatus status) { return "legacy-history-storage-" + storage::storeStatusName(status); }
bool cancelled(const std::function<bool()> &test) { return test && test(); }
struct Loaded {
  storage::StoreRead store;
  LegacyHistoryArchiveRead view;
};
Loaded load(const QString &path) {
  Loaded result; result.store = archive(path).read(); result.view.snapshot = result.store.snapshot;
  if (result.store.status == storage::StoreStatus::Missing) return result;
  if (result.store.status != storage::StoreStatus::Ok) { result.view.code = failure(result.store.status); return result; }
  const auto imports = result.store.document.value("imports");
  if (!imports.isArray()) { result.view.code = "legacy-history-storage-invalid"; return result; }
  QSet<QString> batches;
  bool formatSeen = false;
  for (const auto &value : imports.toArray()) {
    const auto item = value.toObject();
    const auto encoded = item.value("source").toString().toLatin1();
    const auto format = item.value("sourceFormat").toString();
    const bool sqlite = format == "legacy-sqlite-snapshot";
    if (result.store.version == 1 && format != "legacy-json-fragments") { result.view.code = "legacy-history-storage-invalid"; return result; }
    const auto sourceLimit = sqlite ? kLegacySqliteMaxBytes : kLegacyHistoryMaxBytes;
    if (!value.isObject() || !item.value("active").isBool() || !item.value("id").isString() ||
        (format != "legacy-json-fragments" && format != "legacy-xml-fragments" && !sqlite) || item.value("sourceVersion") != "unversioned" ||
        !item.value("source").isString() || encoded.size() > (sourceLimit + 2) / 3 * 4 ||
        !QDateTime::fromString(item.value("importedAt").toString(), Qt::ISODate).isValid()) {
      result.view.code = "legacy-history-storage-invalid"; return result;
    }
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    const auto id = item.value("id").toString();
    if (!decoded || digest(decoded.decoded) != id || batches.contains(id)) { result.view.code = "legacy-history-storage-invalid"; return result; }
    batches.insert(id);
    LegacyHistorySnapshot snapshot = decoded.decoded;
    if (sqlite) {
      const auto records = item.value("records").toString().toLatin1();
      if (!item.value("records").isString() || records.size() > (kLegacySqliteRowsMaxBytes + 2) / 3 * 4) { result.view.code = "legacy-history-storage-invalid"; return result; }
      const auto normalized = QByteArray::fromBase64Encoding(records, QByteArray::AbortOnBase64DecodingErrors);
      if (!normalized || item.value("recordsHash").toString() != digest(normalized.decoded)) { result.view.code = "legacy-history-storage-invalid"; return result; }
      snapshot = SqliteHistorySnapshot{decoded.decoded, normalized.decoded};
    }
    auto preview = previewLegacyHistorySnapshot(snapshot);
    if (legacyHistoryFormatName(preview.format) != format) { result.view.code = "legacy-history-storage-invalid"; return result; }
    if (!preview.ok()) { result.view.code = "legacy-history-storage-invalid"; return result; }
    if (!item.value("active").toBool()) { result.view.recoverySource = snapshot; result.view.recoveryImportId = id; continue; }
    if (!formatSeen) { result.view.preview.format = preview.format; formatSeen = true; }
    else if (result.view.preview.format != preview.format) result.view.preview.format = LegacyHistoryFormat::Mixed;
    result.view.lastImportId = id;
    for (auto &record : preview.records) {
      // Unversioned JSON has no task IDs: matching fields cannot prove that
      // two records are the same task. Preserve distinct snapshots in full.
      record.archiveImportId = id; result.view.preview.records.push_back(std::move(record));
      result.view.preview.report.add(compatibility::ImportDisposition::Retained);
    }
    for (const auto &quarantine : preview.quarantine) result.view.preview.quarantine.append(quarantine);
    result.view.preview.report.add(compatibility::ImportDisposition::Quarantined, preview.quarantine.size());
  }
  return result;
}
}
LegacyHistoryArchiveRead LegacyHistoryArchive::read() const { return load(m_path).view; }
QString LegacyHistoryArchive::importSnapshot(const LegacyHistorySnapshot &source, const QByteArray &expected, bool confirmed,
    const std::function<bool()> &stop) const {
  if (!confirmed || cancelled(stop)) return "cancelled";
  const auto preview = previewLegacyHistorySnapshot(source);
  if (!preview.ok()) return preview.code;
  auto current = load(m_path);
  if (!current.view.code.isEmpty()) return current.view.code;
  if (current.store.snapshot != expected) return "legacy-history-storage-changed";
  auto imports = current.store.document.value("imports").toArray();
  const auto &original = legacyHistorySnapshotBytes(source);
  const auto id = digest(original);
  QJsonObject item;
  for (qsizetype i = 0; i < imports.size(); ++i) if (imports[i].toObject().value("id") == id) {
    item = imports[i].toObject();
    if (item.value("active").toBool()) return {};
    imports.removeAt(i); break;
  }
  if (item.isEmpty()) item = {{"id", id}, {"source", QString::fromLatin1(original.toBase64())},
      {"sourceFormat", legacyHistoryFormatName(preview.format)}, {"sourceVersion", "unversioned"},
      {"importedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  if (const auto *sqlite = std::get_if<SqliteHistorySnapshot>(&source)) {
    item.insert("records", QString::fromLatin1(sqlite->records.toBase64())); item.insert("recordsHash", digest(sqlite->records));
  }
  item.insert("active", true); imports.append(item);
  auto document = current.store.document; document.insert("imports", imports);
  if (cancelled(stop)) return "cancelled";
  const auto version = current.store.version >= 2 || preview.format != LegacyHistoryFormat::Json ? 2 : 1;
  const auto status = archive(m_path).write(document, version, expected, nullptr);
  return status == storage::StoreStatus::Ok ? QString{} : failure(status);
}
QString LegacyHistoryArchive::undo(const QString &importId, const QByteArray &expected, bool confirmed,
    const std::function<bool()> &stop) const {
  if (!confirmed || cancelled(stop)) return "cancelled";
  auto current = load(m_path);
  if (!current.view.code.isEmpty()) return current.view.code;
  if (current.store.snapshot != expected) return "legacy-history-storage-changed";
  auto imports = current.store.document.value("imports").toArray(); bool found = false;
  for (qsizetype i = 0; i < imports.size(); ++i) {
    auto item = imports[i].toObject();
    if (item.value("id") == importId && item.value("active").toBool()) {
      item.insert("active", false); imports.removeAt(i); imports.append(item); found = true; break;
    }
  }
  if (!found) return "legacy-history-import-not-active";
  auto document = current.store.document; document.insert("imports", imports);
  if (cancelled(stop)) return "cancelled";
  const auto status = archive(m_path).write(document, qMax(1, current.store.version), expected, nullptr);
  return status == storage::StoreStatus::Ok ? QString{} : failure(status);
}
QString LegacyHistoryArchive::removeRecovery(const QString &importId, const QByteArray &expected, bool confirmed,
    const std::function<bool()> &stop) const {
  if (!confirmed || cancelled(stop)) return "cancelled";
  auto current = load(m_path);
  if (!current.view.code.isEmpty()) return current.view.code;
  if (current.store.snapshot != expected) return "legacy-history-storage-changed";
  auto imports = current.store.document.value("imports").toArray(); bool found = false;
  for (qsizetype i = 0; i < imports.size(); ++i) {
    const auto item = imports[i].toObject();
    if (item.value("id") == importId && !item.value("active").toBool()) { imports.removeAt(i); found = true; break; }
  }
  if (!found) return "legacy-history-recovery-not-available";
  auto document = current.store.document; document.insert("imports", imports);
  if (cancelled(stop)) return "cancelled";
  const auto status = archive(m_path).write(document, qMax(1, current.store.version), expected, nullptr);
  return status == storage::StoreStatus::Ok ? QString{} : failure(status);
}
}
