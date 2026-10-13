#include "CapturePipeline.h"
#include "ActionSelectionDialog.h"
#include "CaptureFileOutput.h"
#include "CaptureSaveAsDialog.h"
#include "CaptureAnnotationDialog.h"
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
  std::vector<SavedFileArtifact> facts; for (const auto &saved : savedFiles()) facts.push_back(saved);
  return pipelineResultJson(pipeline, facts);
}
std::vector<CaptureFile> CapturePipelineResult::savedFiles() const { return files.empty() && file ? std::vector<CaptureFile>{*file} : files; }
PipelineRetryPlan CapturePipelineResult::retryPlan() const {
  if (pipeline.finalWorkingMedia == "annotation" && !annotationDocument) return {{}, {}, "retry-media-unavailable"};
  bool save = false, saveAs = false;
  for (const auto &saved : savedFiles()) { save |= saved.artifact == "save.file"; saveAs |= saved.artifact == "save-as.file"; }
  return planRetainedCaptureRetry(pipeline, !workingMedia.isNull(), save, saveAs);
}
namespace {
struct ClipboardCall {
  QImage media;
  bool claimed = false;
  bool done = false;
  ActionOutcome result;
};
struct SelectionCall { bool done = false; ActionOutcome result; };
struct FileOutputCall { QString action; std::optional<CaptureFile> file; bool claimed = false, done = false; ActionOutcome result; };
struct SaveAsCall { QString directory; bool done = false; CaptureSaveAsChoice choice; };
struct AnnotationCall { LoadedSource source; bool done = false; CaptureAnnotationChoice choice; };
void completeSelection(const std::shared_ptr<CapturePipelineState> &state, const std::shared_ptr<SelectionCall> &call, ActionOutcome result);
ActionOutcome cancelled() { return {ActionState::Cancelled, {}, {}}; }
QString actionActivity(const QString &id) {
  if (id == "annotation") return "Preparing capture annotation. Cancel Capture stops later actions.";
  if (id == "save") return QStringLiteral("Saving capture. Escape or Cancel Capture stops later actions.");
  if (id == "save-as") return QStringLiteral("Choose a new PNG path. Cancel skips Save As; Cancel Capture stops later actions.");
  if (id == "image-clipboard") return QStringLiteral("Copying capture to the clipboard.");
  if (id == "after-capture-window") return QStringLiteral("Choose actions for this capture. Escape cancels before saving or copying.");
  if (id == "file-clipboard") return QStringLiteral("Copying the saved file to the clipboard.");
  if (id == "path-clipboard") return QStringLiteral("Copying the saved file path to the clipboard.");
  if (id == "reveal") return QStringLiteral("Showing the saved file in your file manager.");
  return QStringLiteral("Running capture action.");
}
}
struct CapturePipelineState {
  CancellationToken cancellation;
  std::mutex mutex;
  std::condition_variable changed;
  std::shared_ptr<ClipboardCall> clipboard;
  std::shared_ptr<SelectionCall> selection;
  std::shared_ptr<FileOutputCall> fileOutput;
  std::shared_ptr<SaveAsCall> saveAs;
  std::shared_ptr<AnnotationCall> annotation;
  std::shared_ptr<CapturePipelineResult> result = std::make_shared<CapturePipelineResult>();
  QString activity;
  bool ready = false;
};
namespace {
void completeAnnotation(const std::shared_ptr<CapturePipelineState> &state, const std::shared_ptr<AnnotationCall> &call, CaptureAnnotationChoice choice) {
  { std::lock_guard<std::mutex> lock(state->mutex); if (call->done) return; call->choice = std::move(choice); call->done = true; }
  state->changed.notify_all();
}
void completeSaveAs(const std::shared_ptr<CapturePipelineState> &state, const std::shared_ptr<SaveAsCall> &call, CaptureSaveAsChoice choice) {
  { std::lock_guard<std::mutex> lock(state->mutex); if (call->done) return; call->choice = std::move(choice); call->done = true; }
  state->changed.notify_all();
}
void completeFileOutput(const std::shared_ptr<CapturePipelineState> &state, const std::shared_ptr<FileOutputCall> &call, ActionOutcome outcome) {
  { std::lock_guard<std::mutex> lock(state->mutex); if (call->done) return; call->result = std::move(outcome); call->done = true; }
  state->changed.notify_all();
}
void completeSelection(const std::shared_ptr<CapturePipelineState> &state, const std::shared_ptr<SelectionCall> &call, ActionOutcome result) {
  { std::lock_guard<std::mutex> lock(state->mutex);
    if (call->done) return;
    call->result = state->cancellation.requested() ? cancelled() : std::move(result); call->done = true;
  }
  state->changed.notify_all();
}
class JournaledExecutor final : public ActionExecutor {
public:
  JournaledExecutor(CaptureActions &actions, PipelineJournalRecorder &journal) : m_actions(actions), m_journal(journal) {}
  ActionOutcome run(const ActionRequest &request) override {
    if (!m_journal.before(request.spec)) return {ActionState::Failed, QStringLiteral("pipeline-journal-unavailable"), {}};
    auto outcome = m_actions.run(request);
    if (outcome.outcomeUnknown) m_interruptionDiagnostic = outcome.diagnostic.value_or(diagnostic::actionFailed);
    return outcome;
  }
  bool createTemporaryUploadFile() override { return m_actions.createTemporaryUploadFile(); }
  void removeTemporaryUploadFile() override { m_actions.removeTemporaryUploadFile(); }
  const QString &interruptionDiagnostic() const { return m_interruptionDiagnostic; }
private:
  CaptureActions &m_actions;
  PipelineJournalRecorder &m_journal;
  QString m_interruptionDiagnostic;
};
}
CapturePipeline::CapturePipeline(QImage media, QString directory, QString pattern, QStringList selected,
                                 QObject *parent, CaptureActionPorts ports, PipelineJournalStorage journal, QWidget *selectionParent)
    : QObject(parent), m_state(std::make_shared<CapturePipelineState>()), m_media(std::move(media)),
      m_directory(std::move(directory)), m_pattern(std::move(pattern)), m_selected(std::move(selected)), m_ports(std::move(ports)), m_journal(std::move(journal)), m_selectionParent(selectionParent) {
  qRegisterMetaType<CapturePipelineReceipt>();
  m_timer = new QTimer(this); m_timer->setInterval(10); connect(m_timer, &QTimer::timeout, this, &CapturePipeline::poll);
}
CapturePipeline::CapturePipeline(CapturePipelineReceipt source, QObject *parent, CaptureActionPorts ports, PipelineJournalStorage journal, QWidget *selectionParent)
    : CapturePipeline(source ? source->workingMedia : QImage{}, source ? source->saveDirectory : QString{},
                      source ? source->filenamePattern : QString{}, {}, parent, std::move(ports), std::move(journal), selectionParent) {
  m_retryRequested = true; m_retrySource = std::move(source);
}
CapturePipeline::~CapturePipeline() {
  m_timer->stop();
  { std::lock_guard<std::mutex> lock(m_state->mutex); m_state->cancellation.request(); }
  m_state->changed.notify_all();
  if (m_selectionDialog) { disconnect(m_selectionDialog, nullptr, this, nullptr); delete m_selectionDialog.data(); }
  if (m_saveAsDialog) { disconnect(m_saveAsDialog, nullptr, this, nullptr); delete m_saveAsDialog.data(); }
  if (m_annotationDialog) { disconnect(m_annotationDialog, nullptr, this, nullptr); delete m_annotationDialog.data(); }
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
  ports.selection = [state](const CancellationToken &token) {
    if (token.requested()) return cancelled();
    auto call = std::make_shared<SelectionCall>();
    std::unique_lock<std::mutex> lock(state->mutex); state->selection = call;
    state->changed.wait(lock, [&] { return call->done || token.requested(); });
    return token.requested() ? cancelled() : call->result;
  };
  ports.fileOutput = [state](const QString &action, const CaptureFile &file, const CancellationToken &token) {
    if (token.requested()) return cancelled();
    auto call = std::make_shared<FileOutputCall>(); call->action = action; call->file = file;
    std::unique_lock<std::mutex> lock(state->mutex); state->fileOutput = call;
    state->changed.wait(lock, [&] { return call->done || (token.requested() && !call->claimed); });
    return call->done ? call->result : cancelled();
  };
  ports.chooseSaveAs = [state](const QString &directory, const CancellationToken &token) {
    if (token.requested()) return CaptureSaveAsChoice{};
    auto call = std::make_shared<SaveAsCall>(); call->directory = directory;
    std::unique_lock<std::mutex> lock(state->mutex); state->saveAs = call;
    state->changed.wait(lock, [&] { return call->done || token.requested(); });
    if (!token.requested() && call->choice.path) state->activity = QStringLiteral("Saving capture to the chosen PNG path. Cancel Capture stops publication at the next safe boundary.");
    return token.requested() ? CaptureSaveAsChoice{} : call->choice;
  };
  ports.annotation = [state](const LoadedSource &source, const CancellationToken &token) {
    if (token.requested()) return CaptureAnnotationChoice{};
    auto call = std::make_shared<AnnotationCall>(); call->source = source;
    std::unique_lock<std::mutex> lock(state->mutex); state->annotation = call; state->activity = "Annotate capture. Cancel skips annotation; Cancel Capture stops later actions.";
    state->changed.wait(lock, [&] { return call->done || token.requested(); });
    if (!token.requested() && call->choice.document) state->activity = "Rendering accepted annotations. Cancel Capture prevents adoption at the next safe boundary.";
    return token.requested() ? CaptureAnnotationChoice{} : call->choice;
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
    auto inheritedFiles = source ? source->savedFiles() : std::vector<CaptureFile>{};
    std::vector<SavedFileArtifact> initialFacts;
    for (auto &file : inheritedFiles) { if (file.creatorTaskId.isEmpty()) file.creatorTaskId = source->pipeline.taskId; initialFacts.push_back(file); }
    PipelineJournalRecorder journal(storage, id, selected, QDateTime::currentDateTimeUtc(), retry.link,
                                    initialFacts, source ? source->pipeline.finalWorkingMedia : QStringLiteral("capture"));
    journal.begin(); // retry lineage/files precede any new external action
    const auto externalPublish = ports.save.beforePublish;
    ports.save.beforePublish = [&journal, externalPublish](const QString &path) {
      if (!journal.beforePublish(path)) throw std::runtime_error("pipeline-journal-unavailable");
      if (externalPublish) externalPublish(path);
    };
    CaptureActions actions(media, directory, pattern, ports, {}, inheritedFiles, source ? source->annotationDocument : std::nullopt); JournaledExecutor executor(actions, journal);
    const auto currentFiles = [&actions, id] {
      std::vector<SavedFileArtifact> facts;
      for (auto file : actions.savedFiles()) { if (file.creatorTaskId.isEmpty()) file.creatorTaskId = id; facts.push_back(file); }
      return facts;
    };
    PipelineHooks hooks;
    hooks.selectionChanged = [&journal](const QStringList &effective) { journal.selectionChanged(effective); };
    hooks.actionFinished = [state, &journal, currentFiles](const ActionResult &result) {
      state->result->pipeline.orderedResults.push_back(result); journal.completed(result, currentFiles());
    };
    PipelineOptions options;
    if (source) options.inheritedWorkingMedia = source->pipeline.finalWorkingMedia;
    if (retryRequested) for (const auto &file : inheritedFiles) {
      options.inheritedSaveAvailable |= file.artifact == "save.file"; options.inheritedSaveAsAvailable |= file.artifact == "save-as.file";
    }
    try { state->result->pipeline = runPipeline(id, selected, options, executor, state->cancellation, hooks); }
    catch (...) {
      state->result->pipeline.taskId = id; state->result->pipeline.state = PipelineState::Interrupted;
      state->result->diagnostic = diagnostic::actionFailed;
    }
    state->result->pipeline.retry = retry.link;
    state->result->workingMedia = actions.workingMedia(); state->result->annotationDocument = actions.annotationDocument();
    if (!executor.interruptionDiagnostic().isEmpty()) state->result->diagnostic = executor.interruptionDiagnostic();
    // Retain the live native binding; canonical/journal facts deliberately
    // slice it away and must never reconstruct authority from metadata.
    state->result->files = actions.savedFiles();
    for (auto &file : state->result->files) if (file.creatorTaskId.isEmpty()) file.creatorTaskId = id;
    if (!state->result->files.empty()) state->result->file = state->result->files.front();
    const auto files = currentFiles();
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
  const QPointer<CapturePipeline> guard(this);
  if (m_selectionDialog) m_selectionDialog->reject();
  if (!guard) return;
  if (m_saveAsDialog) m_saveAsDialog->reject();
  if (!guard) return;
  if (m_annotationDialog) m_annotationDialog->reject();
  if (!guard) return;
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
  std::shared_ptr<AnnotationCall> annotation;
  { std::lock_guard<std::mutex> lock(state->mutex); annotation = std::move(state->annotation); }
  if (annotation) {
    if (state->cancellation.requested()) completeAnnotation(state, annotation, {});
    else {
      emit choosingActions(); if (!guard) return;
      if (state->cancellation.requested()) completeAnnotation(state, annotation, {});
      else if (const auto native = m_ports.annotation) {
        try { completeAnnotation(state, annotation, native(annotation->source, state->cancellation)); }
        catch (...) { completeAnnotation(state, annotation, {{}, diagnostic::actionFailed}); }
        if (!guard) return;
      } else {
        auto *dialog = new CaptureAnnotationDialog(annotation->source, m_selectionParent, m_ports.annotationStyle); m_annotationDialog = dialog;
        const auto parent = m_selectionParent;
        connect(dialog, &QDialog::finished, this, [state, annotation, dialog, parent](int result) {
          const CaptureAnnotationChoice choice = result == QDialog::Accepted ? CaptureAnnotationChoice{dialog->document(), {}} : CaptureAnnotationChoice{};
          const auto ownedState = state; const auto ownedCall = annotation;
          if (parent) parent->activateWindow();
          completeAnnotation(ownedState, ownedCall, choice);
        });
        connect(dialog, &QObject::destroyed, this, [state, annotation] { completeAnnotation(state, annotation, {}); });
        dialog->show(); if (!guard || !m_annotationDialog) return;
        dialog->raise(); if (!guard || !m_annotationDialog) return;
        dialog->activateWindow(); if (!guard) return;
      }
    }
  }
  std::shared_ptr<SaveAsCall> saveAs;
  { std::lock_guard<std::mutex> lock(state->mutex); saveAs = std::move(state->saveAs); }
  if (saveAs) {
    if (state->cancellation.requested()) completeSaveAs(state, saveAs, {});
    else {
      emit choosingActions(); if (!guard) return;
      if (state->cancellation.requested()) completeSaveAs(state, saveAs, {});
      else if (const auto chooser = m_ports.chooseSaveAs) {
        try { completeSaveAs(state, saveAs, chooser(saveAs->directory, state->cancellation)); }
        catch (...) { completeSaveAs(state, saveAs, {{}, diagnostic::actionFailed}); }
        if (!guard) return;
      } else {
        auto *dialog = new CaptureSaveAsDialog(saveAs->directory, m_selectionParent); m_saveAsDialog = dialog;
        const auto parent = m_selectionParent;
        connect(dialog, &QDialog::finished, this, [state, saveAs, dialog, parent](int result) {
          const CaptureSaveAsChoice choice = result == QDialog::Accepted ? CaptureSaveAsChoice{dialog->targetPath(), {}} : CaptureSaveAsChoice{};
          const auto ownedState = state; const auto ownedCall = saveAs;
          if (parent) parent->activateWindow();
          completeSaveAs(ownedState, ownedCall, choice);
        });
        connect(dialog, &QObject::destroyed, this, [state, saveAs] { completeSaveAs(state, saveAs, {}); });
        dialog->show(); if (!guard || !m_saveAsDialog) return;
        dialog->raise(); if (!guard || !m_saveAsDialog) return;
        dialog->activateWindow(); if (!guard) return;
      }
    }
  }
  std::shared_ptr<SelectionCall> selection;
  { std::lock_guard<std::mutex> lock(state->mutex); selection = std::move(state->selection); }
  if (selection) {
    if (state->cancellation.requested()) completeSelection(state, selection, cancelled());
    else {
      // The controller releases capture overlays before this application-modal
      // window claims focus. No nested event loop or GUI access on the worker.
      emit choosingActions(); if (!guard) return;
      if (state->cancellation.requested()) completeSelection(state, selection, cancelled());
      else {
        auto *dialog = new ActionSelectionDialog(m_selected, m_media.size(), m_selectionParent); m_selectionDialog = dialog;
        const QPointer<QWidget> selectionParent(m_selectionParent);
        connect(dialog, &QDialog::finished, this, [state, selection, dialog, selectionParent](int result) {
          auto outcome = result == QDialog::Accepted
              ? ActionOutcome{ActionState::Succeeded, {}, dialog->selectedActions()} : cancelled();
          const auto ownedState = state; const auto ownedCall = selection;
          // Restore the transient owner before acknowledging selection to the
          // worker. Clipboard dispatch still belongs to the GUI thread.
          if (selectionParent) selectionParent->activateWindow();
          completeSelection(ownedState, ownedCall, std::move(outcome));
        });
        connect(dialog, &QObject::destroyed, this, [state, selection] { completeSelection(state, selection, cancelled()); });
        dialog->show(); if (!guard || !m_selectionDialog) return;
        dialog->raise(); if (!guard || !m_selectionDialog) return;
        dialog->activateWindow(); if (!guard) return;
      }
    }
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
  std::shared_ptr<FileOutputCall> output;
  { std::lock_guard<std::mutex> lock(state->mutex); output = std::move(state->fileOutput); if (output) output->claimed = true; }
  if (output) {
    const auto native = m_ports.fileOutput;
    try {
      const auto validation = validateCaptureFile(*output->file);
      if (state->cancellation.requested()) completeFileOutput(state, output, cancelled());
      else if (!validation.isEmpty()) completeFileOutput(state, output, {ActionState::Failed, validation, {}});
      else if (native) completeFileOutput(state, output, native(output->action, *output->file, state->cancellation));
      else if (output->action == "reveal") revealCaptureFile(*output->file, this, [state, output](ActionOutcome result) { completeFileOutput(state, output, std::move(result)); });
      else completeFileOutput(state, output, copyCaptureFile(output->action, *output->file));
    } catch (...) { completeFileOutput(state, output, {ActionState::Failed, diagnostic::actionFailed, {}}); }
    if (!guard) return;
  }
  bool ready;
  { std::lock_guard<std::mutex> lock(state->mutex); ready = state->ready; }
  if (!ready) return;
  m_timer->stop(); m_finished = true;
  emit finished(CapturePipelineReceipt(state->result));
}
} // namespace xerahs::app
