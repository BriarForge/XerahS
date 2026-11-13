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
namespace {
storage::PrivateJsonFile watchFile(const QString &path) {
  return {path, {QStringLiteral("com.xerahs.app/watch-folders"), 1, 16 * 1024 * 1024}};
}
QString readStoreSnapshot(const QString &path, QJsonArray *items, QByteArray *snapshot) {
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
  if (read.version != 1 || !read.document.value("items").isArray()) return "watch-store-invalid-version-or-data";
  *items = read.document.value("items").toArray();
  return {};
}
QString writeStoreSnapshot(const QString &path, const QJsonArray &items, QByteArray *snapshot) {
  const auto store = watchFile(path);
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
  QJsonArray rules, processed;
  m_error = readStoreSnapshot(QDir(m_directory).filePath("watch-folders.json"), &rules, &m_rulesSnapshot);
  if (m_error.isEmpty()) m_error = readStoreSnapshot(QDir(m_directory).filePath("watch-processed.json"), &processed, &m_processedSnapshot);
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
  const auto error = writeStoreSnapshot(QDir(m_directory).filePath("watch-folders.json"), items, &m_rulesSnapshot);
  if (error.isEmpty()) m_rules = normalized;
  return error;
}
QString WatchFolders::remember(const QString &key) {
  if (!m_error.isEmpty()) return m_error;
  const auto write = checkpoint(key);
  return adoptCheckpoint(write, writeCheckpoint(write));
}
CheckpointWrite WatchFolders::checkpoint(const QString &key) const {
  return {QDir(m_directory).filePath("watch-processed.json"), m_processed, key, m_processedSnapshot};
}
CheckpointResult WatchFolders::writeCheckpoint(const CheckpointWrite &write) {
  auto next = write.processed; next.insert(write.key); QJsonArray items;
  for (const auto &item : next) items.append(item);
  auto snapshot = write.expected;
  const auto error = writeStoreSnapshot(write.path, items, &snapshot);
  return {error, snapshot, std::move(next)};
}
QString WatchFolders::adoptCheckpoint(const CheckpointWrite &write, CheckpointResult result) {
  if (!result.error.isEmpty()) return result.error;
  if (m_processedSnapshot != write.expected) return QStringLiteral("watch-store-write-failed");
  m_processed = std::move(result.processed);
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
