#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "EditorSave.h"
#include "EditorSaveDialog.h"
#include <QFileInfo>
#include "EditorShutdown.h"
#include "EditorSource.h"
#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
namespace xerahs::app {
using namespace xerahs::actions;
void CaptureAnnotationDialog::enableTaskMode(const xerahs::editor::EditorConveniencePreferences &preferences, EditorCopyPorts copy) {
  if (m_taskState != TaskState::Inactive) return;
  m_taskState = TaskState::Editing; m_taskPreferences = preferences; m_taskCopy = std::move(copy);
  guardEditorShutdown(*this);
  findChild<QLabel *>("captureAnnotationIntro")->setText("Continue sends one rendered image to the remaining capture actions. Cancel stops the task without saving, copying or uploading. Dirty close always offers Save, Discard and Cancel.");
  findChild<QPushButton *>("applyCaptureAnnotation")->setText("Continue");
  // Fit runs after layout has a viewport. No document/history changes.
  if (preferences.zoomToFit) QTimer::singleShot(0, m_canvas, &EditorCanvas::zoomToFit);
}
void CaptureAnnotationDialog::continueTask() {
  if (m_taskState != TaskState::Editing || !editingReady() || m_taskConfirming) return;
  const QPointer<CaptureAnnotationDialog> owner(this);
  const auto document = m_session.document(); const auto source = m_image;
  const auto state = m_session.stateId(), generation = m_canvas->documentGeneration();
  const auto job = ++m_taskGeneration;
  m_taskState = TaskState::Rendering; m_operationPending = true;
  m_canvas->setEnabled(false); if (!owner) return;
  emit m_canvas->changed(); if (!owner) return;
  m_status->setText("Rendering the task result…");
  struct Result { QImage image; QByteArray png; QString diagnostic; };
  m_taskWork = OwnedWork::run<Result>(this, [source, document](const OwnedWork::Context &context) {
    Result result;
    try {
      auto rendered = renderEditorImage(source, document, {[&context] { return context.cancelled(); }, {}});
      result.image = std::move(rendered.image); result.diagnostic = rendered.diagnostic;
      if (!result.image.isNull() && !context.cancelled()) {
        QBuffer buffer(&result.png); buffer.open(QIODevice::WriteOnly);
        if (!result.image.save(&buffer, "PNG")) { result.image = {}; result.diagnostic = "editor-task-encode-failed"; }
      }
    } catch (...) { result.diagnostic = "editor-task-render-failed"; }
    return result;
  }, [this, owner, document, state, generation, job](Result result) {
    if (job != m_taskGeneration || m_taskState != TaskState::Rendering || m_taskWork.cancelled()) return;
    if (m_session.stateId() != state || m_canvas->documentGeneration() != generation || result.image.isNull()) {
      m_taskState = TaskState::Editing; finishOperation();
      if (owner) m_status->setText("Continue failed: " + (result.diagnostic.isEmpty() ? QString("editor-task-result-stale") : result.diagnostic) + ". The task and editable document are kept.");
      return;
    }
    m_taskState = TaskState::Completing;
    m_accepted = document; m_taskImage = std::move(result.image); m_sidecarIntent = m_saveSidecar->isChecked();
    QString diagnostic;
    // Result is ready before preferences run. No reentrant Continue or Cancel
    // can deliver a second result while the clipboard callback is in flight.
    if (m_taskPreferences.autoCopy) {
      ActionOutcome copied{ActionState::Failed, "clipboard-unavailable", {}};
      const auto publish = m_taskCopy.publish;
      const EditorCopyPayload payload{m_taskImage, result.png, {}};
      if (publish) {
        try { copied = publish(payload); }
        catch (...) { copied.outcomeUnknown = true; }
      }
      if (!owner) return;
      if (copied.state != ActionState::Succeeded || copied.outcomeUnknown || copied.diagnostic || copied.replacementSelection || !copied.artifacts.empty())
        diagnostic = copied.outcomeUnknown ? "editor-task-auto-copy-unknown" : "editor-task-auto-copy-failed";
    }
    if (m_taskCancelledDuringCopy && diagnostic.isEmpty()) diagnostic = "editor-task-auto-copy-cancelled-during";
    // The completed view is read-only and cannot restart the task. Leave errors
    // visible even when auto-close is enabled; never hide a partial failure.
    findChild<QPushButton *>("applyCaptureAnnotation")->setEnabled(false); if (!owner) return;
    auto *buttons = findChild<QDialogButtonBox *>();
    buttons->button(QDialogButtonBox::Cancel)->setText("Close");
    m_status->setText(diagnostic.isEmpty() ? "Continue completed. The rendered result was returned once." : "Continue completed; " + diagnostic + ". The editor is kept open. Later capture actions receive the rendered result.");
    hide(); if (!owner) return; setWindowModality(Qt::NonModal); show(); if (!owner) return;
    m_taskState = TaskState::Completed;
    try { emit taskFinished("Continue", diagnostic); }
    catch (...) { if (owner) { m_status->setText("Task completion observer failed; delivery is uncertain. The result will not be returned again."); return; } }
    if (owner && diagnostic.isEmpty() && m_taskPreferences.autoClose) done(Accepted);
  });
}
void CaptureAnnotationDialog::cancelTask() {
  const QPointer<CaptureAnnotationDialog> owner(this);
  if (m_taskState == TaskState::Completed) { done(Rejected); return; }
  if (m_taskState == TaskState::Rendering) {
    m_taskWork.cancel(); ++m_taskGeneration; m_taskState = TaskState::Editing; finishOperation(); if (!owner) return;
  }
  if (m_taskState == TaskState::Completing || m_taskConfirming || m_closing || m_operationPending || m_canvas->interactionActive()) return;
  if (m_session.dirty()) {
    m_taskConfirming = true;
    const auto confirmedState = m_session.stateId(), confirmedGeneration = m_canvas->documentGeneration();
    QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Question, "Unsaved task changes", "Save an editable copy, discard this task, or keep editing?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this));
    box->setObjectName("taskCloseConfirmation"); box->setDefaultButton(QMessageBox::Cancel);
    const int choice = box->exec();
    if (box) box->setParent(nullptr);
    delete box.data(); if (!owner) return;
    if (confirmedState != m_session.stateId() || confirmedGeneration != m_canvas->documentGeneration()) {
      m_taskConfirming = false; m_status->setText("The task changed during confirmation. Review it before closing."); return;
    }
    if (choice == QMessageBox::Save) {
      // Save is explicit independent user intent, not a Cancel side effect.
      // It keeps the task open; only a later Discard can emit Cancel.
      QPointer<EditorSaveDialog> picker(new EditorSaveDialog("Untitled.png", this));
      const int accepted = picker->exec();
      QString path = picker && accepted == Accepted ? picker->selectedFiles().value(0) : QString{};
      const auto encoding = picker ? picker->encodingOptions() : xerahs::editor::RasterSaveOptions{};
      if (picker) picker->setParent(nullptr);
      delete picker.data(); if (!owner) return;
      if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().isEmpty()) path += "." + xerahs::editor::rasterFormatSuffix(encoding.format);
        if (!xerahs::editor::validRasterSaveOptions(encoding) || xerahs::editor::rasterFormatFromSuffix(QFileInfo(path).suffix()) != std::optional<xerahs::editor::RasterFormat>(encoding.format)) {
          m_taskConfirming = false; m_status->setText("Save format or extension is invalid. The task remains open."); return;
        }
        const auto saveState = m_session.stateId();
        const auto saved = runEditorSave(path, m_image, m_session.document(), this, {}, {}, encoding);
        if (!owner) return;
        if (saved.rasterSaved && saved.sidecarSaved && !saved.cancelled) { m_session.recordSnapshotSave(saveState, true, true); m_status->setText("Editable copy saved. The task is still open."); }
        else m_status->setText("Save incomplete or cancelled. The task is still open; no Cancel outcome was returned.");
      }
      m_taskConfirming = false; return;
    }
    m_taskConfirming = false;
    if (choice != QMessageBox::Discard) return;
  }
  m_taskState = TaskState::Completed; m_accepted.reset(); m_taskImage = {};
  try { emit taskFinished("Cancel", {}); } catch (...) { if (owner) m_status->setText("Task cancellation observer failed; it will not be delivered again."); }
  if (owner) done(Rejected);
}
void CaptureAnnotationDialog::abandonTask() {
  if (m_taskState == TaskState::Completing) { m_taskCancelledDuringCopy = true; return; }
  m_taskWork.cancel(); ++m_taskGeneration;
  if (m_taskState == TaskState::Inactive) { done(Rejected); return; }
  // The upstream cancellation token is already set. Never publish a result or
  // run preferences while retiring the native window.
  m_taskState = TaskState::Completed; m_accepted.reset(); m_taskImage = {};
  done(Rejected);
}
void CaptureAnnotationDialog::closeEvent(QCloseEvent *event) {
  if (m_taskState == TaskState::Inactive) { QDialog::closeEvent(event); return; }
  cancelTask();
  if (m_closing) event->accept(); else event->ignore();
}
}
