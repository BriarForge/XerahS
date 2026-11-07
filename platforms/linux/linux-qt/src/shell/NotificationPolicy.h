// CORE-010: payload-free desktop projection and legacy preview preference parsing.
// Native notifications always use desktopNotification; detailed bodies remain in XerahS.
#pragma once

#include <QJsonObject>
#include <QString>

namespace xerahs::shell {

inline constexpr auto kPrivateNotificationsKey = "privateNotifications";

struct NotificationText { QString title, body; };

// One opaque event ID is shared by the desktop notification and the local result.
struct NotificationEvent { QString id; NotificationText desktop; };
NotificationEvent desktopNotification(const QString &operation, const QString &body, bool ok);

// Leading stable code of a failure body such as "activation-busy: ..." (lowercase words joined by '-').
QString stableCodeOf(const QString &body);
NotificationText presentNotification(const QString &title, const QString &body, bool ok, bool privatePreview);
// Missing or malformed values mean "not private" (the documented default).
bool privatePreviewsEnabled(const QJsonObject &settings);

}  // namespace xerahs::shell
