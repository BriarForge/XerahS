// CORE-007: quit stays in the Application group, after every other command.
#include "features/ApplicationFeature.h"

#include "AppContext.h"
#include "ApplicationControl.h"
#include "CaptureController.h"
#include "RecordingController.h"

#include <QApplication>

namespace xerahs::app {

void installApplication(AppContext &ctx) {
  auto *quit = ctx.commands.add(CommandSpec{QStringLiteral("quit"), QStringLiteral("Quit XerahS"),
      CommandGroup::Application, CommandSurface::Tray | CommandSurface::FileMenu});
  QObject::connect(quit, &QAction::triggered, &ctx.app, [&ctx] {
    const auto diagnostic = applicationControlAvailability(ctx.capture.busy() || ctx.recording.busy());
    if (!diagnostic.isEmpty() && ctx.notify)
      ctx.notify(QStringLiteral("Quit XerahS"),
          QStringLiteral("XerahS is busy. Finish the current dialog, capture or recording, then try Quit again."), false, false);
    if (diagnostic.isEmpty()) ctx.app.quit(); // EditorShutdown enforces every editor's close policy.
  });
}

}  // namespace xerahs::app
