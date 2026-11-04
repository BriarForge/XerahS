#include "NativeNotifications.h"
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QVariantMap>

namespace xerahs::app {
void deliverNotification(const shell::NotificationEvent &event, bool ok, int millisecondsVisible,
                         const QDBusConnection &bus, const QString &service) {
  auto call = QDBusMessage::createMethodCall(service, QStringLiteral("/org/freedesktop/Notifications"),
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
  // Normal urgency leaves suppression and lock-screen policy with the desktop.
  // There are no actions, image hints, sound overrides or alternate delivery after refusal.
  Q_UNUSED(ok);
  const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("com.xerahs.app")},
      {QStringLiteral("urgency"), QVariant::fromValue(uchar(1))}};
  call.setArguments({QStringLiteral("XerahS"), uint(0), QStringLiteral("com.xerahs.app"),
      event.desktop.title.toHtmlEscaped(), event.desktop.body.toHtmlEscaped(), QStringList{}, hints, millisecondsVisible});
  bus.asyncCall(call, 5000);
}
}
