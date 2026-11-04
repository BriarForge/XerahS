// CORE-010: the notification server owns native policy. No actions or media are sent.
#pragma once
#include "shell/NotificationPolicy.h"
#include <QDBusConnection>

namespace xerahs::app {
void deliverNotification(const shell::NotificationEvent &event, bool ok, int millisecondsVisible,
                         const QDBusConnection &bus = QDBusConnection::sessionBus(),
                         const QString &service = QStringLiteral("org.freedesktop.Notifications"));
}
