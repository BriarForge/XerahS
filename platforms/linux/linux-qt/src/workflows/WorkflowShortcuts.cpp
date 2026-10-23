#include "WorkflowShortcuts.h"
namespace xerahs::workflows {
WorkflowShortcutPlan workflowShortcutPlan(const CaptureWorkflows &catalog, const QStringList &available) {
  const auto parsed = parseCaptureWorkflows(workflowsJson(catalog));
  if (!parsed.catalog) return {{}, 0, parsed.diagnostic};
  WorkflowShortcutPlan plan;
  for (const auto &entry : parsed.catalog->entries) {
    if (!workflowAvailability(entry, available).isEmpty()) { ++plan.unavailable; continue; }
    plan.entries.append({entry.id.toString(QUuid::WithoutBraces), entry.name, {}});
  }
  if (plan.entries.isEmpty()) plan.diagnostic = "shortcut-no-workflows";
  return plan;
}
WorkflowShortcutPlan captureShortcutPlan(const CaptureWorkflows &catalog, const QStringList &available,
    const xerahs::settings::HotkeyBindings &bindings) {
  const auto validation = bindings.validate();
  if (!validation.ok) return {{}, 0, validation.diagnostic + ": " + validation.conflictingAction};
  auto plan = workflowShortcutPlan(catalog, available);
  if (plan.diagnostic == "shortcut-no-workflows") plan.diagnostic.clear();
  if (!plan.diagnostic.isEmpty()) return plan;
  for (auto &entry : plan.entries) entry.preferredTrigger = bindings.entries.value(entry.id);
  for (const auto &action : xerahs::settings::captureHotkeyActions())
    plan.entries.prepend({action.id, action.label, bindings.entries.value(action.id)});
  return plan;
}
WorkflowShortcutState::WorkflowShortcutState(const WorkflowShortcutPlan &plan) {
  if (!plan.diagnostic.isEmpty() || plan.entries.size() > 68) return;
  for (const auto &entry : plan.entries) {
    if (!xerahs::settings::stableActionId(entry.id) || m_offered.contains(entry.id)) { m_offered.clear(); return; }
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
