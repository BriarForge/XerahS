// CORE-010: tray notification door.
#include "AppContext.h"

#include "settings/ApplicationSettings.h"
#include "shell/NotificationPolicy.h"

#include <QSystemTrayIcon>

namespace xerahs::app {

void postTrayMessage(AppContext &ctx, const QString &title, const QString &body, bool ok, int millisecondsVisible) {
  const auto text = shell::presentNotification(title, body, ok, shell::privatePreviewsEnabled(ctx.settings.current().values));
  ctx.tray.showMessage(text.title, text.body, ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, millisecondsVisible);
}

}  // namespace xerahs::app
