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
    call->deleteLater();
    // A live signal is newer than this startup read. Never let its delayed
    // snapshot undo a lock or unlock already observed by the scheduler.
    // Retire the call before notifying observers that may destroy this owner.
    if (!m_initialSnapshotSuperseded && !reply.isError() && reply.value().variant().typeId() == QMetaType::Bool)
      m_state.setLocked(reply.value().variant().toBool());
  });
}

void ScreenLockWatcher::screenSaverActiveChanged(bool active) {
  m_initialSnapshotSuperseded = true;
  m_state.setLocked(active);
}
void ScreenLockWatcher::sessionLocked() { screenSaverActiveChanged(true); }
void ScreenLockWatcher::sessionUnlocked() { screenSaverActiveChanged(false); }

}  // namespace xerahs::app
