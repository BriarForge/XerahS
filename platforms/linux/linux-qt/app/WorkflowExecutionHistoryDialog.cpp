#include "WorkflowExecutionHistoryDialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
namespace xerahs::app {
WorkflowHistoryPorts workflowHistoryPorts(const NativeWorkflowExecutions &store) {
  return {[store] { return store.list(); }, [store](const QString &id, const QByteArray &snapshot) { return store.erase(id, snapshot); }};
}
WorkflowExecutionHistoryDialog::WorkflowExecutionHistoryDialog(WorkflowHistoryPorts ports, std::function<bool()> captureBusy, QWidget *parent)
    : QDialog(parent), m_ports(std::move(ports)), m_captureBusy(std::move(captureBusy)) {
  setObjectName("workflowExecutionHistory"); setWindowTitle("Workflow execution history"); setAttribute(Qt::WA_DeleteOnClose); resize(800, 450);
  auto *layout = new QVBoxLayout(this);
  auto *notice = new QLabel("Execution records contain metadata, not saved media. An unconfirmed outcome is unknown, not a failed action. "
      "Forgetting removes recovery evidence only; it never retries work or removes images, pipeline results, credentials or clipboard contents.", this);
  notice->setTextFormat(Qt::PlainText); notice->setWordWrap(true); layout->addWidget(notice);
  m_table = new QTreeWidget(this); m_table->setObjectName("workflowExecutionRecords"); m_table->setAccessibleName("Workflow execution metadata");
  m_table->setRootIsDecorated(false); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setHeaderLabels({"Started", "State", "Execution ID", "Origin", "Diagnostic"}); layout->addWidget(m_table);
  m_status = new QLabel(this); m_status->setObjectName("workflowExecutionHistoryStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); layout->addWidget(buttons);
  m_refresh = buttons->addButton("Refresh", QDialogButtonBox::ActionRole); m_refresh->setObjectName("refreshWorkflowExecutions");
  m_forget = buttons->addButton("Forget record…", QDialogButtonBox::ActionRole); m_forget->setObjectName("forgetWorkflowExecution");
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_refresh, &QPushButton::clicked, this, &WorkflowExecutionHistoryDialog::refresh);
  connect(m_forget, &QPushButton::clicked, this, &WorkflowExecutionHistoryDialog::forget);
  connect(m_table, &QTreeWidget::itemSelectionChanged, this, [this] { if (!m_populating) updateAvailability(); });
  auto *timer = new QTimer(this); timer->setInterval(100);
  connect(timer, &QTimer::timeout, this, [this] {
    if (m_reload && !m_busy && !m_confirming && (!m_captureBusy || !m_captureBusy())) refresh();
    else updateAvailability();
  }); timer->start();
  // Start after construction so synchronous availability observers cannot delete
  // a partly constructed window or allow a late constructor refresh.
  QTimer::singleShot(0, this, [this] { refresh(); });
}
std::optional<WorkflowExecutionEntry> WorkflowExecutionHistoryDialog::selected() const {
  const auto *item = m_table->currentItem(); if (!item) return {};
  const auto id = item->data(0, Qt::UserRole).toString();
  for (const auto &entry : m_listing.entries) if (entry.id == id) return entry;
  return {};
}
void WorkflowExecutionHistoryDialog::updateAvailability() {
  if (m_populating) return;
  const QPointer<WorkflowExecutionHistoryDialog> guard(this);
  const auto busy = m_captureBusy; const bool capture = busy && busy(); if (!guard) return;
  const auto entry = selected(); const bool idle = !m_busy && !m_confirming && !capture;
  m_refresh->setEnabled(idle && bool(m_ports.list)); if (!guard) return;
  m_forget->setEnabled(idle && bool(m_ports.erase) && entry && entry->diagnostic.isEmpty() && entry->result.value("ok").toBool());
}
void WorkflowExecutionHistoryDialog::refresh() {
  if (m_busy || m_confirming || !m_ports.list) return;
  const QPointer<WorkflowExecutionHistoryDialog> guard(this);
  const auto busy = m_captureBusy; const bool capture = busy && busy(); if (!guard) return;
  if (capture) { m_reload = true; m_status->setText("Capture is running. Execution records will refresh when it finishes."); if (guard) updateAvailability(); return; }
  m_reload = false; m_busy = true; m_status->setText("Reading workflow execution metadata…"); if (!guard) return;
  updateAvailability(); if (!guard) return;
  const auto list = m_ports.list;
  m_work = OwnedWork::run<WorkflowExecutionListing>(this, [list](const OwnedWork::Context &context) {
    if (context.cancelled()) return WorkflowExecutionListing{};
    try { return list(); } catch (...) { return WorkflowExecutionListing{{}, "workflow-storage-unavailable"}; }
  }, [this](WorkflowExecutionListing listing) {
    m_busy = false;
    if (m_captureBusy && m_captureBusy()) { m_reload = true; updateAvailability(); return; }
    publish(std::move(listing));
  });
}
void WorkflowExecutionHistoryDialog::publish(WorkflowExecutionListing listing) {
  const QPointer<WorkflowExecutionHistoryDialog> guard(this);
  const auto previous = selected(); m_listing = std::move(listing); m_populating = true;
  m_table->clear(); if (!guard) return;
  for (const auto &entry : m_listing.entries) {
    auto *item = new QTreeWidgetItem({entry.result.value("startedAt").toString(),
        entry.diagnostic.isEmpty() ? entry.result.value("state").toString() : QString("Unavailable"), entry.id,
        entry.result.value("origin").toString(), entry.diagnostic});
    item->setData(0, Qt::UserRole, entry.id); m_table->addTopLevelItem(item); if (!guard) return;
    if (previous && previous->id == entry.id) { m_table->setCurrentItem(item); if (!guard) return; }
  }
  if (!m_table->currentItem() && m_table->topLevelItemCount()) { m_table->setCurrentItem(m_table->topLevelItem(0)); if (!guard) return; }
  m_populating = false;
  m_status->setText(m_listing.diagnostic.isEmpty() ? QString("%1 execution records. Select one to forget its metadata; output files are kept.").arg(m_listing.entries.size()) :
      "Some execution records could not be read (workflow-storage-unavailable). Existing files are preserved.");
  if (guard) updateAvailability();
}
void WorkflowExecutionHistoryDialog::forget() {
  if (m_busy || m_confirming) return;
  const QPointer<WorkflowExecutionHistoryDialog> guard(this);
  const auto busy = m_captureBusy; const bool capture = busy && busy(); if (!guard || capture) return;
  const auto entry = selected(); if (!entry || !entry->diagnostic.isEmpty() || !entry->result.value("ok").toBool() || !m_ports.erase) return;
  m_confirming = true; updateAvailability(); if (!guard) return;
  auto *box = new QMessageBox(QMessageBox::Question, "Forget workflow execution?",
      "Remove only the selected execution metadata? This removes its recovery evidence, including any unconfirmed outcome. "
      "Saved images, pipeline results, credentials, clipboard contents and current work are kept. No action is retried.", QMessageBox::Discard | QMessageBox::Cancel, this);
  box->setObjectName("forgetWorkflowExecutionDialog"); box->setDefaultButton(QMessageBox::Cancel); box->setEscapeButton(QMessageBox::Cancel); box->setAttribute(Qt::WA_DeleteOnClose);
  connect(box, &QMessageBox::finished, this, [this, snapshot = *entry](int choice) {
    m_confirming = false;
    if (choice != QMessageBox::Discard || (m_captureBusy && m_captureBusy())) { updateAvailability(); return; }
    const QPointer<WorkflowExecutionHistoryDialog> live(this); m_busy = true;
    m_status->setText("Forgetting selected execution metadata…"); if (!live) return;
    updateAvailability(); if (!live) return;
    const auto erase = m_ports.erase;
    m_work = OwnedWork::run<RecordWrite>(this, [erase, snapshot](const OwnedWork::Context &context) {
      if (context.cancelled()) return RecordWrite{false, "workflow-forget-cancelled"};
      try { return erase(snapshot.id, snapshot.snapshot); } catch (...) { return RecordWrite{false, "workflow-storage-unavailable"}; }
    }, [this](RecordWrite result) {
      m_busy = false;
      if (result.stored) { refresh(); return; }
      m_status->setText(result.diagnostic == "workflow-record-changed" ?
          "The record changed (workflow-record-changed). Refresh and review it before another attempt." :
          "Could not confirm removal (workflow-storage-unavailable). Refresh and review the record before another attempt.");
      updateAvailability();
    });
  });
  box->open();
}
} // namespace xerahs::app
