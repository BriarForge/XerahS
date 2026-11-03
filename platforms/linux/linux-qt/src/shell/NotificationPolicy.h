// CORE-010: what a desktop notification may show. A notification body can carry private content (file
// paths, URLs, recognised text, device names). With private previews on, the notification keeps the
// operation title and the true result (success, or the stable failure code) and nothing else; the full
// text stays in the XerahS window. Rationale: the reference app (ShareX/XerahS) always shows the full
// text; Flameshot/Greenshot never hide it. Hiding it behind an off-by-default setting preserves that default.
#pragma once

#include <QJsonObject>
#include <QString>

namespace xerahs::shell {

inline constexpr auto kPrivateNotificationsKey = "privateNotifications";

struct NotificationText { QString title, body; };

// Leading stable code of a failure body such as "activation-busy: ..." (lowercase words joined by '-').
QString stableCodeOf(const QString &body);
NotificationText presentNotification(const QString &title, const QString &body, bool ok, bool privatePreview);
// Missing or malformed values mean "not private" (the documented default).
bool privatePreviewsEnabled(const QJsonObject &settings);

}  // namespace xerahs::shell
