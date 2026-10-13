#include "CapturePipeline.h"
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
namespace xerahs::app {
using namespace xerahs::actions;
QJsonObject CapturePipelineResult::canonicalResult() const {
  return pipelineResultJson(pipeline, file ? std::vector<SavedFileArtifact>{*file}
                                         : std::vector<SavedFileArtifact>{});
}
PipelineRetryPlan CapturePipelineResult::retryPlan() const {
  return planRetainedCaptureRetry(pipeline, !workingMedia.isNull(), bool(file));
}
namespace {
struct ClipboardCall {
  QImage media;
  bool claimed = false;
  bool done = false;
  ActionOutcome result;
};
ActionOutcome cancelled() { return {ActionState::Cancelled, {}, {}}; }
QString actionActivity(const QString &id) {
  if (id == "save") return QStringLiteral("Saving capture. Escape or Cancel Capture stops later actions.");
  if (id == "image-clipboard") return QStringLiteral("Copying capture to the clipboard.");
  return QStringLiteral("Running capture action.");
}
}
struct CapturePipelineState {
  CancellationToken cancellation;
  std::mutex mutex;
  std::condition_variable changed;
  std::shared_ptr<ClipboardCall> clipboard;
  std::shared_ptr<CapturePipelineResult> result = std::make_shared<CapturePipelineResult>();
  QString activity;
  bool ready = false;
};
namespace {
class JournaledExecutor final : public ActionExecutor {
public:
  JournaledExecutor(CaptureActions &actions, PipelineJournalRecorder &journal) : m_actions(actions), m_journal(journal) {}
  ActionOutcome run(const ActionRequest &request) override {
    if (!m_journal.before(request.spec)) return {ActionState::Failed, QStringLiteral("pipeline-journal-unavailable"), {}};
    return m_actions.run(request);
  }
  bool createTemporaryUploadFile() override { return m_actions.createTemporaryUploadFile(); }
  void removeTemporaryUploadFile() override { m_actions.removeTemporaryUploadFile(); }
private:
  CaptureActions &m_actions;
  PipelineJournalRecorder &m_journal;
};
}
CapturePipeline::CapturePipeline(QImage media, QString directory, QString pattern, QStringList selected,
                                 QObject *parent, CaptureActionPorts ports, PipelineJournalStorage journal)
    : QObject(parent), m_state(std::make_shared<CapturePipelineState>()), m_media(std::move(media)),
      m_directory(std::move(directory)), m_pattern(std::move(pattern)), m_selected(std::move(selected)), m_ports(std::move(ports)), m_journal(std::move(journal)) {
  qRegisterMetaType<CapturePipelineReceipt>();
  m_timer = new QTimer(this); m_timer->setInterval(10); connect(m_timer, &QTimer::timeout, this, &CapturePipeline::poll);
}
CapturePipeline::CapturePipeline(CapturePipelineReceipt source, QObject *parent, CaptureActionPorts ports, PipelineJournalStorage journal)
    : CapturePipeline(source ? source->workingMedia : QImage{}, source ? source->saveDirectory : QString{},
                      source ? source->filenamePattern : QString{}, {}, parent, std::move(ports), std::move(journal)) {
  m_retryRequested = true; m_retrySource = std::move(source);
}
CapturePipeline::~CapturePipeline() {
  m_timer->stop();
  { std::lock_guard<std::mutex> lock(m_state->mutex); m_state->cancellation.request(); }
  m_state->changed.notify_all();
}
void CapturePipeline::start() {
  if (m_started) return;
  m_started = true;
  const QPointer<CapturePipeline> guard(this);
  emit activityChanged(QStringLiteral("Preparing capture actions."));
  if (!guard) return;
  const auto state = m_state;
  auto ports = m_ports;
  const auto externalStart = ports.actionStarted;
  ports.actionStarted = [state, externalStart](const QString &id) {
    { std::lock_guard<std::mutex> lock(state->mutex); state->activity = actionActivity(id); }
    if (externalStart) externalStart(id);
  };
  // This bridge shares no GUI objects with the worker. The GUI claims a call
  // in poll(); cancellation/owner destruction releases the worker's wait and
  // guarantees an unclaimed request cannot mutate the clipboard later.
  ports.clipboard = [state](const QImage &media, const CancellationToken &token) {
    if (token.requested()) return cancelled();
    auto call = std::make_shared<ClipboardCall>(); call->media = media;
    std::unique_lock<std::mutex> lock(state->mutex); state->clipboard = call;
    state->changed.wait(lock, [&] { return call->done || (token.requested() && !call->claimed); });
    return call->done ? call->result : cancelled();
  };
  auto *worker = QThread::create([state, ports, media = m_media, directory = m_directory,
                                  pattern = m_pattern, selected = m_selected, storage = m_journal,
                                  retryRequested = m_retryRequested, source = m_retrySource]() mutable {
    state->result->workingMedia = media;
    state->result->saveDirectory = directory; state->result->filenamePattern = pattern;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto retry = source ? source->retryPlan() : PipelineRetryPlan{};
    if (retryRequested && !retry.available()) {
      state->result->pipeline.taskId = id; // rejected admission: no action started
      state->result->pipeline.workingMediaRetained = !media.isNull();
      state->result->diagnostic = retry.diagnostic.isEmpty() ? QStringLiteral("retry-invalid-source") : retry.diagnostic;
      std::lock_guard<std::mutex> lock(state->mutex); state->ready = true; return;
    }
    if (retryRequested) selected = retry.selected;
    auto inheritedFile = source ? source->file : std::optional<CaptureFile>{};
    if (inheritedFile && inheritedFile->creatorTaskId.isEmpty()) inheritedFile->creatorTaskId = source->pipeline.taskId;
    PipelineJournalRecorder journal(storage, id, selected, QDateTime::currentDateTimeUtc(), retry.link,
                                    inheritedFile ? std::vector<SavedFileArtifact>{*inheritedFile} : std::vector<SavedFileArtifact>{});
    journal.begin(); // retry lineage/files precede any new external action
    const auto externalPublish = ports.save.beforePublish;
    ports.save.beforePublish = [&journal, externalPublish](const QString &path) {
      if (!journal.beforePublish(path)) throw std::runtime_error("pipeline-journal-unavailable");
      if (externalPublish) externalPublish(path);
    };
    CaptureActions actions(media, directory, pattern, ports, inheritedFile); JournaledExecutor executor(actions, journal);
    const auto currentFiles = [&actions, id] {
      auto file = actions.savedFile(); if (!file) return std::vector<SavedFileArtifact>{};
      if (file->creatorTaskId.isEmpty()) file->creatorTaskId = id;
      return std::vector<SavedFileArtifact>{*file};
    };
    PipelineHooks hooks;
    hooks.actionFinished = [state, &journal, currentFiles](const ActionResult &result) {
      state->result->pipeline.orderedResults.push_back(result); journal.completed(result, currentFiles());
    };
    try { state->result->pipeline = runPipeline(id, selected, {}, executor, state->cancellation, hooks); }
    catch (...) {
      state->result->pipeline.taskId = id; state->result->pipeline.state = PipelineState::Interrupted;
      state->result->diagnostic = diagnostic::actionFailed;
    }
    state->result->pipeline.retry = retry.link;
    const auto files = currentFiles(); if (!files.empty()) state->result->file = files.front();
    journal.finish(state->result->pipeline, files);
    if (!journal.diagnostic().isEmpty()) state->result->diagnostic = journal.diagnostic();
    { std::lock_guard<std::mutex> lock(state->mutex); state->ready = true; }
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start(); m_timer->start();
}
void CapturePipeline::cancel() {
  if (m_finished) return;
  { std::lock_guard<std::mutex> lock(m_state->mutex); m_state->cancellation.request(); }
  m_state->changed.notify_all();
  emit activityChanged(QStringLiteral("Cancellation requested. Finishing the current action safely."));
}
void CapturePipeline::poll() {
  if (m_finished) return;
  const auto state = m_state;
  QString activity;
  { std::lock_guard<std::mutex> lock(state->mutex); activity = state->activity; }
  const QPointer<CapturePipeline> guard(this);
  if (!activity.isEmpty() && activity != m_activity && !state->cancellation.requested()) {
    m_activity = activity; emit activityChanged(activity); if (!guard) return;
  }
  std::shared_ptr<ClipboardCall> call;
  { std::lock_guard<std::mutex> lock(state->mutex); call = std::move(state->clipboard); if (call) call->claimed = true; }
  if (call) {
    ActionOutcome result;
    const auto clipboard = m_ports.clipboard;
    try { result = state->cancellation.requested() ? cancelled() : (clipboard ? clipboard(call->media, state->cancellation) : CaptureActions::copyImageToNativeClipboard(call->media)); }
    catch (...) { result = {ActionState::Failed, diagnostic::actionFailed, {}}; }
    { std::lock_guard<std::mutex> lock(state->mutex); call->result = std::move(result); call->done = true; }
    state->changed.notify_all();
    if (!guard) return;
  }
  bool ready;
  { std::lock_guard<std::mutex> lock(state->mutex); ready = state->ready; }
  if (!ready) return;
  m_timer->stop(); m_finished = true;
  emit finished(CapturePipelineReceipt(state->result));
}
} // namespace xerahs::app
