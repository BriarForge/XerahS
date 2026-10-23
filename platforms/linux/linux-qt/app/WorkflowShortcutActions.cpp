#include "WorkflowShortcutActions.h"
namespace xerahs::app {
bool routeCaptureShortcut(const QString &id, const QMap<QString, QAction *> &actions,
    const std::function<bool()> &available, const std::function<void()> &refused) {
  auto *action = actions.value(id); if (!action) return false;
  if (!action->isEnabled() || (available && !available())) { if (refused) refused(); return true; }
  action->trigger(); return true;
}
namespace {
QString messageFor(const QString &code) {
  if (code == "shortcut-cancelled") return "Desktop shortcut setup was cancelled. Enable workflow shortcuts to try again.";
  if (code == "shortcut-denied") return "The desktop refused shortcut setup. Review its shortcut permissions, then try again.";
  if (code == "shortcut-timeout") return "The desktop did not finish shortcut setup in time. The request was closed; enable shortcuts to retry.";
  if (code == "shortcut-no-workflows") return "No saved workflows are available for shortcuts. Save a supported region workflow first.";
  if (code == "shortcut-session-closed") return "The desktop closed the shortcut session. Enable workflow shortcuts to reconnect.";
  if (code == "shortcut-invalid") return "The desktop returned invalid shortcut data. The session was closed; no shortcut will run.";
  return "The desktop GlobalShortcuts portal is unavailable. Use Run selected or the workflow CLI; enable shortcuts to retry after the desktop service is available.";
}
}
WorkflowShortcutActions::WorkflowShortcutActions(PlanProvider plan, std::function<bool()> canConfigure,
    QDBusConnection bus, WorkflowShortcutPortalConfig config, QObject *parent)
    : QObject(parent), m_plan(std::move(plan)), m_canConfigure(std::move(canConfigure)), m_bus(std::move(bus)), m_config(std::move(config)) {
  m_enable = new QAction("Enable capture and workflow shortcuts…", this); m_enable->setObjectName("enable-workflow-shortcuts");
  m_enable->setToolTip("Ask the desktop to bind capture actions and saved workflows for this session. Preferred triggers are suggestions; the desktop and user decide. Enable again after restarting.");
  m_disable = new QAction("Disable capture and workflow shortcuts", this); m_disable->setObjectName("disable-workflow-shortcuts");
  m_disable->setToolTip("Close this session's bindings. Saved workflows and the desktop's remembered choices are kept.");
  connect(m_enable, &QAction::triggered, this, &WorkflowShortcutActions::configure);
  connect(m_disable, &QAction::triggered, this, [this] { disable("Workflow shortcuts disabled for this session."); }); refresh();
}
void WorkflowShortcutActions::configure() {
  if (pending()) return;
  if (m_canConfigure && !m_canConfigure()) { emit notify("XerahS is busy. Configure shortcuts after the current capture or dialog finishes.", false); return; }
  xerahs::workflows::WorkflowShortcutPlan plan;
  try { if (m_plan) plan = m_plan(); else plan.diagnostic = "shortcut-no-workflows"; }
  catch (...) { plan.diagnostic = "shortcut-no-workflows"; }
  if (!plan.diagnostic.isEmpty()) {
    emit notify(plan.diagnostic == "shortcut-no-workflows" ? messageFor(plan.diagnostic) : QStringLiteral("Saved workflows could not be loaded unchanged. Open Capture workflows and Reload before enabling shortcuts."), false); return;
  }
  if (m_portal) { auto *old = m_portal.data(); m_portal.clear(); old->retire(); }
  auto *portal = new PortalWorkflowShortcuts(std::move(plan), m_bus, m_config, this); m_portal = portal;
  connect(portal, &PortalWorkflowShortcuts::activated, this, [this, portal](const QString &id) { if (m_portal == portal) emit workflowRequested(id); });
  connect(portal, &PortalWorkflowShortcuts::changed, this, [this, portal] {
    if (m_portal != portal) return;
    refresh(); emit bindingsChanged();
    if (portal->active()) emit notify("Workflow shortcut bindings for this session:\n" + portal->summary(), true);
    else if (portal->pending()) emit notify("Waiting for desktop shortcut setup. Choose bindings in the desktop dialog, or Disable workflow shortcuts to cancel.", true);
    else {
      const auto diagnostic = portal->diagnostic(); m_portal.clear(); portal->retire(); refresh(); emit notify(messageFor(diagnostic), false);
    }
  });
  connect(portal, &PortalWorkflowShortcuts::configurationResult, this, [this, portal](bool supported) {
    if (m_portal != portal) return;
    emit notify(supported ? "Review capture and workflow bindings in the desktop shortcut settings." :
      "This desktop cannot open its shortcut editor through the portal. Open the desktop Keyboard/Shortcuts settings, or disable and enable shortcuts to request setup again.", supported);
  });
  portal->start();
}
void WorkflowShortcutActions::disable(const QString &message) {
  if (!m_portal) return;
  auto *old = m_portal.data(); m_portal.clear(); old->retire(); refresh(); emit bindingsChanged(); emit notify(message, true);
}
void WorkflowShortcutActions::changeInDesktopSettings() {
  if (m_canConfigure && !m_canConfigure()) { emit notify("XerahS is busy. Open desktop shortcut settings after the current capture or dialog finishes.", false); return; }
  if (m_portal && m_portal->active()) m_portal->configureDesktop();
  else configure();
}
void WorkflowShortcutActions::invalidate() { disable("Saved workflows changed. Shortcuts were disabled; enable them again to review the current workflow list."); }
void WorkflowShortcutActions::refresh() {
  m_enable->setText(pending() ? "Setting up capture and workflow shortcuts…" : m_portal && m_portal->active() ? "Configure capture and workflow shortcuts…" : "Enable capture and workflow shortcuts…");
  m_enable->setEnabled(!pending()); m_disable->setEnabled(bool(m_portal));
}
} // namespace xerahs::app
