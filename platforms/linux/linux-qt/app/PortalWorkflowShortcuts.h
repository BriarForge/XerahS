#pragma once
#include "workflows/WorkflowShortcuts.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
namespace xerahs::app {
struct PortalShortcut { QString id; QVariantMap properties; };
using PortalShortcutList = QList<PortalShortcut>;
QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut);
void registerPortalShortcutTypes();
struct WorkflowShortcutPortalConfig {
  QString service = QStringLiteral("org.freedesktop.portal.Desktop");
  int callTimeoutMs = 5000, responseTimeoutMs = 120000;
};
// One explicit opt-in session. No global key grabs, compositor config writes,
// startup opt-in or automatic rebind after owner/session loss.
class PortalWorkflowShortcuts final : public QObject {
  Q_OBJECT
public:
  PortalWorkflowShortcuts(xerahs::workflows::WorkflowShortcutPlan plan,
      QDBusConnection bus = QDBusConnection::sessionBus(), WorkflowShortcutPortalConfig config = {}, QObject *parent = nullptr);
  ~PortalWorkflowShortcuts() override;
  void start(const QString &parentWindow = {});
  void stop();
  void configureDesktop();
  // Keeps a pending method reply alive long enough to close its returned handle.
  void retire();
  bool pending() const;
  bool active() const;
  QMap<QString, QString> bindings() const { return m_bindings; }
  QString summary() const { return m_summary; }
  QString diagnostic() const { return m_diagnostic; }
signals:
  void changed();
  void configurationResult(bool supported);
  void activated(const QString &workflowId);
private slots:
  void response(uint code, const QVariantMap &results, const QDBusMessage &message);
  void pressed(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options);
  void released(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options);
  void bindingsChanged(const QDBusObjectPath &session, const xerahs::app::PortalShortcutList &shortcuts);
  void sessionClosed(const QVariantMap &details, const QDBusMessage &message);
  void disconnected();
private:
  enum class Phase { Idle, Owner, Creating, Binding, Ready, Stopped };
  void resolveOwner(bool activate);
  void subscribe();
  void request(bool create);
  void complete(uint code, const QVariantMap &results);
  bool acceptBindings(const PortalShortcutList &shortcuts);
  void fail(const QString &diagnostic);
  void closePath(const QString &path, bool session);
  void cleanup();
  QDBusConnection m_bus;
  WorkflowShortcutPortalConfig m_config;
  xerahs::workflows::WorkflowShortcutPlan m_plan;
  xerahs::workflows::WorkflowShortcutState m_authority;
  Phase m_phase = Phase::Idle;
  QString m_owner, m_request, m_session, m_expectedSession, m_parentWindow, m_summary, m_diagnostic, m_observedOwner;
  QTimer m_timer;
  bool m_waiting = false, m_retiring = false, m_responded = false, m_ownerChanged = false;
  struct EarlyResponse { QString path; uint code; QVariantMap results; };
  QVector<EarlyResponse> m_early;
  QSet<QString> m_closed;
  QMap<QString, QString> m_bindings;
};
} // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::PortalShortcut)
Q_DECLARE_METATYPE(xerahs::app::PortalShortcutList)
