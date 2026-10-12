#include "CapturePipeline.h"
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <condition_variable>
#include <mutex>
namespace xerahs::app {
using namespace xerahs::actions;
QJsonObject CapturePipelineResult::canonicalResult() const {
  return pipelineResultJson(pipeline, file ? std::vector<SavedFileArtifact>{*file}
                                         : std::vector<SavedFileArtifact>{});
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
CapturePipeline::CapturePipeline(QImage media, QString directory, QString pattern, QStringList selected,
                                 QObject *parent, CaptureActionPorts ports)
    : QObject(parent), m_state(std::make_shared<CapturePipelineState>()), m_media(std::move(media)),
      m_directory(std::move(directory)), m_pattern(std::move(pattern)), m_selected(std::move(selected)), m_ports(std::move(ports)) {
  qRegisterMetaType<CapturePipelineReceipt>();
  m_timer = new QTimer(this); m_timer->setInterval(10); connect(m_timer, &QTimer::timeout, this, &CapturePipeline::poll);
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
                                  pattern = m_pattern, selected = m_selected] {
    state->result->workingMedia = media;
    CaptureActions actions(media, directory, pattern, ports);
    PipelineHooks hooks;
    hooks.actionFinished = [state](const ActionResult &result) { state->result->pipeline.orderedResults.push_back(result); };
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    try { state->result->pipeline = runPipeline(id, selected, {}, actions, state->cancellation, hooks); }
    catch (...) {
      state->result->pipeline.taskId = id; state->result->pipeline.state = PipelineState::Interrupted;
      state->result->diagnostic = diagnostic::actionFailed;
    }
    state->result->file = actions.savedFile();
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
