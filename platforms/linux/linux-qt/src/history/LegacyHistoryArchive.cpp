#include "LegacyHistoryArchive.h"
#include "storage/PrivateJsonFile.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QSet>

namespace xerahs::history {
namespace {
storage::PrivateJsonFile archive(const QString &path) {
  return {path, {"com.xerahs.app/linux-qt/legacy-history", 1, 16 * 1024 * 1024}};
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
  for (const auto &value : imports.toArray()) {
    const auto item = value.toObject();
    const auto encoded = item.value("source").toString().toLatin1();
    if (!value.isObject() || !item.value("active").isBool() || !item.value("id").isString() ||
        item.value("sourceFormat") != "legacy-json-fragments" || item.value("sourceVersion") != "unversioned" ||
        !item.value("source").isString() || encoded.size() > (kLegacyHistoryMaxBytes + 2) / 3 * 4 ||
        !QDateTime::fromString(item.value("importedAt").toString(), Qt::ISODate).isValid()) {
      result.view.code = "legacy-history-storage-invalid"; return result;
    }
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    const auto id = item.value("id").toString();
    if (!decoded || digest(decoded.decoded) != id || batches.contains(id)) { result.view.code = "legacy-history-storage-invalid"; return result; }
    batches.insert(id);
    auto preview = previewLegacyHistory(decoded.decoded);
    if (!preview.ok()) { result.view.code = "legacy-history-storage-invalid"; return result; }
    if (!item.value("active").toBool()) { result.view.recoverySource = decoded.decoded; result.view.recoveryImportId = id; continue; }
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
QString LegacyHistoryArchive::importSnapshot(const QByteArray &source, const QByteArray &expected, bool confirmed,
    const std::function<bool()> &stop) const {
  if (!confirmed || cancelled(stop)) return "cancelled";
  const auto preview = previewLegacyHistory(source);
  if (!preview.ok()) return preview.code;
  auto current = load(m_path);
  if (!current.view.code.isEmpty()) return current.view.code;
  if (current.store.snapshot != expected) return "legacy-history-storage-changed";
  auto imports = current.store.document.value("imports").toArray();
  const auto id = digest(source);
  QJsonObject item;
  for (qsizetype i = 0; i < imports.size(); ++i) if (imports[i].toObject().value("id") == id) {
    item = imports[i].toObject();
    if (item.value("active").toBool()) return {};
    imports.removeAt(i); break;
  }
  if (item.isEmpty()) item = {{"id", id}, {"source", QString::fromLatin1(source.toBase64())},
      {"sourceFormat", "legacy-json-fragments"}, {"sourceVersion", "unversioned"},
      {"importedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  item.insert("active", true); imports.append(item);
  auto document = current.store.document; document.insert("imports", imports);
  if (cancelled(stop)) return "cancelled";
  const auto status = archive(m_path).write(document, 1, expected, nullptr);
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
  const auto status = archive(m_path).write(document, 1, expected, nullptr);
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
  const auto status = archive(m_path).write(document, 1, expected, nullptr);
  return status == storage::StoreStatus::Ok ? QString{} : failure(status);
}
}
