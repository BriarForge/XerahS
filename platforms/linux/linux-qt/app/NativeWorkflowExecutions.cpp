#include "NativeWorkflowExecutions.h"
#include <QDir>
#include <algorithm>
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
WorkflowExecutionListing NativeWorkflowExecutions::list() const {
  const auto stored = m_records->list(); WorkflowExecutionListing listing;
  if (!stored.diagnostic.isEmpty()) listing.diagnostic = "workflow-storage-unavailable";
  for (const auto &entry : stored.entries) {
    WorkflowExecutionEntry projected{entry.id, entry.bytes, {}, {}};
    if (!entry.diagnostic.isEmpty()) projected.diagnostic = "workflow-storage-unavailable";
    else if (const auto record = parseWorkflowExecution(entry.bytes, entry.id)) projected.result = workflowExecutionResult(*record);
    else projected.diagnostic = "workflow-execution-invalid";
    listing.entries.push_back(std::move(projected));
  }
  std::sort(listing.entries.begin(), listing.entries.end(), [](const auto &a, const auto &b) {
    const auto left = a.result.value("startedAt").toString(), right = b.result.value("startedAt").toString();
    return left == right ? a.id < b.id : left > right;
  });
  return listing;
}
RecordWrite NativeWorkflowExecutions::erase(const QString &id, const QByteArray &snapshot) const {
  const auto erased = m_records->erase(id, snapshot);
  if (erased.stored) return erased;
  return {false, erased.diagnostic == "pipeline-journal-changed" ? QString("workflow-record-changed") : QString("workflow-storage-unavailable")};
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
