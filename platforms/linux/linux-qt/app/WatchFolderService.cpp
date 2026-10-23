// CORE-024, CORE-025.
#include "WatchFolderService.h"
#include "FileUploadController.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

namespace xerahs::app {
WatchFolderService::WatchFolderService(watch::WatchFolders &store, FileUploadController &uploads,
    std::function<QString()> captures, watch::StabilityTracker::Clock clock, QObject *parent)
    : QObject(parent), m_store(store), m_uploads(uploads), m_captures(std::move(captures)), m_stability(std::move(clock)) {
  m_timer.setInterval(500);
  connect(&m_timer, &QTimer::timeout, this, &WatchFolderService::rescan);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { rescan(); });
  connect(&m_uploads, &FileUploadController::fileCompleted, this, &WatchFolderService::complete);
}
void WatchFolderService::start() {
  if (m_running) return;
  m_running = true;
  if (!m_store.loadError().isEmpty()) { m_blocked = true; emit notify(m_store.loadError(), false); return; }
  m_timer.start(); rescan();
}
void WatchFolderService::stop() {
  m_running = false; m_timer.stop();
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  m_uploads.cancel();
}
QString WatchFolderService::setRules(const QVector<watch::Rule> &rules) {
  const auto error = m_store.saveRules(rules, m_captures());
  if (error.isEmpty()) { m_reported.clear(); rescan(); }
  return error;
}
void WatchFolderService::complete(const QString &path, bool success, const QString &error) {
  if (path != m_pendingPath || path.isEmpty()) return;
  if (success) {
    const auto saved = m_store.remember(m_pendingKey);
    if (!saved.isEmpty()) { m_blocked = true; emit notify(saved + ": watching stopped; completed action was not checkpointed.", false); }
  }
  emit processed(path, success);
  emit notify(success ? QStringLiteral("Processed %1").arg(QFileInfo(path).fileName()) : error, success);
  m_pendingPath.clear(); m_pendingKey.clear();
}
void WatchFolderService::rescan() {
  if (!m_running || m_blocked) return;
  QSet<QString> directories, files;
  struct Candidate { QString path; watch::Action action; };
  QVector<Candidate> candidates;
  const auto captures = QDir::cleanPath(QFileInfo(m_captures()).absoluteFilePath());
  const auto canonicalCaptures = QFileInfo(captures).canonicalFilePath();
  const auto excluded = [&](const QString &path) {
    for (const auto &root : {captures, canonicalCaptures})
      if (!root.isEmpty() && (path == root || path.startsWith(root + '/'))) return true;
    return false;
  };
  const auto report = [&](const QString &key, const QString &error) {
    if (!m_reported.contains(key)) { m_reported.insert(key); emit notify(error, false); }
  };
  for (const auto &rule : m_store.rules()) {
    if (!rule.enabled) continue;
    QString root;
    const auto error = watch::authorizeRoot(rule.path, captures, &root);
    if (!error.isEmpty() || root != rule.path) { report(rule.path, error.isEmpty() ? "watch-root-changed" : error); continue; }
    QStringList pending{root};
    while (!pending.isEmpty()) {
      const auto directory = pending.takeLast();
      if (directories.contains(directory) || excluded(directory)) continue;
      const QFileInfo info(directory);
      if (info.isSymLink() || info.ownerId() != QFileInfo(root).ownerId() || !info.isReadable() || !info.isExecutable()) { report(directory, "watch-directory-denied"); continue; }
      directories.insert(directory);
      for (const auto &entry : QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name)) {
        if (entry.isSymLink() || entry.isHidden() || excluded(entry.absoluteFilePath())) continue;
        if (entry.isDir()) { if (rule.recursive) pending.append(entry.absoluteFilePath()); continue; }
        if (entry.ownerId() != info.ownerId() || !watch::matches(rule, entry.absoluteFilePath())) continue;
        const auto path = entry.absoluteFilePath(); files.insert(path);
        if (m_stability.ready(path)) candidates.append({path, rule.action});
      }
    }
  }
  m_stability.retain(files);
  const auto watched = m_watcher.directories();
  QStringList remove, add;
  for (const auto &path : watched) if (!directories.contains(path)) remove.append(path);
  for (const auto &path : directories) if (!watched.contains(path)) add.append(path);
  if (!remove.isEmpty()) m_watcher.removePaths(remove);
  if (!add.isEmpty()) for (const auto &path : m_watcher.addPaths(add)) report(path + ":watcher", "watch-notification-unavailable; periodic scanning remains active");
  if (!m_pendingPath.isEmpty()) return;
  for (const auto &candidate : candidates) {
    const auto key = watch::fingerprint(candidate.path);
    if (key.isEmpty() || m_store.processed(key) || m_attempted.contains(key)) continue;
    m_pendingPath = candidate.path; m_pendingKey = key; m_attempted.insert(key);
    if (candidate.action == watch::Action::Upload) { m_uploads.upload({candidate.path}); return; }
    const auto targetDirectory = m_captures();
    QString error;
    if (!QDir().mkpath(targetDirectory)) error = "watch-capture-directory-failed";
    const auto target = QDir(targetDirectory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + '-' + QFileInfo(candidate.path).fileName());
    QFile source(candidate.path); QSaveFile output(target); output.setDirectWriteFallback(false);
    if (error.isEmpty() && (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly) || !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner))) error = "watch-copy-open-failed";
    while (error.isEmpty() && !source.atEnd()) {
      const auto chunk = source.read(1024 * 1024);
      if (chunk.isEmpty() || output.write(chunk) != chunk.size()) error = "watch-copy-failed";
    }
    if (error.isEmpty() && watch::fingerprint(candidate.path) != key) error = "watch-file-changed";
    if (error.isEmpty() && !output.commit()) error = "watch-copy-commit-failed";
    source.close();
    if (error.isEmpty() && candidate.action == watch::Action::Move && !QFile::remove(candidate.path)) error = "watch-move-remove-failed; copy retained";
    complete(candidate.path, error.isEmpty(), error);
    return;
  }
}
}
