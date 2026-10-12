#include "CaptureRecoveryActions.h"
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
  connect(m_edit, &QAction::triggered, this, &CaptureRecoveryActions::edit);
  connect(m_discard, &QAction::triggered, this, &CaptureRecoveryActions::discard);
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
  m_edit->setEnabled(available && bool(m_opener)); if (!guard) return;
  m_discard->setEnabled(available); if (!guard) return;
  m_edit->setToolTip(m_opener ? m_edit->statusTip() : QStringLiteral("The image editor is unavailable."));
}
void CaptureRecoveryActions::edit() {
  if (m_running || !m_capture || m_capture->busy() || !m_opener || QApplication::activeModalWidget()) return;
  const auto receipt = m_capture->lastPipelineResult(); if (!receipt) return;
  const auto opener = m_opener; const auto parent = m_dialogParent;
  const QPointer<CaptureRecoveryActions> guard(this);
  ++m_operation; m_running = true; updateAvailability(); if (!guard) return;
  bool opened = false, failed = false;
  try { opened = opener(receipt->workingMedia, parent.data()); } catch (...) { failed = true; }
  if (!guard) return;
  m_running = false; updateAvailability(); if (!guard) return;
  if (failed) emit notify(QStringLiteral("Could not edit the retained capture (action-failed). The retained result is still available."), false);
  else if (opened) emit notify(QStringLiteral("Opened the retained capture as a new unsaved image."), true);
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
