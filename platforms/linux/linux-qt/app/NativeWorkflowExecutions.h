#pragma once
#include "NativePrivateRecords.h"
#include "workflows/WorkflowExecution.h"
namespace xerahs::app {
struct WorkflowExecutionEntry {
  QString id;
  QByteArray snapshot;
  QJsonObject result; // Redacted Core projection; never settings or artifact read authority.
  QString diagnostic;
};
struct WorkflowExecutionListing { std::vector<WorkflowExecutionEntry> entries; QString diagnostic; };
class NativeWorkflowExecutions final {
public:
  explicit NativeWorkflowExecutions(QString directory);
  static QString defaultDirectory();
  xerahs::workflows::WorkflowExecution::Writer writer() const;
  QJsonObject result(const QString &id) const;
  WorkflowExecutionListing list() const;
  RecordWrite erase(const QString &id, const QByteArray &snapshot) const;
private:
  std::shared_ptr<const NativePrivateRecords> m_records;
};
} // namespace xerahs::app
