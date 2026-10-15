#include "WorkflowShortcuts.h"
namespace xerahs::workflows {
WorkflowShortcutPlan workflowShortcutPlan(const CaptureWorkflows &catalog, const QStringList &available) {
  const auto parsed = parseCaptureWorkflows(workflowsJson(catalog));
  if (!parsed.catalog) return {{}, 0, parsed.diagnostic};
  WorkflowShortcutPlan plan;
  for (const auto &entry : parsed.catalog->entries) {
    if (!workflowAvailability(entry, available).isEmpty()) { ++plan.unavailable; continue; }
    plan.entries.append({entry.id.toString(QUuid::WithoutBraces), entry.name});
  }
  if (plan.entries.isEmpty()) plan.diagnostic = "shortcut-no-workflows";
  return plan;
}
WorkflowShortcutState::WorkflowShortcutState(const WorkflowShortcutPlan &plan) {
  if (!plan.diagnostic.isEmpty() || plan.entries.size() > 64) return;
  for (const auto &entry : plan.entries) {
    const QUuid id(entry.id);
    if (id.isNull() || id.toString(QUuid::WithoutBraces) != entry.id || m_offered.contains(entry.id)) { m_offered.clear(); return; }
    m_offered.insert(entry.id);
  }
}
bool WorkflowShortcutState::setBound(const QStringList &ids) {
  QSet<QString> next;
  for (const auto &id : ids) {
    if (!m_offered.contains(id) || next.contains(id)) return false;
    next.insert(id);
  }
  m_bound = next; m_held.intersect(m_bound); return true;
}
QString WorkflowShortcutState::press(const QString &id) {
  if (!m_bound.contains(id) || m_held.contains(id)) return {};
  m_held.insert(id); return id;
}
void WorkflowShortcutState::release(const QString &id) { m_held.remove(id); }
void WorkflowShortcutState::clear() { m_bound.clear(); m_held.clear(); }
} // namespace xerahs::workflows
