#pragma once
#include "PortalWorkflowShortcuts.h"
#include <QAction>
#include <QPointer>
#include <functional>
namespace xerahs::app {
// Returns false only for a non-built-in ID. A refused press is consumed,
// reported immediately, and never queued or sent to a disabled QAction.
bool routeCaptureShortcut(const QString &id, const QMap<QString, QAction *> &actions,
    const std::function<bool()> &available, const std::function<void()> &refused);
class WorkflowShortcutActions final : public QObject {
  Q_OBJECT
public:
  using PlanProvider = std::function<xerahs::workflows::WorkflowShortcutPlan()>;
  WorkflowShortcutActions(PlanProvider plan, std::function<bool()> canConfigure,
      QDBusConnection bus = QDBusConnection::sessionBus(), WorkflowShortcutPortalConfig config = {}, QObject *parent = nullptr);
  QAction *enableAction() const { return m_enable; }
  QAction *disableAction() const { return m_disable; }
  QMap<QString, QString> bindings() const { return m_portal && m_portal->active() ? m_portal->bindings() : QMap<QString, QString>{}; }
  bool pending() const { return m_portal && m_portal->pending(); }
  void invalidate();
  void changeInDesktopSettings();
signals:
  void bindingsChanged();
  void workflowRequested(const QString &id);
  void notify(const QString &body, bool success);
private:
  void configure();
  void disable(const QString &message);
  void refresh();
  PlanProvider m_plan;
  std::function<bool()> m_canConfigure;
  QDBusConnection m_bus;
  WorkflowShortcutPortalConfig m_config;
  QPointer<PortalWorkflowShortcuts> m_portal;
  QAction *m_enable, *m_disable;
};
} // namespace xerahs::app
