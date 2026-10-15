#pragma once
#include "CaptureWorkflows.h"
#include <QSet>
namespace xerahs::workflows {
struct WorkflowShortcut { QString id, description; };
struct WorkflowShortcutPlan { QVector<WorkflowShortcut> entries; int unavailable = 0; QString diagnostic; };
WorkflowShortcutPlan workflowShortcutPlan(const CaptureWorkflows &catalog, const QStringList &available);
// Domain authority for a portal session: only offered and accepted IDs can run,
// once per press/release. Binding changes preserve held keys that remain bound.
class WorkflowShortcutState {
public:
  explicit WorkflowShortcutState(const WorkflowShortcutPlan &plan);
  bool setBound(const QStringList &ids);
  QString press(const QString &id);
  void release(const QString &id);
  void clear();
private:
  QSet<QString> m_offered, m_bound, m_held;
};
} // namespace xerahs::workflows
