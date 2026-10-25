#pragma once
#include "NativeWorkflowExecutions.h"
#include "OwnedWork.h"
#include <QDialog>
class QLabel;
class QPushButton;
class QTreeWidget;
namespace xerahs::app {
struct WorkflowHistoryPorts {
  std::function<WorkflowExecutionListing()> list;
  std::function<RecordWrite(const QString &, const QByteArray &)> erase;
};
WorkflowHistoryPorts workflowHistoryPorts(const NativeWorkflowExecutions &store);
class WorkflowExecutionHistoryDialog final : public QDialog {
  Q_OBJECT
public:
  WorkflowExecutionHistoryDialog(WorkflowHistoryPorts ports, std::function<bool()> captureBusy, QWidget *parent = nullptr);
  ~WorkflowExecutionHistoryDialog() override { m_work.cancel(); }
private:
  void refresh();
  void forget();
  void updateAvailability();
  void publish(WorkflowExecutionListing listing);
  std::optional<WorkflowExecutionEntry> selected() const;
  WorkflowHistoryPorts m_ports;
  std::function<bool()> m_captureBusy;
  WorkflowExecutionListing m_listing;
  OwnedWork m_work;
  bool m_busy = false, m_confirming = false, m_populating = false, m_reload = false;
  QTreeWidget *m_table;
  QLabel *m_status;
  QPushButton *m_refresh, *m_forget;
};
} // namespace xerahs::app
