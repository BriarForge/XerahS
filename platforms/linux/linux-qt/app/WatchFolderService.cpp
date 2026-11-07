// CORE-024, CORE-025.
#include "WatchFolderService.h"
#include "FileUploadController.h"
#include "OwnedWork.h"
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
  for (const auto &diagnostic : m_store.diagnostics()) emit notify(diagnostic, false);
  m_timer.start(); rescan();
}
void WatchFolderService::stop() {
  m_running = false; m_timer.stop(); m_work.cancel();
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  m_uploads.cancel();
}
QString WatchFolderService::setRules(const QVector<watch::Rule> &rules) {
  const auto error = m_store.saveRules(rules, m_captures());
  if (error.isEmpty()) { m_reported.clear(); rescan(); }
  return error;
}
WatchFolderService::~WatchFolderService() { m_work.cancel(); }
void WatchFolderService::setFileOperationHook(std::function<void(const QString &)> hook) { m_fileHook = std::move(hook); }
// Copy/move runs on an owned worker; the GUI thread only snapshots inputs and receives the verdict.
void WatchFolderService::startFileWork(const QString &sourcePath, const QString &key, bool move) {
  const auto directory = m_captures();
  const auto hook = m_fileHook;
  const auto path = sourcePath;
  m_work = OwnedWork::run<QString>(this, std::function<QString(const OwnedWork::Context &)>(
      [directory, hook, path, key, move](const OwnedWork::Context &ctx) -> QString {
    if (!QDir().mkpath(directory)) return QStringLiteral("watch-capture-directory-failed");
    const auto target = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + '-' + QFileInfo(path).fileName());
    QFile source(path); QSaveFile output(target); output.setDirectWriteFallback(false);
    if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly) || !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner))
      return QStringLiteral("watch-copy-open-failed");
    const auto total = source.size(); qint64 copied = 0;
    while (!source.atEnd()) {
      if (ctx.cancelled()) { output.cancelWriting(); return QStringLiteral("cancelled"); }
      if (hook) hook(path);
      const auto chunk = source.read(1024 * 1024);
      if (chunk.isEmpty() || output.write(chunk) != chunk.size()) { output.cancelWriting(); return QStringLiteral("watch-copy-failed"); }
      copied += chunk.size(); ctx.progress(copied, total);
    }
    if (ctx.cancelled()) { output.cancelWriting(); return QStringLiteral("cancelled"); }
    if (watch::fingerprint(path) != key) { output.cancelWriting(); return QStringLiteral("watch-file-changed"); }
    if (!output.commit()) return QStringLiteral("watch-copy-commit-failed");
    source.close();
    if (move && !QFile::remove(path)) return QStringLiteral("watch-move-remove-failed; copy retained");
    return {};
  }), std::function<void(QString)>([this, path](const QString &error) { complete(path, error.isEmpty(), error); }),
  std::function<void(qint64, qint64)>([this, path](qint64 done, qint64 total) { emit progress(path, done, total); }));
}
void WatchFolderService::complete(const QString &path, bool success, const QString &error) {
  if (path != m_pendingPath || path.isEmpty()) return;
  if (success) {
    const auto saved = m_store.remember(m_pendingKey);
    if (!saved.isEmpty()) { m_blocked = true; emit notify(saved + ": watching stopped; completed action was not checkpointed.", false); }
  }
  // A cancelled action (stop mid-processing) is not a verdict on the file: forget the attempt so it is discovered again.
  if (!success && (!m_running || error == QLatin1String("cancelled"))) m_attempted.remove(m_pendingKey);
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
    if (candidate.action == watch::Action::Upload) { m_uploads.uploadFromWatchFolder(candidate.path); return; }
    startFileWork(candidate.path, key, candidate.action == watch::Action::Move);
    return;
  }
}
}
