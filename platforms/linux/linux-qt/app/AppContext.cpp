// CORE-010: tray notification door.
#include "AppContext.h"

#include "ApplicationWindow.h"
#include "NativeNotifications.h"
#include "shell/NotificationPolicy.h"


namespace xerahs::app {

void postTrayMessage(AppContext &ctx, const QString &title, const QString &body, bool ok, int millisecondsVisible, bool desktop) {
  const auto event = shell::desktopNotification(title, body, ok);
  if (auto *window = qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr))
    window->showResult(event.desktop.title, body, ok);
  if (desktop) deliverNotification(event, ok, millisecondsVisible);
}

}  // namespace xerahs::app
