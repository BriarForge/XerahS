#include "PortalWorkflowShortcuts.h"
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QUuid>
#include <algorithm>
namespace xerahs::app {
namespace {
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
const QString portal = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");
const QString sessionInterface = QStringLiteral("org.freedesktop.portal.Session");
QString senderPart(const QDBusConnection &bus) { auto sender = bus.baseService().mid(1); return sender.replace('.', '_'); }
bool ownedPath(const QDBusConnection &bus, const QString &path, bool session) {
  const auto prefix = root + (session ? "/session/" : "/request/") + senderPart(bus) + '/';
  const auto tail = path.mid(prefix.size());
  return path.startsWith(prefix) && !tail.isEmpty() && !tail.contains('/') && path.size() <= 512;
}
}
QDBusArgument &operator<<(QDBusArgument &a, const PortalShortcut &s) { a.beginStructure(); a << s.id << s.properties; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, PortalShortcut &s) { a.beginStructure(); a >> s.id >> s.properties; a.endStructure(); return a; }
void registerPortalShortcutTypes() {
  qRegisterMetaType<QDBusMessage>(); qRegisterMetaType<QVariantMap>();
  qDBusRegisterMetaType<PortalShortcut>(); qDBusRegisterMetaType<PortalShortcutList>();
}
PortalWorkflowShortcuts::PortalWorkflowShortcuts(xerahs::workflows::WorkflowShortcutPlan plan, QDBusConnection bus,
    WorkflowShortcutPortalConfig config, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_config(std::move(config)), m_plan(std::move(plan)), m_authority(m_plan) {
  registerPortalShortcutTypes(); m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, [this] { fail("shortcut-timeout"); });
}
PortalWorkflowShortcuts::~PortalWorkflowShortcuts() { cleanup(); }
bool PortalWorkflowShortcuts::pending() const { return m_phase != Phase::Idle && m_phase != Phase::Ready && m_phase != Phase::Stopped; }
bool PortalWorkflowShortcuts::active() const { return m_phase == Phase::Ready; }
void PortalWorkflowShortcuts::start(const QString &parentWindow) {
  if (m_phase != Phase::Idle) return;
  if (!m_plan.diagnostic.isEmpty() || m_plan.entries.isEmpty() || m_plan.entries.size() > 64) { fail("shortcut-no-workflows"); return; }
  // Reject malformed plans even when a native caller bypasses the store helper.
  QStringList offered; for (const auto &s : m_plan.entries) {
    if (s.description.isEmpty() || s.description.size() > 128) { fail("shortcut-invalid"); return; }
    offered.append(s.id);
  }
  if (!m_authority.setBound(offered)) { fail("shortcut-invalid"); return; } m_authority.clear();
  if (!m_bus.isConnected()) { fail("shortcut-unavailable"); return; }
  m_parentWindow = parentWindow; m_phase = Phase::Owner;
  auto *owner = new QDBusServiceWatcher(m_config.service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(owner, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &next) {
    m_ownerChanged = true; m_observedOwner = next;
    if (!m_owner.isEmpty() && next != m_owner) fail("shortcut-unavailable");
  });
  emit changed(); if (m_phase != Phase::Stopped) resolveOwner(true);
}
void PortalWorkflowShortcuts::resolveOwner(bool activate) {
  m_timer.start(std::max(1, m_config.callTimeoutMs));
  auto message = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner"); message << m_config.service;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, activate](QDBusPendingCallWatcher *call) {
    const QDBusPendingReply<QString> reply = *call; call->deleteLater(); if (m_phase == Phase::Stopped) return;
    if (reply.isError()) {
      if (!activate || reply.error().name() != "org.freedesktop.DBus.Error.NameHasNoOwner") { fail("shortcut-unavailable"); return; }
      auto message = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "StartServiceByName"); message << m_config.service << uint(0);
      auto *start = new QDBusPendingCallWatcher(m_bus.asyncCall(message, std::max(1, m_config.callTimeoutMs)), this);
      connect(start, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<uint> reply = *call; call->deleteLater(); if (m_phase == Phase::Stopped) return;
        if (reply.isError()) fail("shortcut-unavailable"); else resolveOwner(false);
      }); return;
    }
    m_owner = reply.value(); if (!m_owner.startsWith(':') || (m_ownerChanged && m_observedOwner != m_owner)) { fail("shortcut-unavailable"); return; }
    auto *owner = new QDBusServiceWatcher(m_owner, m_bus, QDBusServiceWatcher::WatchForUnregistration, this);
    connect(owner, &QDBusServiceWatcher::serviceUnregistered, this, [this] { fail("shortcut-unavailable"); });
    subscribe(); if (m_phase != Phase::Stopped) request(true);
  });
}
void PortalWorkflowShortcuts::subscribe() {
  const bool ok = m_bus.connect(m_owner, {}, requestInterface, "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage))) &&
      m_bus.connect(m_owner, root, portal, "Activated", this, SLOT(pressed(QDBusObjectPath,QString,qulonglong,QVariantMap))) &&
      m_bus.connect(m_owner, root, portal, "Deactivated", this, SLOT(released(QDBusObjectPath,QString,qulonglong,QVariantMap))) &&
      m_bus.connect(m_owner, root, portal, "ShortcutsChanged", this, SLOT(bindingsChanged(QDBusObjectPath,xerahs::app::PortalShortcutList))) &&
      m_bus.connect(m_owner, {}, sessionInterface, "Closed", this, SLOT(sessionClosed(QVariantMap,QDBusMessage)));
  m_bus.connect({}, "/org/freedesktop/DBus/Local", "org.freedesktop.DBus.Local", "Disconnected", this, SLOT(disconnected()));
  if (!ok) fail("shortcut-unavailable");
}
void PortalWorkflowShortcuts::request(bool create) {
  m_phase = create ? Phase::Creating : Phase::Binding;
  const auto token = "xerahs" + QUuid::createUuid().toString(QUuid::Id128);
  m_request = root + "/request/" + senderPart(m_bus) + '/' + token; m_responded = false; m_early.clear(); m_waiting = true;
  QVariantMap options{{"handle_token", token}};
  auto message = QDBusMessage::createMethodCall(m_owner, root, portal, create ? "CreateSession" : "BindShortcuts");
  if (create) {
    const auto sessionToken = "xerahs" + QUuid::createUuid().toString(QUuid::Id128);
    options.insert("session_handle_token", sessionToken); m_expectedSession = root + "/session/" + senderPart(m_bus) + '/' + sessionToken;
    message << options;
  } else {
    PortalShortcutList shortcuts;
    for (const auto &entry : m_plan.entries) {
      QVariantMap properties{{"description", entry.description}};
      // Portal preferred_trigger uses XDG accelerator syntax, not Qt key codes.
      if (!entry.preferredTrigger.isEmpty()) {
        QString trigger = entry.preferredTrigger;
        trigger.replace("Ctrl+", "CTRL+").replace("Alt+", "ALT+").replace("Shift+", "SHIFT+").replace("Meta+", "LOGO+");
        auto parts = trigger.split('+');
        auto key = parts.takeLast();
        if (key.size() == 1 && key.front().isLetter()) key = key.toLower();
        if (key == "Space") key = "space";
        if (key == "PageUp") key = "Page_Up";
        if (key == "PageDown") key = "Page_Down";
        parts.append(key);
        properties.insert("preferred_trigger", parts.join('+'));
      }
      shortcuts.append({entry.id, properties});
    }
    message << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(shortcuts) << m_parentWindow << options;
  }
  m_timer.start(std::max(1, m_config.callTimeoutMs));
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
    const QDBusPendingReply<QDBusObjectPath> reply = *call; call->deleteLater(); m_waiting = false;
    if (reply.isError()) { if (m_phase != Phase::Stopped) fail("shortcut-unavailable"); }
    else if (!ownedPath(m_bus, reply.value().path(), false)) { if (m_phase != Phase::Stopped) fail("shortcut-invalid"); }
    else {
      m_request = reply.value().path();
      if (m_phase == Phase::Stopped) {
        closePath(m_request, false);
        // A cancelled CreateSession may have created its session before the
        // method handle arrived. Close our nonce path again after that reply.
        m_closed.remove(m_expectedSession); closePath(m_expectedSession, true);
      }
      else {
        m_timer.start(std::max(1, m_config.responseTimeoutMs));
        const auto early = std::move(m_early); m_early.clear();
        for (const auto &r : early) if (r.path == m_request) { complete(r.code, r.results); break; }
      }
    }
    if (m_retiring && !m_waiting) deleteLater();
  });
}
void PortalWorkflowShortcuts::response(uint code, const QVariantMap &results, const QDBusMessage &message) {
  if (message.service() != m_owner || !ownedPath(m_bus, message.path(), false)) return;
  if (m_phase == Phase::Stopped) {
    if (message.path() == m_request && results.value("session_handle").toString() == m_expectedSession) closePath(m_expectedSession, true);
    return;
  }
  if (m_phase != Phase::Creating && m_phase != Phase::Binding) return;
  if (m_waiting) {
    if (m_early.size() >= 4) { fail("shortcut-invalid"); return; }
    m_early.append({message.path(), code, results}); return;
  }
  if (message.path() == m_request) complete(code, results);
}
void PortalWorkflowShortcuts::complete(uint code, const QVariantMap &results) {
  m_responded = true; m_timer.stop();
  if (code != 0) { fail(code == 1 ? "shortcut-cancelled" : "shortcut-denied"); return; }
  if (m_phase == Phase::Creating) {
    const auto session = results.value("session_handle");
    if (session.metaType().id() != QMetaType::QString || session.toString() != m_expectedSession) { fail("shortcut-invalid"); return; }
    m_session = session.toString(); request(false); return;
  }
  const auto value = results.value("shortcuts");
  if (value.metaType() != QMetaType::fromType<QDBusArgument>() || value.value<QDBusArgument>().currentSignature() != "a(sa{sv})" || !acceptBindings(qdbus_cast<PortalShortcutList>(value))) { fail("shortcut-invalid"); return; }
  m_phase = Phase::Ready; emit changed();
}
bool PortalWorkflowShortcuts::acceptBindings(const PortalShortcutList &shortcuts) {
  if (shortcuts.size() > m_plan.entries.size()) return false;
  QStringList ids, lines; QMap<QString, QString> bindings;
  for (const auto &s : shortcuts) {
    const auto trigger = s.properties.value("trigger_description");
    if (trigger.isValid() && (trigger.metaType().id() != QMetaType::QString || trigger.toString().size() > 512)) return false;
    ids.append(s.id); bindings.insert(s.id, trigger.toString().isEmpty() ? QStringLiteral("Desktop shortcut (trigger not reported)") : trigger.toString());
    for (const auto &entry : m_plan.entries) if (entry.id == s.id) lines.append(entry.description + " — " + (trigger.toString().isEmpty() ? QStringLiteral("Desktop shortcut") : trigger.toString()));
  }
  if (!m_authority.setBound(ids)) return false;
  m_bindings = bindings;
  m_summary = lines.isEmpty() ? QStringLiteral("No workflow shortcuts were bound by the desktop.") : lines.join('\n');
  if (m_plan.unavailable) m_summary += QString("\n%1 unavailable saved workflow(s) were not offered.").arg(m_plan.unavailable);
  return true;
}
void PortalWorkflowShortcuts::pressed(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &) {
  if (!active() || session.path() != m_session) return;
  const auto workflow = m_authority.press(id); if (!workflow.isEmpty()) emit activated(workflow);
}
void PortalWorkflowShortcuts::released(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &) {
  if (active() && session.path() == m_session) m_authority.release(id);
}
void PortalWorkflowShortcuts::bindingsChanged(const QDBusObjectPath &session, const PortalShortcutList &shortcuts) {
  if (!active() || session.path() != m_session) return;
  if (!acceptBindings(shortcuts)) fail("shortcut-invalid"); else emit changed();
}
void PortalWorkflowShortcuts::sessionClosed(const QVariantMap &, const QDBusMessage &message) {
  if (message.service() == m_owner && message.path() == m_session) fail("shortcut-session-closed");
}
void PortalWorkflowShortcuts::disconnected() { fail("shortcut-unavailable"); }
void PortalWorkflowShortcuts::closePath(const QString &path, bool session) {
  if (m_owner.isEmpty() || !ownedPath(m_bus, path, session) || m_closed.contains(path)) return;
  m_closed.insert(path); m_bus.asyncCall(QDBusMessage::createMethodCall(m_owner, path, session ? sessionInterface : requestInterface, "Close"));
}
void PortalWorkflowShortcuts::cleanup() {
  m_timer.stop(); m_authority.clear();
  if (!m_responded) closePath(m_request, false);
  closePath(m_session.isEmpty() ? m_expectedSession : m_session, true);
  for (const auto &r : m_early) if (r.path == m_request && r.results.value("session_handle").toString() == m_expectedSession) closePath(m_expectedSession, true);
  m_early.clear();
}
void PortalWorkflowShortcuts::fail(const QString &diagnostic) {
  if (m_phase == Phase::Stopped) return;
  m_phase = Phase::Stopped; m_diagnostic = diagnostic; cleanup(); emit changed();
}
void PortalWorkflowShortcuts::configureDesktop() {
  if (!active()) return;
  auto message = QDBusMessage::createMethodCall(m_owner, root, portal, "ConfigureShortcuts");
  message << QVariant::fromValue(QDBusObjectPath(m_session)) << m_parentWindow << QVariantMap{};
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, m_config.callTimeoutMs), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
    const bool supported = call->reply().type() != QDBusMessage::ErrorMessage;
    call->deleteLater(); if (active()) emit configurationResult(supported);
  });
}
void PortalWorkflowShortcuts::stop() { fail("shortcut-disabled"); }
void PortalWorkflowShortcuts::retire() { m_retiring = true; stop(); if (!m_waiting) deleteLater(); }
} // namespace xerahs::app
