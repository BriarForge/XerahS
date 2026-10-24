#include "CaptureRecoveryActions.h"
#include "PipelineResultDialog.h"
#include <QAction>
#include <QApplication>
#include <QMessageBox>
namespace xerahs::app {
CaptureRecoveryActions::CaptureRecoveryActions(CaptureController &capture, QWidget *dialogParent,
                                               CapturedImageOpener opener, QObject *parent)
    : QObject(parent), m_capture(&capture), m_dialogParent(dialogParent), m_opener(std::move(opener)) {
  m_edit = new QAction(QStringLiteral("Edit retained capture…"), this); m_edit->setObjectName(QStringLiteral("editRetainedCapture"));
  m_edit->setStatusTip(QStringLiteral("Open the retained pixels as a new unsaved editor document."));
  m_discard = new QAction(QStringLiteral("Discard retained capture…"), this); m_discard->setObjectName(QStringLiteral("discardRetainedCapture"));
  m_discard->setStatusTip(QStringLiteral("Clear the retained result from memory. Saved files, clipboard contents and editor documents are kept."));
  m_review = new QAction(QStringLiteral("Review retained capture result…"), this); m_review->setObjectName(QStringLiteral("reviewRetainedCaptureResult"));
  m_review->setStatusTip(QStringLiteral("Inspect action outcomes and saved-file metadata without repeating actions."));
  m_retry = new QAction(QStringLiteral("Retry failed capture actions…"), this); m_retry->setObjectName(QStringLiteral("retryRetainedCaptureActions"));
  m_retry->setStatusTip(QStringLiteral("Make one linked attempt at failed capture outputs. Earlier successful actions are kept."));
  connect(m_edit, &QAction::triggered, this, &CaptureRecoveryActions::edit);
  connect(m_discard, &QAction::triggered, this, &CaptureRecoveryActions::discard);
  connect(m_review, &QAction::triggered, this, &CaptureRecoveryActions::review);
  connect(m_retry, &QAction::triggered, this, &CaptureRecoveryActions::retry);
  connect(&capture, &CaptureController::busyChanged, this, &CaptureRecoveryActions::updateAvailability);
  connect(&capture, &CaptureController::retainedResultChanged, this, &CaptureRecoveryActions::updateAvailability);
  connect(&capture, &QObject::destroyed, this, &CaptureRecoveryActions::updateAvailability);
  updateAvailability();
}
CaptureRecoveryActions::~CaptureRecoveryActions() {
  if (m_dialog) { disconnect(m_dialog, nullptr, this, nullptr); delete m_dialog.data(); }
}
void CaptureRecoveryActions::updateAvailability() {
  const bool available = m_capture && !m_capture->busy() && m_capture->lastPipelineResult() && !m_running;
  const QPointer<CaptureRecoveryActions> guard(this);
  const bool editable = m_capture && m_capture->lastPipelineResult() && m_capture->lastPipelineResult()->annotationDocument ? bool(m_annotationOpener) : bool(m_opener);
  m_edit->setEnabled(available && editable); if (!guard) return;
  m_discard->setEnabled(available); if (!guard) return;
  m_review->setEnabled(available); if (!guard) return;
  const auto plan = m_capture && m_capture->lastPipelineResult() ? m_capture->lastPipelineResult()->retryPlan() : xerahs::actions::PipelineRetryPlan{};
  m_retry->setEnabled(available && plan.available()); if (!guard) return;
  m_retry->setToolTip(plan.diagnostic == "retry-counter-persistence-failed" ?
      QStringLiteral("The PNG was saved, but its filename counter was not persisted. Automatic retry is disabled to avoid another PNG. Review counter storage; retained pixels can still be edited or explicitly saved.") : plan.available() ? m_retry->statusTip() :
      QStringLiteral("Retry unavailable (%1). Retained pixels can still be opened in the editor.").arg(plan.diagnostic.isEmpty() ? QStringLiteral("retry-no-failed-actions") : plan.diagnostic));
  if (!guard) return;
  m_edit->setToolTip(editable ? m_edit->statusTip() : QStringLiteral("The image or annotation editor is unavailable."));
}
void CaptureRecoveryActions::setAnnotationOpener(std::function<bool(const xerahs::editor::AnnotationDocument &, QWidget *)> opener) {
  m_annotationOpener = std::move(opener); updateAvailability();
}
void CaptureRecoveryActions::retry() {
  if (m_running || !m_capture || m_capture->busy() || QApplication::activeModalWidget()) return;
  const auto expected = m_capture->lastPipelineResult(); if (!expected) return;
  const auto plan = expected->retryPlan(); if (!plan.available()) return;
  QStringList labels;
  for (const auto &id : plan.selected) labels.append(CaptureActions::actionLabel(id));
  QString companionNote;
  for (const auto &action : expected->pipeline.orderedResults)
    if (xerahs::actions::hasAcknowledgedPartialRaster(action.action, action.state, action.artifacts))
      companionNote = " A partial paired save retries only its .xann companion at the existing PNG path; the PNG is kept without rewriting it or reopening Save As.";
  const auto operation = ++m_operation; m_running = true;
  const QPointer<CaptureRecoveryActions> guard(this); updateAvailability(); if (!guard) return;
  auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Retry failed capture actions?"),
      QStringLiteral("Retry %1 once using the retained capture? Earlier successful actions are kept. "
                     "The attempt creates a linked result. Cancel Capture or Escape in the XerahS window stops later actions.").arg(labels.join(QStringLiteral(" and "))) + companionNote,
      QMessageBox::Retry | QMessageBox::Cancel, m_dialogParent);
  box->setObjectName(QStringLiteral("retryRetainedCaptureDialog")); box->setDefaultButton(QMessageBox::Cancel);
  box->setEscapeButton(QMessageBox::Cancel); box->setAttribute(Qt::WA_DeleteOnClose); m_dialog = box;
  connect(box, &QMessageBox::finished, this, [this, operation, expected](int choice) {
    if (operation != m_operation) return;
    const QPointer<CaptureRecoveryActions> guard(this);
    m_dialog = nullptr; m_running = false;
    if (choice == QMessageBox::Retry && m_dialogParent) {
      const QPointer<QWidget> parent = m_dialogParent;
      if (parent && parent->isMinimized()) parent->setWindowState(parent->windowState() & ~Qt::WindowMinimized);
      if (!guard) return;
      if (parent) parent->show();
      if (!guard) return;
      if (parent) parent->raise();
      if (!guard) return;
      if (parent) parent->activateWindow();
    }
    if (!guard) return;
    const bool accepted = choice == QMessageBox::Retry && m_capture && m_capture->retryFailedActions(expected);
    if (!guard) return;
    updateAvailability(); if (!guard) return;
    if (choice == QMessageBox::Retry && !accepted)
      emit notify(QStringLiteral("The retained capture changed or is busy. Review its current result before retrying."), false);
  });
  connect(box, &QObject::destroyed, this, [this, operation] {
    if (operation != m_operation || !m_running) return;
    m_dialog = nullptr; m_running = false; updateAvailability();
  });
  box->open();
}
void CaptureRecoveryActions::edit() {
  if (m_running || !m_capture || m_capture->busy() || QApplication::activeModalWidget()) return;
  const auto receipt = m_capture->lastPipelineResult(); if (!receipt) return;
  const auto opener = m_opener; const auto parent = m_dialogParent;
  const auto annotationOpener = m_annotationOpener;
  if (receipt->annotationDocument ? !annotationOpener : !opener) return;
  const QPointer<CaptureRecoveryActions> guard(this);
  ++m_operation; m_running = true; updateAvailability(); if (!guard) return;
  bool opened = false, failed = false;
  try { opened = receipt->annotationDocument ? annotationOpener(*receipt->annotationDocument, parent.data()) : opener(receipt->workingMedia, parent.data()); } catch (...) { failed = true; }
  if (!guard) return;
  m_running = false; updateAvailability(); if (!guard) return;
  if (failed) emit notify(QStringLiteral("Could not edit the retained capture (action-failed). The retained result is still available."), false);
  else if (opened) emit notify(QStringLiteral("Opened the retained capture as a new unsaved image."), true);
}
void CaptureRecoveryActions::review() {
  if (m_running || !m_capture || m_capture->busy() || QApplication::activeModalWidget()) return;
  const auto receipt = m_capture->lastPipelineResult(); if (!receipt) return;
  const auto report = receipt->canonicalResult();
  const auto operation = ++m_operation; m_running = true;
  const QPointer<CaptureRecoveryActions> guard(this);
  updateAvailability(); if (!guard) return;
  auto *dialog = makePipelineResultDialog(report, m_dialogParent); m_dialog = dialog;
  const auto restore = [this, operation] {
    if (operation != m_operation || !m_running) return;
    m_dialog = nullptr; m_running = false; updateAvailability();
  };
  connect(dialog, &QDialog::finished, this, restore);
  connect(dialog, &QObject::destroyed, this, restore);
  dialog->open();
}
void CaptureRecoveryActions::discard() {
  if (m_running || !m_capture || m_capture->busy() || QApplication::activeModalWidget()) return;
  const auto expected = m_capture->lastPipelineResult(); if (!expected) return;
  const auto operation = ++m_operation; m_running = true;
  const QPointer<CaptureRecoveryActions> guard(this);
  updateAvailability(); if (!guard) return;
  auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Discard retained capture?"),
      QStringLiteral("This clears the retained capture and its result from memory. Saved files, clipboard contents and existing editor documents are kept. "
                     "If this capture has no saved copy, its pixels will be lost."), QMessageBox::Discard | QMessageBox::Cancel, m_dialogParent);
  box->setObjectName(QStringLiteral("discardRetainedCaptureDialog")); box->setDefaultButton(QMessageBox::Cancel);
  box->setEscapeButton(QMessageBox::Cancel); box->setAttribute(Qt::WA_DeleteOnClose); m_dialog = box;
  connect(box, &QMessageBox::finished, this, [this, operation, expected](int choice) {
    if (operation != m_operation) return;
    const QPointer<CaptureRecoveryActions> guard(this);
    bool discarded = false;
    if (choice == QMessageBox::Discard && m_capture) discarded = m_capture->discardRetainedResult(expected);
    if (!guard) return;
    m_dialog = nullptr; m_running = false; updateAvailability(); if (!guard) return;
    if (discarded) emit notify(QStringLiteral("Discarded the retained capture. Saved files, clipboard contents and editor documents are kept."), true);
    else if (choice == QMessageBox::Discard) emit notify(QStringLiteral("The retained capture changed or is busy. Review the current result before discarding it."), false);
  });
  connect(box, &QObject::destroyed, this, [this, operation] {
    if (operation != m_operation || !m_running) return;
    m_dialog = nullptr; m_running = false; updateAvailability();
  });
  box->open();
}
} // namespace xerahs::app
