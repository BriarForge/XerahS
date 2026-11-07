// CORE-016: feeds ScreenLockState from the desktop. Session bus: org.freedesktop.ScreenSaver and
// org.gnome.ScreenSaver ActiveChanged(bool). System bus: org.freedesktop.login1.Session Lock/Unlock
// signals and the LockedHint of the caller's session. Both buses are injected.
#pragma once

#include "capture/CaptureConditions.h"

#include <QDBusConnection>
#include <QObject>

namespace xerahs::app {

class ScreenLockWatcher final : public QObject {
  Q_OBJECT
public:
  ScreenLockWatcher(xerahs::capture::ScreenLockState &state, const QDBusConnection &session, const QDBusConnection &system,
      QObject *parent = nullptr);
  bool connected() const { return m_connected; }
private slots:
  void screenSaverActiveChanged(bool active);
  void sessionLocked();
  void sessionUnlocked();
private:
  xerahs::capture::ScreenLockState &m_state;
  bool m_connected = false;
  bool m_initialSnapshotSuperseded = false;
};

}  // namespace xerahs::app
