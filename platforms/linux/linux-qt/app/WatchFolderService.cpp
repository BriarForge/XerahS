// CORE-024, CORE-025.
#include "WatchFolderService.h"
#include "FileUploadController.h"
#include "OwnedWork.h"
#include "NativeWorkflowExecutions.h"
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
  connect(&m_uploads, &FileUploadController::executionFinished, this, [this](const QString &path, const QString &, const QString &state, const QStringList &) {
    if (path == m_pendingPath) m_terminalState = state;
  });
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
void WatchFolderService::setBeforeRemoveHook(std::function<void(const QString &)> hook) { m_removeHook = std::move(hook); }
void WatchFolderService::setExecutionWriter(workflows::WorkflowExecution::Writer writer) { m_executionWriter = std::move(writer); }
namespace {
QJsonObject ruleSnapshot(const watch::Rule &rule) {
  // Redacted: the watched path is private and is not part of the snapshot.
  return {{"action", rule.action == watch::Action::Upload ? "upload" : rule.action == watch::Action::Copy ? "copy" : "move"},
      {"recursive", rule.recursive}, {"filters", QJsonArray::fromStringList(rule.filters)}};
}
}
void WatchFolderService::setFileOperationHook(std::function<void(const QString &)> hook) { m_fileHook = std::move(hook); }
// Copy/move runs on an owned worker; the GUI thread only snapshots inputs and receives the verdict.
void WatchFolderService::startFileWork(const QString &sourcePath, const QString &key, const watch::Rule &rule) {
  const bool move = rule.action == watch::Action::Move;
  m_execution = std::make_unique<workflows::WorkflowExecution>(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("watch-folder"),
      workflows::fileOperationSnapshot(move ? "move" : "copy", {}, {}, ruleSnapshot(rule)),
      m_executionWriter ? m_executionWriter : NativeWorkflowExecutions(NativeWorkflowExecutions::defaultDirectory()).writer());
  if (!m_execution->admit() || !m_execution->start()) m_execution.reset();  // storage failure never blocks the file action
  const auto directory = m_captures();
  const auto hook = m_fileHook, removeHook = m_removeHook;
  const auto path = sourcePath;
  m_work = OwnedWork::run<FileResult>(this, std::function<FileResult(const OwnedWork::Context &)>(
      [directory, hook, removeHook, path, key, move](const OwnedWork::Context &ctx) -> FileResult {
    FileResult r;
    const auto fail = [&r](const QString &code) { r.error = code; return r; };
    if (!QDir().mkpath(directory)) return fail(QStringLiteral("watch-capture-directory-failed"));
    const auto target = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + '-' + QFileInfo(path).fileName());
    QFile source(path); QSaveFile output(target); output.setDirectWriteFallback(false);
    if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly) || !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner))
      return fail(QStringLiteral("watch-copy-open-failed"));
    r.read = true;
    const auto total = source.size(); qint64 copied = 0;
    while (!source.atEnd()) {
      if (ctx.cancelled()) { output.cancelWriting(); return fail(QStringLiteral("cancelled")); }
      if (hook) hook(path);
      const auto chunk = source.read(1024 * 1024);
      if (chunk.isEmpty() || output.write(chunk) != chunk.size()) { output.cancelWriting(); return fail(QStringLiteral("watch-copy-failed")); }
      copied += chunk.size(); ctx.progress(copied, total);
    }
    if (ctx.cancelled()) { output.cancelWriting(); return fail(QStringLiteral("cancelled")); }
    if (watch::fingerprint(path) != key) { output.cancelWriting(); return fail(QStringLiteral("watch-file-changed")); }
    if (!output.commit()) return fail(QStringLiteral("watch-copy-commit-failed"));
    r.copied = true;  // durable: the copy exists before the source is touched
    source.close();
    if (move && removeHook) removeHook(path);
    if (move && ctx.cancelled()) return fail(QStringLiteral("cancelled"));
    if (move && !QFile::remove(path)) return fail(QStringLiteral("watch-move-remove-failed; copy retained"));
    return r;
  }), std::function<void(FileResult)>([this, path](const FileResult &result) { finishFileWork(path, result); }),
  std::function<void(qint64, qint64)>([this, path](qint64 done, qint64 total) { emit progress(path, done, total); }));
}
void WatchFolderService::finishFileWork(const QString &path, const FileResult &result) {
  using namespace workflows;
  const auto operation = m_execution ? m_execution->record().settings.value("operation").toString() : QStringLiteral("copy");
  const auto code = fileOperationDiagnostic(result.error);
  const bool cancelled = code == QLatin1String("cancelled");
  const auto stepFailed = [&](const QString &action) {
    if (!m_execution) return;
    if (cancelled) m_execution->step(action, QStringLiteral("cancelled"));
    else m_execution->step(action, QStringLiteral("failed"), code, classifyFileOperationFailure(code));
  };
  if (m_execution) {
    if (result.read) m_execution->step(QStringLiteral("read-file"), QStringLiteral("succeeded"));
    if (result.copied) m_execution->step(QStringLiteral("copy-file"), QStringLiteral("succeeded"));
    if (result.error.isEmpty() && operation == QLatin1String("move")) m_execution->step(QStringLiteral("remove-source"), QStringLiteral("succeeded"));
    else if (!result.error.isEmpty()) stepFailed(!result.read ? (code == QLatin1String("watch-copy-open-failed") ? "read-file" : "copy-file") : !result.copied ? "copy-file" : "remove-source");
  }
  const auto steps = m_execution ? m_execution->record().steps : QJsonArray{};
  const auto terminal = fileOperationTerminal(m_execution ? m_execution->record().id : QString{}, operation, steps, result.error.isEmpty() ? QString{} : code);
  m_terminalState = m_execution ? terminal.state : (result.error.isEmpty() ? QStringLiteral("succeeded") : (result.copied ? QStringLiteral("partial") : cancelled ? QStringLiteral("cancelled") : QStringLiteral("permanent-failure")));
  if (m_execution) { m_execution->finish(terminal.state, terminal.diagnostic, terminal.outputs); m_execution.reset(); }
  complete(path, result.error.isEmpty(), result.error);
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
  // WFCAT-017: the notification reads the terminal state, so a partial or failed
  // outcome can never be announced as processed.
  const auto notice = workflows::fileOperationNotice(QStringLiteral("watch"), m_terminalState.isEmpty() ? (success ? QStringLiteral("succeeded") : QStringLiteral("permanent-failure")) : m_terminalState, QFileInfo(path).fileName());
  emit notify(notice.completed ? notice.headline : (error.isEmpty() ? notice.headline : error + QLatin1Char('\n') + notice.headline), notice.completed);
  m_pendingPath.clear(); m_pendingKey.clear(); m_terminalState.clear();
}
void WatchFolderService::rescan() {
  if (!m_running || m_blocked) return;
  QSet<QString> directories, files;
  struct Candidate { QString path; watch::Rule rule; };
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
        if (m_stability.ready(path)) candidates.append({path, rule});
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
    if (candidate.rule.action == watch::Action::Upload) { m_uploads.uploadFromWatchFolder(candidate.path, ruleSnapshot(candidate.rule)); return; }
    startFileWork(candidate.path, key, candidate.rule);
    return;
  }
}
}
