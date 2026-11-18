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
  // CORE-025 P1: the typed publication outcome decides the disposition.
  connect(&m_uploads, &FileUploadController::publicationResolved, this,
      [this](const QString &path, UploadPublication, int attempts, int answeredAttempts, int acceptedAttempts) {
    if (path != m_pendingPath || path.isEmpty()) return;
    m_uploadAttempts = attempts;
    m_answeredAttempts = answeredAttempts;
    m_acceptedAttempts = acceptedAttempts;
  });
}
void WatchFolderService::start() {
  if (m_running) return;
  m_running = true;
  if (!m_store.loadError().isEmpty()) { m_blocked = true; emit notify(m_store.loadError(), false); return; }
  for (const auto &diagnostic : m_store.diagnostics()) emit notify(diagnostic, false);
  // CORE-025 P2/P3: a leftover sending record from an earlier process is
  // unconfirmed. It is never uploaded again automatically under the same key,
  // and the startup notice names the files without disclosing any path.
  const QPointer<WatchFolderService> owner(this);
  const auto leftovers = m_store.publications();
  if (!leftovers.isEmpty()) {
    QStringList names;
    int remaining = 0;
    for (const auto &record : leftovers) {
      const auto last = record.key.lastIndexOf('\n');
      const auto split = last > 0 ? record.key.lastIndexOf('\n', last - 1) : -1;
      const auto name = QFileInfo(split >= 0 ? record.key.left(split) : record.key).fileName();
      if (names.size() < 5) {
        names.append(name);
      } else ++remaining;
    }
    if (!names.isEmpty()) {
      auto summary = names.join(QStringLiteral(", "));
      if (remaining > 0) summary += QStringLiteral(" and %1 more").arg(remaining);
      const auto notice = QStringLiteral("watch-publication-unconfirmed: %1 may already have been uploaded. Use Upload file to retry.")
          .arg(summary);
      emit notify(notice, false);
      if (!owner) return;
    }
  }
  m_timer.start(); rescan();
}
void WatchFolderService::stop() {
  // Cooperative only: the copy and discovery workers poll cancelled(); the
  // persistence and dispatch-check writers do not, so their cancel() requests
  // just record intent, and each continuation decides from m_running and the
  // generation instead.
  m_running = false; ++m_publicationGeneration; m_timer.stop(); m_work.cancel(); m_fileStorage.cancel(); m_dispatch.cancel();
  ++m_discoveryGeneration; m_discovery.cancel(); m_rescanRequested = false;
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  m_uploads.cancel();
}
QString WatchFolderService::setRules(const QVector<watch::Rule> &rules) {
  const auto error = m_store.saveRules(rules, m_captures());
  if (error.isEmpty()) {
    ++m_publicationGeneration;
    ++m_discoveryGeneration; m_discovery.cancel();
    m_reported.clear(); rescan();
  }
  return error;
}
WatchFolderService::~WatchFolderService() { m_work.cancel(); m_discovery.cancel(); m_fileStorage.cancel(); m_dispatch.cancel(); }
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
void WatchFolderService::setDispatchCheckHook(std::function<void(const QString &)> hook) { m_dispatchCheckHook = std::move(hook); }
// Copy/move runs on an owned worker; the GUI thread only snapshots inputs and receives the verdict.
// WFCAT-004: the execution record's admission, start, step and terminal
// persistence also run on owned work off the GUI thread. The pre-file fence is
// preserved: a failed admission/start write still stops before any file work
// starts, and the copy worker is launched only after admission and start are
// durably recorded. A stop or rule change landing while that write is in flight
// never starts file work; the record settles cancelled and the file stays
// discoverable. The terminal observation, notice and history signals are
// emitted only after the terminal write settled, keeping the durable terminal
// observation. The execution record is shared with the worker lambdas instead
// of unique, and one worker writes it at a time: the GUI thread never touches
// it while a persistence worker runs.
void WatchFolderService::startFileWork(const QString &sourcePath, const QString &key, const watch::Rule &rule) {
  const bool move = rule.action == watch::Action::Move;
  m_execution = std::make_shared<workflows::WorkflowExecution>(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("watch-folder"),
      workflows::fileOperationSnapshot(move ? "move" : "copy", {}, {}, ruleSnapshot(rule)),
      m_executionWriter ? m_executionWriter : NativeWorkflowExecutions(NativeWorkflowExecutions::defaultDirectory()).writer());
  const auto generation = m_publicationGeneration;
  struct Admission { bool stored = false; QJsonObject observation; };
  const auto execution = m_execution;
  m_fileStorage = OwnedWork::run<Admission>(this,
      [execution](const OwnedWork::Context &) { return Admission{execution->admit() && execution->start(), execution->recordedObservation()}; },
      [this, path = sourcePath, key, move, generation](Admission admitted) {
    if (!admitted.stored) {
      m_terminalObservation = admitted.observation;
      m_execution.reset();
      m_blocked = true;
      m_terminalState = QStringLiteral("permanent-failure");
      qInfo().noquote() << "Workflow terminal" << actions::pipelineObservationText(m_terminalObservation);
      complete(path, false, QStringLiteral("workflow-storage-unavailable: Watching stopped before file work; could not save the execution record."));
      return;
    }
    // A stop or rule change that landed while the write was in flight never
    // starts file work; the record settles cancelled with no step action and
    // the file stays discoverable.
    if (!m_running || generation != m_publicationGeneration) {
      finishFileWork(path, {QStringLiteral("cancelled"), false, false, /*beforeFileWork=*/true});
      return;
    }
    startCopyWorker(path, key, move);
  });
}
void WatchFolderService::startCopyWorker(const QString &sourcePath, const QString &key, bool move) {
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
// WFCAT-004: the terminal step and finish persistence run on owned work off
// the GUI thread; the terminal state, observation, notice and history signals
// are emitted on the GUI thread only after the terminal write settled.
void WatchFolderService::finishFileWork(const QString &path, const FileResult &result) {
  using namespace workflows;
  // All inputs are frozen on the GUI thread; the worker owns the record until
  // its write settles.
  const auto execution = m_execution;
  const auto operation = execution ? execution->record().settings.value("operation").toString() : QStringLiteral("copy");
  const auto code = fileOperationDiagnostic(result.error);
  const bool cancelled = code == QLatin1String("cancelled");
  struct TerminalPersistence { bool stored = true; QString state; QJsonObject observation; };
  m_fileStorage = OwnedWork::run<TerminalPersistence>(this,
      [execution, result, operation, code, cancelled](const OwnedWork::Context &) {
    TerminalPersistence persisted;
    bool stored = true;
    const auto stepFailed = [&](const QString &action) {
      if (!execution) return;
      if (cancelled) stored = execution->step(action, QStringLiteral("cancelled")) && stored;
      else stored = execution->step(action, QStringLiteral("failed"), code, classifyFileOperationFailure(code)) && stored;
    };
    if (execution) {
      if (result.beforeFileWork) {
        // WFCAT-006 accurate cancellation: the interrupt landed before any file
        // work began, so no step action ran and none is recorded.
        persisted.state = QStringLiteral("cancelled");
        stored = execution->finish(QStringLiteral("cancelled"), QStringLiteral("cancelled"), {}, /*noSyntheticStep=*/true) && stored;
      } else {
        if (result.read) stored = execution->step(QStringLiteral("read-file"), QStringLiteral("succeeded")) && stored;
        if (result.copied) stored = execution->step(QStringLiteral("copy-file"), QStringLiteral("succeeded")) && stored;
        if (result.error.isEmpty() && operation == QLatin1String("move")) stored = execution->step(QStringLiteral("remove-source"), QStringLiteral("succeeded")) && stored;
        else if (!result.error.isEmpty()) stepFailed(!result.read ? (code == QLatin1String("watch-copy-open-failed") ? "read-file" : "copy-file") : !result.copied ? "copy-file" : "remove-source");
        const auto steps = execution->record().steps;
        const auto terminal = fileOperationTerminal(execution->record().id, operation, steps, result.error.isEmpty() ? QString{} : code, 0);
        persisted.state = terminal.state;
        stored = execution->finish(terminal.state, terminal.diagnostic, terminal.outputs) && stored;
      }
      persisted.observation = execution->recordedObservation();
    } else
      persisted.state = result.error.isEmpty() ? QStringLiteral("succeeded")
          : (result.copied ? QStringLiteral("partial") : cancelled ? QStringLiteral("cancelled") : QStringLiteral("permanent-failure"));
    persisted.stored = stored;
    return persisted;
  }, [this, path, result, execution](TerminalPersistence persisted) {
    if (execution) m_terminalObservation = persisted.observation;
    m_execution.reset();
    m_terminalState = persisted.state;
    QString error = result.error;
    if (!persisted.stored) {
      m_blocked = true;
      if (result.copied) m_terminalState = QStringLiteral("partial");
      if (!error.isEmpty()) error += QLatin1Char('\n');
      error += QStringLiteral("workflow-storage-unavailable: Watching stopped; could not save the execution record.");
      if (!result.beforeFileWork) error += QStringLiteral(" Completed file work was retained.");
    }
    qInfo().noquote() << "Workflow terminal" << actions::pipelineObservationText(m_terminalObservation);
    // CORE-025 P4: local copy and move are unchanged. They never enter the
    // publication disposition path.
    complete(path, result.error.isEmpty(), error);
  });
}
// CORE-025 P2: the send intent is durably saved before any HTTP request is
// issued. When the intent cannot be saved, no request is sent and watching
// stops with watch-store-write-failed.
void WatchFolderService::beginPublication(const QString &path, const QString &key, const QJsonObject &rule) {
  const auto generation = m_publicationGeneration;
  const auto at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  const auto intent = m_store.publicationIntent(key, at);
  const auto hook = m_checkpointHook;
  const auto dispatchHook = m_dispatchCheckHook;
  m_checkpointing = true;
  // CORE-024: the final re-check that the file was not replaced runs on the
  // intent worker, after the intent write settles and before any dispatch; the
  // GUI thread never hashes the file.
  struct IntentCheck { watch::IntentResult result; bool matches = false; };
  m_checkpoint = OwnedWork::run<IntentCheck>(this,
      [intent, hook, dispatchHook, path, key](const OwnedWork::Context &) -> IntentCheck {
    IntentCheck check;
    try {
      if (hook) hook();
      check.result = watch::WatchFolders::writeIntent(intent);
    } catch (...) { check.result = {QStringLiteral("watch-store-write-failed"), {}, {}, {}, {}}; }
    // A failed intent never dispatches, so the re-check is only earned on success.
    if (check.result.error.isEmpty()) {
      if (dispatchHook) dispatchHook(path);
      check.matches = watch::fingerprint(path) == key;
    }
    return check;
  }, [this, intent, path, rule, generation](IntentCheck check) {
    const auto &result = check.result;
    // CORE-025 P2: an intent that cannot be saved is always reported as
    // watch-store-write-failed, whichever store error caused it.
    const auto failed = !result.error.isEmpty();
    const auto saved = failed ? QString() : m_store.adoptIntent(intent, std::move(check.result));
    m_checkpointing = false;
    const QPointer<WatchFolderService> guard(this);
    if (!saved.isEmpty()) {
      // CORE-025 P2: zero HTTP requests, and watching stops.
      m_blocked = true;
      emit notify(saved + QStringLiteral(": watching stopped; the upload was not sent because its publication could not be saved."), false);
      if (guard) {
        // The operation ends without a publication: the file was never sent.
        settle(path, false, saved, {});
        if (guard) emit checkpointSettled();
      }
      return;
    }
    if (failed) {
      // CORE-025 P2: zero HTTP requests, and watching stops.
      m_blocked = true;
      emit notify(QStringLiteral("watch-store-write-failed: watching stopped; the upload was not sent because its publication could not be saved."), false);
      if (guard) {
        settle(path, false, QStringLiteral("watch-store-write-failed"), {});
        if (guard) emit checkpointSettled();
      }
      return;
    }
    if (!guard) return;
    // The worker's observation cannot authorize a file replaced before dispatch.
    if (!m_running || generation != m_publicationGeneration || !check.matches) {
      writeSettlement(path, watch::PublicationDisposition::Released, QStringLiteral("cancelled"));
      return;
    }
    m_uploads.uploadFromWatchFolder(path, rule);
    if (guard) emit checkpointSettled();
  });
}
void WatchFolderService::announceUnconfirmed(const QString &path) {
  emit notify(QStringLiteral("watch-publication-unconfirmed: %1 may already have been uploaded. Use Upload file to retry.")
      .arg(QFileInfo(path).fileName()), false);
}
void WatchFolderService::writeSettlement(const QString &path, watch::PublicationDisposition disposition, const QString &error) {
  const auto request = m_store.settlePublication(m_pendingKey, disposition);
  const auto hook = m_checkpointHook;
  m_checkpointing = true;
  m_checkpoint = OwnedWork::run<watch::SettleResult>(this,
      [request, hook](const OwnedWork::Context &) -> watch::SettleResult {
    try {
      if (hook) hook();
      return watch::WatchFolders::writeSettlement(request);
    } catch (...) { return {QStringLiteral("watch-store-write-failed"), {}, {}, {}, {}}; }
  }, [this, expected = request.expected, path, disposition, error](watch::SettleResult result) {
    const auto saved = m_store.adoptSettlement({{}, {}, {}, {}, disposition, expected, {}}, std::move(result));
    m_checkpointing = false;
    const QPointer<WatchFolderService> guard(this);
    if (disposition == watch::PublicationDisposition::Unconfirmed) {
      announceUnconfirmed(path);
      if (!guard) return;
    }
    settle(path, false, error, saved);
    if (guard) emit checkpointSettled();
  });
}
void WatchFolderService::complete(const QString &path, bool success, const QString &error) {
  if (path != m_pendingPath || path.isEmpty() || m_checkpointing) return;
  // CORE-025 P1: the disposition decides what happens to a publication. A copy
  // or move is not a publication and keeps its existing checkpoint behavior.
  if (m_publicationAction) {
    watch::PublicationFacts facts;
    facts.requestStarted = m_uploadAttempts > 0;
    facts.attempts = m_uploadAttempts;
    facts.everyAttemptAnswered = m_uploadAttempts > 0 && m_answeredAttempts == m_uploadAttempts;
    facts.acceptedOnAnyAttempt = m_acceptedAttempts > 0;
    facts.succeeded = success;
    facts.restartWithSending = false;
    const auto disposition = watch::publicationDisposition(facts);
    if (disposition != watch::PublicationDisposition::Processed) {
      // A released or unconfirmed publication never checkpoints as processed.
      writeSettlement(path, disposition, error);
      return;
    }
  }
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
    } catch (...) { return {QStringLiteral("watch-store-write-failed"), {}, {}, {}, {}}; }
  }, [this, key = write.key, expected = write.expected, path, success, error](watch::CheckpointResult result) {
    const auto saved = m_store.adoptCheckpoint({{}, {}, key, expected, {}, {}}, std::move(result));
    m_checkpointing = false;
    const QPointer<WatchFolderService> guard(this);
    settle(path, success, error, saved);
    if (!guard) return;
    emit checkpointSettled();
  });
}
void WatchFolderService::setCheckpointHook(std::function<void()> hook) { m_checkpointHook = std::move(hook); }
void WatchFolderService::finishCheckpoint() {
  const QPointer<WatchFolderService> owner(this);
  while (owner && (m_checkpointing || !m_pendingPath.isEmpty())) {
    QEventLoop loop;
    bool settled = false;
    connect(this, &WatchFolderService::checkpointSettled, &loop, [&] { settled = true; loop.quit(); });
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    // Application exit interrupts nested loops; main drains again after exec.
    if (!settled) return;
  }
}
void WatchFolderService::settle(const QString &path, bool success, const QString &error, const QString &saved) {
  const QPointer<WatchFolderService> guard(this);
  if (!saved.isEmpty()) {
    m_blocked = true;
    emit notify(saved + ": watching stopped; completed action was not checkpointed.", false);
    if (!guard) return;
  }
  // Started publications remain attempted until process exit; unsent work can be discovered again.
  if (!success && !m_store.publicationPending(m_pendingKey) &&
      !(m_publicationAction && m_uploadAttempts > 0) &&
      (!m_running || error == QLatin1String("cancelled"))) m_attempted.remove(m_pendingKey);
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
  emit checkpointSettled();
}
void WatchFolderService::rescan() {
  if (!m_running || m_blocked) return;
  if (m_discovery.running()) { m_rescanRequested = true; return; }
  m_rescanRequested = false;
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
    if (m_pendingPath.isEmpty() && !m_checkpointing && !m_dispatching) selectNextCandidate(ready, generation, capturesFolder);
    if (guard && m_rescanRequested) rescan();
  });
}
// CORE-024: the final dispatch fingerprint runs on owned work off the GUI
// thread; the GUI thread never hashes a candidate file. One candidate is
// checked at a time, and nothing is dispatched until the check settles. A
// stop, rule change or capture-folder change that lands while a check is in
// flight abandons the selection; the file stays discoverable. A candidate
// whose fingerprint no longer matches is refused exactly as before, and the
// next ready candidate is checked instead.
void WatchFolderService::selectNextCandidate(QVector<Candidate> ready, quint64 generation, const QString &capturesFolder) {
  while (!ready.isEmpty()) {
    const auto candidate = ready.takeFirst();
    const auto &key = candidate.key;
    if (key.isEmpty() || m_store.processed(key) || m_attempted.contains(key)) continue;
    // CORE-025 P2: a publication left over from an earlier process is
    // unconfirmed and is never uploaded again automatically under this key.
    if (m_store.publicationPending(key)) continue;
    m_dispatching = true;
    const auto hook = m_dispatchCheckHook;
    m_dispatch = OwnedWork::run<bool>(this,
        [path = candidate.path, key, hook](const OwnedWork::Context &) {
          // The worker's observation cannot authorize a file replaced before dispatch.
          if (hook) hook(path);
          return watch::fingerprint(path) == key;
        },
        [this, tail = ready, candidate, generation, capturesFolder](bool matches) {
      m_dispatching = false;
      // An interrupt that landed while the check was in flight ends the
      // selection; the next rescan re-discovers the file.
      if (!m_running || m_blocked || generation != m_discoveryGeneration || capturesFolder != m_captures()) return;
      if (!matches) { selectNextCandidate(tail, generation, capturesFolder); return; }
      m_pendingPath = candidate.path; m_pendingKey = candidate.key; m_attempted.insert(candidate.key);
      m_publicationAction = candidate.rule.action == watch::Action::Upload;
      m_uploadAttempts = 0; m_answeredAttempts = 0; m_acceptedAttempts = 0;
      if (candidate.rule.action == watch::Action::Upload) {
        // CORE-025 P2: the send intent is durably saved before any dispatch.
        beginPublication(candidate.path, candidate.key, ruleSnapshot(candidate.rule));
      } else startFileWork(candidate.path, candidate.key, candidate.rule);
    });
    return;
  }
}
}
