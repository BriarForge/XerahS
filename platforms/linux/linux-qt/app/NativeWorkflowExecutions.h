#pragma once
#include "NativePrivateRecords.h"
#include "workflows/WorkflowExecution.h"
namespace xerahs::app {
class NativeWorkflowExecutions final {
public:
  explicit NativeWorkflowExecutions(QString directory);
  static QString defaultDirectory();
  xerahs::workflows::WorkflowExecution::Writer writer() const;
  QJsonObject result(const QString &id) const;
private:
  std::shared_ptr<const NativePrivateRecords> m_records;
};
} // namespace xerahs::app
