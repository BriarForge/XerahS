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
  for (const auto &value : rules) {
    const auto o = value.toObject();
    if (!o.value("path").isString() || !o.value("filters").isArray() || !o.value("enabled").isBool() || !o.value("recursive").isBool() || o.value("action").toInt(-1) < 0 || o.value("action").toInt() > 2) { m_error = "watch-rule-invalid"; m_rules.clear(); return; }
    Rule rule; rule.path = o.value("path").toString(); rule.filters.clear();
    for (const auto &filter : o.value("filters").toArray()) {
      if (!filter.isString() || filter.toString().isEmpty()) { m_error = "watch-filter-invalid"; return; }
      rule.filters.append(filter.toString());
    }
    rule.enabled = o.value("enabled").toBool(); rule.recursive = o.value("recursive").toBool(); rule.action = Action(o.value("action").toInt());
    m_rules.append(rule);
  }
  for (const auto &value : processed) {
    if (!value.isString()) { m_error = "watch-checkpoint-invalid"; return; }
    m_processed.insert(value.toString());
  }
}
QString WatchFolders::saveRules(const QVector<Rule> &rules, const QString &captures) {
  if (!m_error.isEmpty()) return m_error;
  QJsonArray items; QVector<Rule> normalized; QSet<QString> roots;
  for (auto rule : rules) {
    const auto error = authorizeRoot(rule.path, captures, &rule.path);
    if (!error.isEmpty()) return error;
    for (const auto &root : roots)
      if (within(rule.path, root) || within(root, rule.path)) return "watch-overlapping-roots";
    roots.insert(rule.path);
    if (rule.filters.isEmpty()) return "watch-filter-required";
    for (const auto &filter : rule.filters) if (filter.trimmed().isEmpty() || filter.contains('/')) return "watch-filter-invalid";
    if (int(rule.action) < 0 || int(rule.action) > 2) return "watch-action-invalid";
    items.append(QJsonObject{{"path", rule.path}, {"filters", QJsonArray::fromStringList(rule.filters)}, {"recursive", rule.recursive}, {"enabled", rule.enabled}, {"action", int(rule.action)}});
    normalized.append(rule);
  }
  const auto error = writeStoreSnapshot(QDir(m_directory).filePath("watch-folders.json"), items, &m_rulesSnapshot);
  if (error.isEmpty()) m_rules = normalized;
  return error;
}
QString WatchFolders::remember(const QString &key) {
  if (!m_error.isEmpty()) return m_error;
  auto next = m_processed; next.insert(key); QJsonArray items;
  for (const auto &item : next) items.append(item);
  const auto error = writeStoreSnapshot(QDir(m_directory).filePath("watch-processed.json"), items, &m_processedSnapshot);
  if (error.isEmpty()) m_processed = std::move(next);
  return error;
}
StabilityTracker::StabilityTracker(Clock clock) : m_clock(std::move(clock)) {
  if (!m_clock) { QElapsedTimer timer; timer.start(); m_clock = [timer] { return timer.elapsed(); }; }
}
bool StabilityTracker::ready(const QString &path) {
  const auto key = fingerprint(path); const auto now = m_clock();
  if (key.isEmpty()) { m_observed.remove(path); return false; }
  auto it = m_observed.find(path);
  if (it == m_observed.end() || it->key != key) { m_observed.insert(path, {key, now}); return false; }
  return now - it->since >= 2000;
}
void StabilityTracker::retain(const QSet<QString> &paths) {
  for (auto it = m_observed.begin(); it != m_observed.end();) if (!paths.contains(it.key())) it = m_observed.erase(it); else ++it;
}
}
