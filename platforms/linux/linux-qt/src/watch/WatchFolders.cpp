// CORE-024, CORE-025.
#include "WatchFolders.h"
#include "storage/PrivateJsonFile.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <unistd.h>

namespace xerahs::watch {
namespace {
bool within(const QString &path, const QString &root) { return path == root || path.startsWith(root + '/'); }
QString capturesPath(const QString &path) {
  const auto canonical = QFileInfo(path).canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : canonical;
}
}
QString authorizeRoot(const QString &path, const QString &captures, QString *canonical) {
  const QFileInfo info(path);
  const auto root = info.canonicalFilePath();
  if (root.isEmpty() || !info.isDir()) return "watch-directory-required";
  if (info.ownerId() != uint(geteuid()) || !info.isReadable() || !info.isExecutable()) return "watch-directory-denied";
  const QStringList system{ "/", "/bin", "/boot", "/dev", "/etc", "/lib", "/lib64", "/proc", "/root", "/run", "/sbin", "/sys", "/usr", "/var", "/home", "/tmp", "/opt", "/mnt", "/media" };
  if (system.contains(root)) return "watch-system-root";
  for (const auto &reserved : QStringList{ "/dev", "/proc", "/sys", "/etc", "/usr", "/boot" })
    if (within(root, reserved)) return "watch-system-root";
  if (within(root, capturesPath(captures))) return "watch-capture-loop";
  *canonical = root;
  return {};
}
bool matches(const Rule &rule, const QString &path) {
  const QFileInfo info(path);
  const auto name = info.fileName();
  if (!info.isFile() || info.isSymLink() || info.isHidden() || name.startsWith('.') || name.startsWith('~') || name.endsWith('~')) return false;
  for (const auto &part : QDir::cleanPath(path).split('/')) if (part.startsWith('.')) return false;
  for (const auto &suffix : QStringList{ ".tmp", ".temp", ".part", ".partial", ".crdownload", ".swp" })
    if (name.endsWith(suffix, Qt::CaseInsensitive)) return false;
  for (const auto &glob : rule.filters)
    if (QRegularExpression(QRegularExpression::wildcardToRegularExpression(glob), QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) return true;
  return false;
}
QString fingerprint(const QString &path) {
  const QFileInfo info(path);
  if (!info.isFile() || info.isSymLink()) return {};
  return info.canonicalFilePath() + '\n' + QString::number(info.size()) + '\n' + QString::number(info.lastModified().toMSecsSinceEpoch());
}
QString publicationDispositionName(PublicationDisposition disposition) {
  switch (disposition) {
    case PublicationDisposition::Processed: return QStringLiteral("processed");
    case PublicationDisposition::Released: return QStringLiteral("released");
    case PublicationDisposition::Unconfirmed: return QStringLiteral("unconfirmed");
  }
  return QStringLiteral("unconfirmed");
}
QString publicationStateName(PublicationState state) {
  return state == PublicationState::Unconfirmed ? QStringLiteral("unconfirmed") : QStringLiteral("sending");
}
PublicationDisposition publicationDisposition(const PublicationFacts &facts) {
  // CORE-025 P1. The order is the contract; the first match wins.
  if (facts.restartWithSending) return PublicationDisposition::Unconfirmed;
  if (facts.succeeded) return PublicationDisposition::Processed;
  if (facts.requestStarted && facts.everyAttemptAnswered) return PublicationDisposition::Released;
  if (facts.requestStarted) return PublicationDisposition::Unconfirmed;
  return PublicationDisposition::Released;
}
QStringList retainedKeys(const QStringList &keys, const QList<RetentionFacts> &facts) {
  QHash<QString, RetentionFacts> byKey;
  for (const auto &fact : facts) byKey.insert(fact.key, fact);
  QStringList kept;
  for (const auto &key : keys) {
    const auto it = byKey.constFind(key);
    if (it == byKey.constEnd()) { kept.append(key); continue; }
    const auto &fact = *it;
    // A sending publication is never pruned.
    if (fact.sending) { kept.append(key); continue; }
    // A missing parent may be an unmounted or removed root, not a deleted file.
    if (!fact.parentExists) { kept.append(key); continue; }
    // The file is kept only while its fingerprint still equals the recorded key.
    if (fact.fingerprint == key) kept.append(key);
  }
  return kept;
}
namespace {
storage::PrivateJsonFile watchFile(const QString &path) {
  return {path, {QStringLiteral("com.xerahs.app/watch-folders"), 2, 16 * 1024 * 1024}};
}
storage::PrivateJsonFile rulesFile(const QString &path) {
  return {path, {QStringLiteral("com.xerahs.app/watch-folders"), 1, 16 * 1024 * 1024}};
}
// CORE-025 P6: read the processed store. Version 1 holds only `items`; version 2
// adds `publications`. A version 2 store is refused by a reader that only
// understands version 1, so the refusal happens in the version check below.
QString readProcessedStore(const QString &path, QJsonArray *items, QJsonArray *publications,
                           QByteArray *snapshot, int *version) {
  const auto read = watchFile(path).read();
  *snapshot = read.snapshot;
  switch (read.status) {
    case storage::StoreStatus::Missing: return {};
    case storage::StoreStatus::Unsafe: return "watch-store-unsafe";
    case storage::StoreStatus::TooLarge:
    case storage::StoreStatus::ReadFailed: return "watch-store-read-failed";
    case storage::StoreStatus::Ok: break;
    default: return "watch-store-invalid-version-or-data";
  }
  if (!read.document.value("items").isArray()) return "watch-store-invalid-version-or-data";
  if (read.version == 1) {
    *items = read.document.value("items").toArray();
    if (publications) *publications = QJsonArray{};
    if (version) *version = 1;
    return {};
  }
  if (read.version != 2) return "watch-store-invalid-version-or-data";
  const auto records = read.document.value("publications");
  if (!records.isArray()) return "watch-store-invalid-version-or-data";
  *items = read.document.value("items").toArray();
  if (publications) *publications = records.toArray();
  if (version) *version = 2;
  return {};
}
QString readRulesStore(const QString &path, QJsonArray *items, QByteArray *snapshot) {
  const auto read = rulesFile(path).read();
  *snapshot = read.snapshot;
  switch (read.status) {
    case storage::StoreStatus::Missing: return {};
    case storage::StoreStatus::Unsafe: return "watch-store-unsafe";
    case storage::StoreStatus::TooLarge:
    case storage::StoreStatus::ReadFailed: return "watch-store-read-failed";
    case storage::StoreStatus::Ok: break;
    default: return "watch-store-invalid-version-or-data";
  }
  if (read.version != 1 || !read.document.value("items").isArray()) return "watch-store-invalid-version-or-data";
  *items = read.document.value("items").toArray();
  return {};
}
QJsonArray publicationsJson(const QList<PublicationRecord> &records) {
  QJsonArray array;
  for (const auto &record : records)
    array.append(QJsonObject{{"key", record.key}, {"state", publicationStateName(record.state)}, {"at", record.at}});
  return array;
}
QList<PublicationRecord> publicationsFrom(const QJsonArray &array, QString *error) {
  QList<PublicationRecord> records;
  for (const auto &value : array) {
    const auto object = value.toObject();
    const auto key = object.value("key").toString();
    const auto state = object.value("state").toString();
    if (key.isEmpty() || (state != QLatin1String("sending") && state != QLatin1String("unconfirmed"))) {
      if (error) *error = "watch-store-invalid-version-or-data";
      return {};
    }
    records.append({key, state == QLatin1String("unconfirmed") ? PublicationState::Unconfirmed : PublicationState::Sending,
                    object.value("at").toString()});
  }
  return records;
}
QString writeProcessedStore(const QString &path, const QJsonArray &items, const QJsonArray &publications,
                             QByteArray *snapshot) {
  const auto store = watchFile(path);
  const auto read = store.read();
  if (read.status == storage::StoreStatus::Unsafe) return "watch-store-unsafe";
  if (read.status == storage::StoreStatus::ReadFailed || read.status == storage::StoreStatus::TooLarge) return "watch-store-read-failed";
  if (read.status != storage::StoreStatus::Missing &&
      (read.status != storage::StoreStatus::Ok || (read.version != 1 && read.version != 2))) return "watch-store-invalid-version-or-data";
  const auto parent = QFileInfo(path).absolutePath();
  const QFileInfo dir(parent);
  if ((dir.exists() && dir.ownerId() != uint(geteuid())) || !QDir().mkpath(parent)) return "watch-store-unsafe";
  if (!QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return "watch-store-permissions";
  const auto status = store.write({{"items", items}, {"publications", publications}}, 2, *snapshot, snapshot);
  switch (status) {
    case storage::StoreStatus::Ok: return {};
    case storage::StoreStatus::Unsafe: return "watch-store-unsafe";
    case storage::StoreStatus::TooLarge: return "watch-store-full";
    default: return "watch-store-write-failed";
  }
}
// Called only by worker write paths. Fingerprints end in two newline-separated
// numeric fields; removing those from the right preserves newlines in filenames.
CheckpointResult persistCheckpoint(const QString &path, const QByteArray &expected,
    QSet<QString> processed, QList<PublicationRecord> publications) {
  auto allKeys = processed;
  QSet<QString> sending;
  for (const auto &record : publications) {
    allKeys.insert(record.key);
    if (record.state == PublicationState::Sending) sending.insert(record.key);
  }
  const auto keys = allKeys.values();
  QList<RetentionFacts> facts;
  for (const auto &key : keys) {
    const auto last = key.lastIndexOf('\n');
    const auto previous = last > 0 ? key.lastIndexOf('\n', last - 1) : -1;
    // Preserve unrecognized historic keys rather than guessing their paths.
    if (previous < 0) continue;
    const auto source = key.left(previous);
    facts.append({key, QFileInfo(QFileInfo(source).absolutePath()).isDir(), fingerprint(source), sending.contains(key)});
  }
  const auto retained = retainedKeys(keys, facts);
  const QSet<QString> kept(retained.cbegin(), retained.cend());
  processed.intersect(kept);
  publications.removeIf([&](const PublicationRecord &record) { return !kept.contains(record.key); });
  QJsonArray items;
  for (const auto &key : processed) items.append(key);
  auto snapshot = expected;
  const auto error = writeProcessedStore(path, items, publicationsJson(publications), &snapshot);
  return {error, snapshot, std::move(processed), std::move(publications)};
}
// The watch rules store keeps version 1; CORE-025 P6 changes only the checkpoint store.
QString writeRulesStore(const QString &path, const QJsonArray &items, QByteArray *snapshot) {
  const auto store = rulesFile(path);
  const auto read = store.read();
  if (read.status == storage::StoreStatus::Unsafe) return "watch-store-unsafe";
  if (read.status == storage::StoreStatus::ReadFailed || read.status == storage::StoreStatus::TooLarge) return "watch-store-read-failed";
  if (read.status != storage::StoreStatus::Missing &&
      (read.status != storage::StoreStatus::Ok || read.version != 1 || !read.document.value("items").isArray())) return "watch-store-invalid-version-or-data";
  const auto parent = QFileInfo(path).absolutePath();
  const QFileInfo dir(parent);
  if ((dir.exists() && dir.ownerId() != uint(geteuid())) || !QDir().mkpath(parent)) return "watch-store-unsafe";
  if (!QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return "watch-store-permissions";
  const auto status = store.write({{"items", items}}, 1, *snapshot, snapshot);
  switch (status) {
    case storage::StoreStatus::Ok: return {};
    case storage::StoreStatus::Unsafe: return "watch-store-unsafe";
    case storage::StoreStatus::TooLarge: return "watch-store-full";
    default: return "watch-store-write-failed";
  }
}
}
WatchFolders::WatchFolders(QString directory) : m_directory(std::move(directory)) {
  QJsonArray rules, processed, publications;
  m_error = readRulesStore(QDir(m_directory).filePath("watch-folders.json"), &rules, &m_rulesSnapshot);
  if (m_error.isEmpty())
    m_error = readProcessedStore(QDir(m_directory).filePath("watch-processed.json"), &processed, &publications, &m_processedSnapshot, nullptr);
  if (!m_error.isEmpty()) return;
  if (rules.size() > maximumWatchRules) { m_error = "watch-store-full"; return; }
  QSet<QUuid> ids; QSet<QString> paths;
  for (const auto &value : rules) {
    const auto o = value.toObject(); Rule rule;
    bool valid = o.value("path").isString() && QDir::isAbsolutePath(o.value("path").toString()) &&
        o.value("filters").isArray() && o.value("enabled").isBool() && o.value("recursive").isBool() &&
        o.value("action").isDouble() && o.value("action").toInt(-1) >= 0 && o.value("action").toInt(-1) <= 2;
    rule.path = o.value("path").toString();
    rule.id = o.contains("id") ? QUuid(o.value("id").toString()) : QUuid::createUuidV5(QUuid("6ba7b811-9dad-11d1-80b4-00c04fd430c8"), rule.path);
    valid = valid && !rule.id.isNull() && !ids.contains(rule.id) && !paths.contains(rule.path);
    rule.filters.clear();
    const auto filters = o.value("filters").toArray(); valid = valid && !filters.isEmpty() && filters.size() <= maximumWatchFilters;
    for (const auto &filter : filters) {
      if (!filter.isString() || filter.toString().trimmed().isEmpty() || filter.toString().contains('/') || rule.filters.contains(filter.toString())) valid = false;
      rule.filters.append(filter.toString());
    }
    if (!valid) {
      m_unreadable.append(o.contains("_unreadableWatchRule") ? value : QJsonValue(QJsonObject{{"_unreadableWatchRule", value}}));
      m_diagnostics.append("watch-rule-unreadable"); continue;
    }
    rule.enabled = o.value("enabled").toBool(); rule.recursive = o.value("recursive").toBool(); rule.action = Action(o.value("action").toInt());
    ids.insert(rule.id); paths.insert(rule.path); m_rules.append(rule);
  }
  if (!rules.isEmpty() && m_rules.isEmpty()) { m_error = "watch-rule-invalid"; return; }
  for (const auto &value : processed) {
    if (!value.isString()) { m_error = "watch-checkpoint-invalid"; return; }
    m_processed.insert(value.toString());
  }
  QString publicationsError;
  m_publications = publicationsFrom(publications, &publicationsError);
  if (!publicationsError.isEmpty()) { m_error = publicationsError; return; }
  for (auto &record : m_publications) record.state = PublicationState::Unconfirmed;
}
QString WatchFolders::saveRules(const QVector<Rule> &rules, const QString &captures) {
  if (!m_error.isEmpty()) return m_error;
  if (rules.size() + m_unreadable.size() > maximumWatchRules) return "watch-store-full";
  QJsonArray items; QVector<Rule> normalized; QSet<QString> roots; QSet<QUuid> ids;
  for (auto rule : rules) {
    if (rule.id.isNull()) {
      for (const auto &saved : m_rules) if (saved.path == rule.path) { rule.id = saved.id; break; }
      if (rule.id.isNull()) rule.id = QUuid::createUuid();
    }
    const auto originalPath = rule.path;
    const auto error = authorizeRoot(rule.path, captures, &rule.path);
    bool retainedMissing = false;
    if (error == "watch-directory-required") for (const auto &saved : m_rules)
      if (saved.id == rule.id && saved.path == originalPath) { retainedMissing = true; break; }
    if (!error.isEmpty() && !retainedMissing) return error;
    for (const auto &root : roots)
      if (within(rule.path, root) || within(root, rule.path)) return "watch-overlapping-roots";
    roots.insert(rule.path);
    if (ids.contains(rule.id)) return "watch-duplicate-id";
    ids.insert(rule.id);
    if (rule.filters.isEmpty()) return "watch-filter-required";
    if (rule.filters.size() > maximumWatchFilters) return "watch-filter-limit";
    QSet<QString> filters;
    for (const auto &filter : rule.filters) { if (filters.contains(filter)) return "watch-filter-duplicate"; filters.insert(filter); }
    for (const auto &filter : rule.filters) if (filter.trimmed().isEmpty() || filter.contains('/')) return "watch-filter-invalid";
    if (int(rule.action) < 0 || int(rule.action) > 2) return "watch-action-invalid";
    items.append(QJsonObject{{"id", rule.id.toString(QUuid::WithoutBraces)}, {"path", rule.path}, {"filters", QJsonArray::fromStringList(rule.filters)}, {"recursive", rule.recursive}, {"enabled", rule.enabled}, {"action", int(rule.action)}});
    normalized.append(rule);
  }
  for (const auto &raw : m_unreadable) items.append(raw);
  const auto error = writeRulesStore(QDir(m_directory).filePath("watch-folders.json"), items, &m_rulesSnapshot);
  if (error.isEmpty()) m_rules = normalized;
  return error;
}
QString WatchFolders::remember(const QString &key) {
  if (!m_error.isEmpty()) return m_error;
  const auto write = checkpoint(key);
  return adoptCheckpoint(write, writeCheckpoint(write));
}
CheckpointWrite WatchFolders::checkpoint(const QString &key) const {
  return {QDir(m_directory).filePath("watch-processed.json"), m_processed, key, m_processedSnapshot, m_publications};
}
CheckpointResult WatchFolders::writeCheckpoint(const CheckpointWrite &write) {
  auto next = write.processed; next.insert(write.key);
  auto publications = write.publications;
  publications.removeIf([&](const PublicationRecord &record) { return record.key == write.key; });
  return persistCheckpoint(write.path, write.expected, std::move(next), std::move(publications));
}
QString WatchFolders::adoptCheckpoint(const CheckpointWrite &write, CheckpointResult result) {
  if (!result.error.isEmpty()) return result.error;
  if (m_processedSnapshot != write.expected) return QStringLiteral("watch-store-write-failed");
  m_processed = std::move(result.processed);
  m_publications = std::move(result.publications);
  m_processedSnapshot = result.snapshot;
  return {};
}
bool WatchFolders::publicationPending(const QString &key) const {
  for (const auto &record : m_publications) if (record.key == key) return true;
  return false;
}
CheckpointIntent WatchFolders::publicationIntent(const QString &key, const QString &at) const {
  return {QDir(m_directory).filePath("watch-processed.json"), m_processed, m_publications, key, m_processedSnapshot,
      at.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : at};
}
IntentResult WatchFolders::writeIntent(const CheckpointIntent &intent) {
  auto records = intent.publications;
  records.removeIf([&](const PublicationRecord &record) { return record.key == intent.key; });
  records.append({intent.key, PublicationState::Sending, intent.at});
  return persistCheckpoint(intent.path, intent.expected, intent.processed, std::move(records));
}
QString WatchFolders::adoptIntent(const CheckpointIntent &intent, IntentResult result) {
  if (!result.error.isEmpty()) return result.error;
  if (m_processedSnapshot != intent.expected) return QStringLiteral("watch-store-write-failed");
  m_processed = std::move(result.processed);
  m_publications = std::move(result.publications);
  m_processedSnapshot = result.snapshot;
  return {};
}
SettleRequest WatchFolders::settlePublication(const QString &key, PublicationDisposition disposition) const {
  return {QDir(m_directory).filePath("watch-processed.json"), m_processed, m_publications, key, disposition, m_processedSnapshot};
}
SettleResult WatchFolders::writeSettlement(const SettleRequest &request) {
  auto next = request.processed;
  if (request.disposition == PublicationDisposition::Processed) next.insert(request.key);
  auto records = request.publications;
  records.removeIf([&](const PublicationRecord &record) { return record.key == request.key; });
  if (request.disposition == PublicationDisposition::Unconfirmed)
    records.append({request.key, PublicationState::Unconfirmed, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)});
  return persistCheckpoint(request.path, request.expected, std::move(next), std::move(records));
}
QString WatchFolders::adoptSettlement(const SettleRequest &request, SettleResult result) {
  if (!result.error.isEmpty()) return result.error;
  if (m_processedSnapshot != request.expected) return QStringLiteral("watch-store-write-failed");
  m_processed = std::move(result.processed);
  m_publications = std::move(result.publications);
  m_processedSnapshot = result.snapshot;
  return {};
}
StabilityTracker::StabilityTracker(Clock clock) : m_clock(std::move(clock)) {
  if (!m_clock) { QElapsedTimer timer; timer.start(); m_clock = [timer] { return timer.elapsed(); }; }
}
bool StabilityTracker::ready(const QString &path) {
  return ready(path, fingerprint(path));
}
bool StabilityTracker::ready(const QString &path, const QString &key) {
  const auto now = m_clock();
  if (key.isEmpty()) { m_observed.remove(path); return false; }
  auto it = m_observed.find(path);
  if (it == m_observed.end() || it->key != key) { m_observed.insert(path, {key, now}); return false; }
  return now - it->since >= 2000;
}
void StabilityTracker::retain(const QSet<QString> &paths) {
  for (auto it = m_observed.begin(); it != m_observed.end();) if (!paths.contains(it.key())) it = m_observed.erase(it); else ++it;
}
}
