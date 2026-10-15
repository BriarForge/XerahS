#include "NativeWorkflowExecutions.h"
#include <QDir>
namespace xerahs::app {
using namespace xerahs::workflows;
NativeWorkflowExecutions::NativeWorkflowExecutions(QString directory) : m_records(std::make_shared<NativePrivateRecords>(std::move(directory),
    [](const QByteArray &bytes, const QString &id) { return bool(parseWorkflowExecution(bytes, id)); })) {}
QString NativeWorkflowExecutions::defaultDirectory() {
  const auto configured = qEnvironmentVariable("XDG_STATE_HOME");
  const auto state = QDir::isAbsolutePath(configured) ? configured : QDir::homePath() + QStringLiteral("/.local/state");
  return QDir::cleanPath(state + QStringLiteral("/com.xerahs.app/workflow-executions"));
}
WorkflowExecution::Writer NativeWorkflowExecutions::writer() const {
  return [records = m_records](const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) { return records->replace(id, expected, bytes); };
}
QJsonObject NativeWorkflowExecutions::result(const QString &id) const {
  if (QUuid(id).isNull() || !xerahs::actions::validJournalTaskId(id)) return workflowExecutionError("workflow-execution-id-invalid");
  const auto listing = m_records->list();
  if (!listing.diagnostic.isEmpty()) return workflowExecutionError("workflow-storage-unavailable");
  for (const auto &entry : listing.entries) if (entry.id == id) {
    if (!entry.diagnostic.isEmpty()) return workflowExecutionError("workflow-storage-unavailable");
    const auto record = parseWorkflowExecution(entry.bytes, id);
    return record ? workflowExecutionResult(*record) : workflowExecutionError("workflow-execution-invalid");
  }
  return workflowExecutionError("workflow-execution-not-found");
}
} // namespace xerahs::app
