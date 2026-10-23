#pragma once
#include "CaptureWorkflows.h"
#include "settings/HotkeyBindings.h"
#include <QSet>
namespace xerahs::workflows {
struct WorkflowShortcut { QString id, description; QString preferredTrigger; };
struct WorkflowShortcutPlan { QVector<WorkflowShortcut> entries; int unavailable = 0; QString diagnostic; };
// One plan and one portal bind for built-ins and available workflows.
WorkflowShortcutPlan captureShortcutPlan(const CaptureWorkflows &catalog, const QStringList &available,
    const xerahs::settings::HotkeyBindings &bindings);
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
