// CORE-007: quit stays in the Application group, after every other command.
#include "features/ApplicationFeature.h"

#include "AppContext.h"

#include <QApplication>

namespace xerahs::app {

void installApplication(AppContext &ctx) {
  auto *quit = ctx.commands.add(CommandSpec{QStringLiteral("quit"), QStringLiteral("Quit XerahS"),
      CommandGroup::Application, CommandSurface::Tray | CommandSurface::FileMenu});
  QObject::connect(quit, &QAction::triggered, &ctx.app, &QApplication::quit);
}

}  // namespace xerahs::app
