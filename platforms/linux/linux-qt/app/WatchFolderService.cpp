// CORE-024, CORE-025.
#include "WatchFolderService.h"
#include "actions/PipelineResultJson.h"
#include <QDebug>
#include "FileUploadController.h"
#include "OwnedWork.h"
#include "NativeWorkflowExecutions.h"
#include <QDir>
#include <QEventLoop>
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
  connect(&m_uploads, &FileUploadController::executionFinished, this, [this](const QString &path, const QString &id, const QString &state, const QStringList &outputs) {
    if (path == m_pendingPath) {
      m_terminalState = state;
      m_terminalObservation = {{"task_id", id}, {"state", state}, {"outputs", QJsonArray::fromStringList(outputs)}};
    }
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
  ++m_discoveryGeneration; m_discovery.cancel(); m_rescanRequested = false;
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  m_uploads.cancel();
}
QString WatchFolderService::setRules(const QVector<watch::Rule> &rules) {
  const auto error = m_store.saveRules(rules, m_captures());
  if (error.isEmpty()) {
    ++m_discoveryGeneration; m_discovery.cancel();
    m_reported.clear(); rescan();
  }
  return error;
}
WatchFolderService::~WatchFolderService() { m_work.cancel(); m_discovery.cancel(); }
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
void WatchFolderService::setDiscoveryHook(std::function<void()> hook) { m_discoveryHook = std::move(hook); }
// Copy/move runs on an owned worker; the GUI thread only snapshots inputs and receives the verdict.
void WatchFolderService::startFileWork(const QString &sourcePath, const QString &key, const watch::Rule &rule) {
  const bool move = rule.action == watch::Action::Move;
  m_execution = std::make_unique<workflows::WorkflowExecution>(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("watch-folder"),
      workflows::fileOperationSnapshot(move ? "move" : "copy", {}, {}, ruleSnapshot(rule)),
      m_executionWriter ? m_executionWriter : NativeWorkflowExecutions(NativeWorkflowExecutions::defaultDirectory()).writer());
  if (!m_execution->admit() || !m_execution->start()) {
    m_terminalObservation = m_execution->recordedObservation();
    m_execution.reset();
    m_blocked = true;
    m_terminalState = QStringLiteral("permanent-failure");
    qInfo().noquote() << "Workflow terminal" << actions::pipelineObservationText(m_terminalObservation);
    complete(sourcePath, false, QStringLiteral("workflow-storage-unavailable: Watching stopped before file work; could not save the execution record."));
    return;
  }
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
  bool stored = true;
  const auto stepFailed = [&](const QString &action) {
    if (!m_execution) return;
    if (cancelled) stored = m_execution->step(action, QStringLiteral("cancelled")) && stored;
    else stored = m_execution->step(action, QStringLiteral("failed"), code, classifyFileOperationFailure(code)) && stored;
  };
  if (m_execution) {
    if (result.read) stored = m_execution->step(QStringLiteral("read-file"), QStringLiteral("succeeded")) && stored;
    if (result.copied) stored = m_execution->step(QStringLiteral("copy-file"), QStringLiteral("succeeded")) && stored;
    if (result.error.isEmpty() && operation == QLatin1String("move")) stored = m_execution->step(QStringLiteral("remove-source"), QStringLiteral("succeeded")) && stored;
    else if (!result.error.isEmpty()) stepFailed(!result.read ? (code == QLatin1String("watch-copy-open-failed") ? "read-file" : "copy-file") : !result.copied ? "copy-file" : "remove-source");
  }
  const auto steps = m_execution ? m_execution->record().steps : QJsonArray{};
  const auto terminal = fileOperationTerminal(m_execution ? m_execution->record().id : QString{}, operation, steps, result.error.isEmpty() ? QString{} : code, 0);
  m_terminalState = m_execution ? terminal.state : (result.error.isEmpty() ? QStringLiteral("succeeded") : (result.copied ? QStringLiteral("partial") : cancelled ? QStringLiteral("cancelled") : QStringLiteral("permanent-failure")));
  if (m_execution) { stored = m_execution->finish(terminal.state, terminal.diagnostic, terminal.outputs) && stored; m_terminalObservation = m_execution->recordedObservation(); m_execution.reset(); }
  QString error = result.error;
  if (!stored) {
    m_blocked = true;
    if (result.copied) m_terminalState = QStringLiteral("partial");
    if (!error.isEmpty()) error += QLatin1Char('\n');
    error += QStringLiteral("workflow-storage-unavailable: Watching stopped; could not save the execution record. Completed file work was retained.");
  }
  qInfo().noquote() << "Workflow terminal" << actions::pipelineObservationText(m_terminalObservation);
  complete(path, result.error.isEmpty(), error);
}
void WatchFolderService::complete(const QString &path, bool success, const QString &error) {
  if (path != m_pendingPath || path.isEmpty() || m_checkpointing) return;
  if (!success || !m_store.loadError().isEmpty()) {
    settle(path, success, error, success ? m_store.loadError() : QString{});
    return;
  }
  m_checkpointing = true;
  const auto write = m_store.checkpoint(m_pendingKey);
  const auto hook = m_checkpointHook;
  m_checkpoint = OwnedWork::run<watch::CheckpointResult>(this,
      [write, hook](const OwnedWork::Context &) -> watch::CheckpointResult {
    try {
      if (hook) hook();
      return watch::WatchFolders::writeCheckpoint(write);
    } catch (...) { return {QStringLiteral("watch-store-write-failed"), {}, {}}; }
  }, [this, key = write.key, expected = write.expected, path, success, error](watch::CheckpointResult result) {
    const auto saved = m_store.adoptCheckpoint({{}, {}, key, expected}, std::move(result));
    m_checkpointing = false;
    const QPointer<WatchFolderService> guard(this);
    settle(path, success, error, saved);
    if (guard) emit checkpointSettled();
  });
}
void WatchFolderService::setCheckpointHook(std::function<void()> hook) { m_checkpointHook = std::move(hook); }
void WatchFolderService::finishCheckpoint() {
  if (!m_checkpointing) return;
  QEventLoop loop;
  connect(this, &WatchFolderService::checkpointSettled, &loop, &QEventLoop::quit);
  connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::ExcludeUserInputEvents);
}
void WatchFolderService::settle(const QString &path, bool success, const QString &error, const QString &saved) {
  const QPointer<WatchFolderService> guard(this);
  if (!saved.isEmpty()) {
    m_blocked = true;
    emit notify(saved + ": watching stopped; completed action was not checkpointed.", false);
    if (!guard) return;
  }
  // A cancelled action (stop mid-processing) is not a verdict on the file: forget the attempt so it is discovered again.
  if (!success && (!m_running || error == QLatin1String("cancelled"))) m_attempted.remove(m_pendingKey);
  emit processed(path, success && (m_terminalState.isEmpty() || m_terminalState == QLatin1String("succeeded")));
  if (!guard) return;
  // WFCAT-017: the notification reads the terminal state, so a partial or failed
  // outcome can never be announced as processed.
  const auto notice = workflows::fileOperationNotice(QStringLiteral("watch"), m_terminalState.isEmpty() ? (success ? QStringLiteral("succeeded") : QStringLiteral("permanent-failure")) : m_terminalState, QFileInfo(path).fileName());
  if (!m_terminalObservation.isEmpty()) {
    const auto text = actions::pipelineObservationText(m_terminalObservation);
    emit activityChanged(text); if (!guard) return;
    emit terminalObserved(m_terminalObservation); if (!guard) return;
    emit notify(error.isEmpty() ? text : text + QLatin1Char('\n') + error, error.isEmpty() && m_terminalObservation.value("state") == "succeeded");
  } else emit notify(notice.completed ? notice.headline : (error.isEmpty() ? notice.headline : error + QLatin1Char('\n') + notice.headline), notice.completed);
  if (!guard) return;
  m_pendingPath.clear(); m_pendingKey.clear(); m_terminalState.clear(); m_terminalObservation = {};
}
void WatchFolderService::rescan() {
  if (!m_running || m_blocked) return;
  if (m_discovery.running()) { m_rescanRequested = true; return; }
  m_rescanRequested = false;
  struct Candidate { QString path, key; watch::Rule rule; };
  struct Discovery {
    QSet<QString> directories, files;
    QVector<Candidate> candidates;
    QVector<QPair<QString, QString>> reports;
    bool cancelled = false;
  };
  const auto generation = m_discoveryGeneration;
  const auto rules = m_store.rules();
  const auto capturesFolder = m_captures();
  const auto hook = m_discoveryHook;
  m_discovery = OwnedWork::run<Discovery>(this,
      [rules, capturesFolder, hook](const OwnedWork::Context &ctx) {
    Discovery result;
    const auto captures = QDir::cleanPath(QFileInfo(capturesFolder).absoluteFilePath());
    const auto canonicalCaptures = QFileInfo(captures).canonicalFilePath();
    const auto excluded = [&](const QString &path) {
      for (const auto &root : {captures, canonicalCaptures})
        if (!root.isEmpty() && (path == root || path.startsWith(root + '/'))) return true;
      return false;
    };
    for (const auto &rule : rules) {
      if (ctx.cancelled()) { result.cancelled = true; return result; }
      if (!rule.enabled) continue;
      QString root;
      const auto error = watch::authorizeRoot(rule.path, captures, &root);
      if (!error.isEmpty() || root != rule.path) {
        result.reports.append({rule.path, error.isEmpty() ? QStringLiteral("watch-root-changed") : error});
        continue;
      }
      QStringList pending{root};
      while (!pending.isEmpty()) {
        if (ctx.cancelled()) { result.cancelled = true; return result; }
        const auto directory = pending.takeLast();
        if (result.directories.contains(directory) || excluded(directory)) continue;
        const QFileInfo info(directory);
        if (info.isSymLink() || info.ownerId() != QFileInfo(root).ownerId() || !info.isReadable() || !info.isExecutable()) {
          result.reports.append({directory, QStringLiteral("watch-directory-denied")});
          continue;
        }
        result.directories.insert(directory);
        if (hook) hook();
        if (ctx.cancelled()) { result.cancelled = true; return result; }
        for (const auto &entry : QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name)) {
          if (ctx.cancelled()) { result.cancelled = true; return result; }
          if (entry.isSymLink() || entry.isHidden() || excluded(entry.absoluteFilePath())) continue;
          if (entry.isDir()) { if (rule.recursive) pending.append(entry.absoluteFilePath()); continue; }
          if (entry.ownerId() != info.ownerId() || !watch::matches(rule, entry.absoluteFilePath())) continue;
          const auto path = entry.absoluteFilePath(); result.files.insert(path);
          result.candidates.append({path, watch::fingerprint(path), rule});
        }
      }
    }
    return result;
  }, [this, generation, capturesFolder](Discovery result) {
    if (!m_running || m_blocked) return;
    if (generation != m_discoveryGeneration || capturesFolder != m_captures()) { rescan(); return; }
    if (result.cancelled) return;
    const QPointer<WatchFolderService> guard(this);
    const auto report = [&](const QString &key, const QString &error) {
      if (!m_reported.contains(key)) { m_reported.insert(key); emit notify(error, false); }
    };
    for (const auto &item : result.reports) {
      report(item.first, item.second);
      if (!guard || !m_running || generation != m_discoveryGeneration) return;
    }
    QVector<Candidate> ready;
    for (const auto &candidate : result.candidates)
      if (m_stability.ready(candidate.path, candidate.key)) ready.append(candidate);
    m_stability.retain(result.files);
    const auto watched = m_watcher.directories();
    const QSet<QString> watchedSet(watched.begin(), watched.end());
    QStringList remove, add;
    for (const auto &path : watched) if (!result.directories.contains(path)) remove.append(path);
    for (const auto &path : result.directories) if (!watchedSet.contains(path)) add.append(path);
    if (!remove.isEmpty()) m_watcher.removePaths(remove);
    if (!add.isEmpty()) for (const auto &path : m_watcher.addPaths(add)) {
      report(path + ":watcher", "watch-notification-unavailable; periodic scanning remains active");
      if (!guard || !m_running || generation != m_discoveryGeneration) return;
    }
    if (m_pendingPath.isEmpty()) for (const auto &candidate : ready) {
      const auto &key = candidate.key;
      if (key.isEmpty() || m_store.processed(key) || m_attempted.contains(key)) continue;
      // The worker's observation cannot authorize a file replaced before dispatch.
      if (watch::fingerprint(candidate.path) != key) continue;
      m_pendingPath = candidate.path; m_pendingKey = key; m_attempted.insert(key);
      if (candidate.rule.action == watch::Action::Upload)
        m_uploads.uploadFromWatchFolder(candidate.path, ruleSnapshot(candidate.rule));
      else startFileWork(candidate.path, key, candidate.rule);
      break;
    }
    if (guard && m_rescanRequested) rescan();
  });
}
}
