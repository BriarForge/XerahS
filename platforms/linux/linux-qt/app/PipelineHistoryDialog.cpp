#include "PipelineHistoryDialog.h"
#include "PipelineResultDialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <mutex>
namespace xerahs::app {
struct HistoryJob {
  std::mutex mutex;
  bool ready = false, erasing = false;
  JournalListing listing;
  JournalWrite erased;
};
PipelineHistoryDialog::PipelineHistoryDialog(PipelineJournalStorage storage, std::function<bool()> captureBusy, QWidget *parent)
    : QDialog(parent), m_storage(std::move(storage)), m_captureBusy(std::move(captureBusy)) {
  setObjectName(QStringLiteral("captureHistoryDialog")); setWindowTitle(QStringLiteral("Capture result history"));
  setAttribute(Qt::WA_DeleteOnClose); resize(760, 440);
  auto *layout = new QVBoxLayout(this);
  auto *notice = new QLabel(QStringLiteral("Capture result records survive restart. Interrupted actions can have unknown outcomes. "
      "Recovery does not retry actions. Image pixels are not stored here; saved images remain in their files."), this);
  notice->setTextFormat(Qt::PlainText); notice->setWordWrap(true); layout->addWidget(notice);
  m_table = new QTreeWidget(this); m_table->setObjectName(QStringLiteral("captureHistoryRecords"));
  m_table->setAccessibleName(QStringLiteral("Durable capture results")); m_table->setRootIsDecorated(false);
  m_table->setHeaderLabels({QStringLiteral("Started"), QStringLiteral("Pipeline"), QStringLiteral("Task"), QStringLiteral("Diagnostic")});
  layout->addWidget(m_table);
  m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("captureHistoryStatus"));
  m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); layout->addWidget(buttons);
  m_refresh = buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole); m_refresh->setObjectName(QStringLiteral("refreshCaptureHistory"));
  m_review = buttons->addButton(QStringLiteral("Review result…"), QDialogButtonBox::ActionRole); m_review->setObjectName(QStringLiteral("reviewCaptureHistory"));
  m_forget = buttons->addButton(QStringLiteral("Forget record…"), QDialogButtonBox::ActionRole); m_forget->setObjectName(QStringLiteral("forgetCaptureHistory"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_refresh, &QPushButton::clicked, this, &PipelineHistoryDialog::refresh);
  connect(m_review, &QPushButton::clicked, this, &PipelineHistoryDialog::review);
  connect(m_forget, &QPushButton::clicked, this, &PipelineHistoryDialog::forget);
  connect(m_table, &QTreeWidget::itemSelectionChanged, this, &PipelineHistoryDialog::updateAvailability);
  connect(m_table, &QTreeWidget::itemActivated, this, [this] { review(); });
  m_timer = new QTimer(this); m_timer->setInterval(20); connect(m_timer, &QTimer::timeout, this, &PipelineHistoryDialog::poll);
  m_timer->start(); refresh(); m_table->setFocus();
}
const JournalEntry *PipelineHistoryDialog::selectedEntry() const {
  const auto *item = m_table->currentItem(); if (!item) return nullptr;
  const auto id = item->data(0, Qt::UserRole).toString();
  for (const auto &entry : m_listing.entries) if (entry.taskId == id) return &entry;
  return nullptr;
}
void PipelineHistoryDialog::updateAvailability() {
  const auto *entry = selectedEntry(); const bool idle = !m_job && !m_confirming;
  const bool busy = m_captureBusy && m_captureBusy();
  m_refresh->setEnabled(idle && !busy); m_review->setEnabled(idle && entry && bool(entry->parsed.record));
  m_forget->setEnabled(idle && entry && bool(entry->parsed.record) && !busy);
}
void PipelineHistoryDialog::refresh() {
  if (m_job || m_confirming || !m_storage) return;
  if (m_captureBusy && m_captureBusy()) {
    m_reloadWhenIdle = true; m_status->setText(QStringLiteral("Capture is running. Results will refresh when it finishes.")); updateAvailability(); return;
  }
  m_reloadWhenIdle = false;
  m_job = std::make_shared<HistoryJob>(); const auto job = m_job; const auto storage = m_storage;
  m_status->setText(QStringLiteral("Reading capture results…")); updateAvailability();
  auto *worker = QThread::create([job, storage] {
    auto listing = storage->list(); std::lock_guard<std::mutex> lock(job->mutex);
    job->listing = std::move(listing); job->ready = true;
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
}
void PipelineHistoryDialog::poll() {
  if (!m_job) {
    if (m_reloadWhenIdle && !m_confirming && (!m_captureBusy || !m_captureBusy())) refresh();
    else updateAvailability();
    return;
  }
  const auto job = m_job;
  { std::lock_guard<std::mutex> lock(job->mutex); if (!job->ready) return; }
  m_job.reset();
  if (job->erasing) {
    if (!job->erased.stored) { m_status->setText(QStringLiteral("Could not forget the record (%1). Refresh and review the current result.").arg(job->erased.diagnostic)); updateAvailability(); return; }
    refresh(); return;
  }
  // A capture may have started during the worker read. Never present its live
  // write-ahead record as a restarted/interrupted run; reload after completion.
  if (m_captureBusy && m_captureBusy()) { m_reloadWhenIdle = true; updateAvailability(); return; }
  const auto selected = selectedEntry() ? selectedEntry()->taskId : QString{};
  m_listing = std::move(job->listing); m_table->clear();
  for (const auto &entry : m_listing.entries) {
    const auto &record = entry.parsed.record;
    auto *item = new QTreeWidgetItem(m_table, {record ? record->createdAt.toLocalTime().toString(Qt::ISODateWithMs) : QString{},
        record ? record->report.value("pipeline_state").toString() : QStringLiteral("Unavailable"), entry.taskId, entry.parsed.diagnostic});
    item->setData(0, Qt::UserRole, entry.taskId); if (entry.taskId == selected) m_table->setCurrentItem(item);
  }
  if (!m_table->currentItem() && m_table->topLevelItemCount()) m_table->setCurrentItem(m_table->topLevelItem(0));
  for (int column = 0; column < 4; ++column) m_table->resizeColumnToContents(column);
  m_status->setText(m_listing.diagnostic.isEmpty() ? QStringLiteral("%1 result records. Forgetting removes only the selected metadata record; saved files and current retained pixels are kept.").arg(m_listing.entries.size()) :
      QStringLiteral("Some results could not be read (%1). Existing records are preserved.").arg(m_listing.diagnostic));
  updateAvailability();
}
void PipelineHistoryDialog::review() {
  if (m_job || m_confirming) return;
  const auto *entry = selectedEntry(); if (!entry || !entry->parsed.record) return;
  const auto record = *entry->parsed.record;
  auto *dialog = makePipelineResultDialog(record.report, this, record.selected, record.pendingAction.value_or(QString{}));
  dialog->open();
}
void PipelineHistoryDialog::forget() {
  if (m_job || m_confirming || (m_captureBusy && m_captureBusy())) return;
  const auto *entry = selectedEntry(); if (!entry || !entry->parsed.record) return;
  const auto snapshot = *entry; m_confirming = true; updateAvailability();
  auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Forget capture result?"),
      QStringLiteral("Remove this result record from history? This removes its recovery evidence. Saved files, clipboard contents, editor documents and current retained pixels are kept."),
      QMessageBox::Discard | QMessageBox::Cancel, this);
  box->setObjectName(QStringLiteral("forgetCaptureHistoryDialog")); box->setDefaultButton(QMessageBox::Cancel);
  box->setEscapeButton(QMessageBox::Cancel); box->setAttribute(Qt::WA_DeleteOnClose);
  connect(box, &QMessageBox::finished, this, [this, snapshot](int choice) {
    m_confirming = false;
    if (choice != QMessageBox::Discard || (m_captureBusy && m_captureBusy())) { updateAvailability(); return; }
    m_job = std::make_shared<HistoryJob>(); const auto job = m_job; job->erasing = true; const auto storage = m_storage;
    m_status->setText(QStringLiteral("Forgetting selected result…")); updateAvailability();
    auto *worker = QThread::create([job, storage, snapshot] {
      const auto erased = storage->erase(snapshot.taskId, snapshot.bytes); std::lock_guard<std::mutex> lock(job->mutex);
      job->erased = erased; job->ready = true;
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
  });
  box->open();
}
} // namespace xerahs::app
