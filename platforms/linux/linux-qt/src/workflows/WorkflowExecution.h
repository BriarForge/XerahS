#pragma once
#include "CaptureWorkflows.h"
#include "actions/PipelineJournal.h"
#include <QJsonArray>
#include <functional>
namespace xerahs::workflows {
// Private observation, never restart/retry authority or an artifact read grant.
struct WorkflowExecutionRecord {
  QString id, origin, state = QStringLiteral("admitted"), pipelineId, diagnostic;
  QDateTime startedAt, finishedAt;
  QJsonObject settings;
  QJsonArray steps;
};
QByteArray serializeWorkflowExecution(const WorkflowExecutionRecord &record);
std::optional<WorkflowExecutionRecord> parseWorkflowExecution(const QByteArray &bytes, const QString &id);
QJsonObject workflowExecutionResult(const WorkflowExecutionRecord &record);
QJsonObject workflowExecutionError(const QString &code);
class WorkflowExecution final {
public:
  using Writer = std::function<xerahs::actions::JournalWrite(const QString &, const std::optional<QByteArray> &, const QByteArray &)>;
  WorkflowExecution(QString id, QString origin, const CaptureWorkflow &effective, Writer writer);
  ~WorkflowExecution();
  WorkflowExecution(const WorkflowExecution &) = delete;
  WorkflowExecution &operator=(const WorkflowExecution &) = delete;
  bool admit();
  bool start();
  bool processing(const QString &pipelineId);
  bool finish(const QString &state, const QString &diagnostic = {});
  bool complete(const xerahs::actions::PipelineResult &pipeline);
  const WorkflowExecutionRecord &record() const { return m_record; }
private:
  bool persist();
  QString captureStep() const { return "capture-" + m_record.settings.value("capture").toString(); }
  WorkflowExecutionRecord m_record;
  Writer m_writer;
  std::optional<QByteArray> m_expected;
  bool m_failed = false;
};
} // namespace xerahs::workflows
