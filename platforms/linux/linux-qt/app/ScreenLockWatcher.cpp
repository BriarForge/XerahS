#include "ScreenLockWatcher.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace xerahs::app {

ScreenLockWatcher::ScreenLockWatcher(xerahs::capture::ScreenLockState &state, const QDBusConnection &session,
    const QDBusConnection &system, QObject *parent)
    : QObject(parent), m_state(state) {
  auto sessionBus = session;
  auto systemBus = system;
  bool any = false;
  for (const auto &interface : {QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("org.gnome.ScreenSaver")})
    any |= sessionBus.connect({}, {}, interface, QStringLiteral("ActiveChanged"), this, SLOT(screenSaverActiveChanged(bool)));
  any |= systemBus.connect(QStringLiteral("org.freedesktop.login1"), {}, QStringLiteral("org.freedesktop.login1.Session"),
      QStringLiteral("Lock"), this, SLOT(sessionLocked()));
  any |= systemBus.connect(QStringLiteral("org.freedesktop.login1"), {}, QStringLiteral("org.freedesktop.login1.Session"),
      QStringLiteral("Unlock"), this, SLOT(sessionUnlocked()));
  m_connected = any;
  // Initial state: the session's LockedHint (absent on a bus without logind; then the signals decide).
  auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
      QStringLiteral("/org/freedesktop/login1/session/auto"), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
  message << QStringLiteral("org.freedesktop.login1.Session") << QStringLiteral("LockedHint");
  auto *watcher = new QDBusPendingCallWatcher(systemBus.asyncCall(message, 2000), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
    QDBusPendingReply<QDBusVariant> reply = *call;
    if (!reply.isError() && reply.value().variant().typeId() == QMetaType::Bool) m_state.setLocked(reply.value().variant().toBool());
    call->deleteLater();
  });
}

void ScreenLockWatcher::screenSaverActiveChanged(bool active) { m_state.setLocked(active); }
void ScreenLockWatcher::sessionLocked() { m_state.setLocked(true); }
void ScreenLockWatcher::sessionUnlocked() { m_state.setLocked(false); }

}  // namespace xerahs::app
