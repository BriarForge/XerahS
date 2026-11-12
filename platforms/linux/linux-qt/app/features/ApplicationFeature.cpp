// CORE-007: quit stays in the Application group, after every other command.
#include "features/ApplicationFeature.h"

#include "AppContext.h"
#include "ApplicationControl.h"
#include "CommandPalette.h"
#include "CaptureController.h"
#include "RecordingController.h"

#include <QApplication>

namespace xerahs::app {

void installApplication(AppContext &ctx) {
  auto *openPalette = ctx.commands.add(CommandSpec{QStringLiteral("command-palette"), QStringLiteral("Command palette…"),
      CommandGroup::Application, CommandSurface::Tray | CommandSurface::Window | CommandSurface::FileMenu,
      QStringLiteral("Search available XerahS commands")});
  openPalette->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  openPalette->setShortcutContext(Qt::WindowShortcut);
  QObject::connect(openPalette, &QAction::triggered, &ctx.app,
      [&ctx, palette = QPointer<CommandPalette>{}]() mutable {
    if (palette && palette->isVisible()) { palette->raise(); palette->activateWindow(); return; }
    QWidget *parent = ctx.dialogParent ? ctx.dialogParent() : nullptr;
    if (!parent || QApplication::activeModalWidget()) {
      if (ctx.notify) ctx.notify(QStringLiteral("Command palette"),
          QStringLiteral("Finish the current dialog, then open the command palette from the XerahS window."), false, false);
      return;
    }
    if (!palette) palette = new CommandPalette(ctx.commands, parent);
    palette->show(); palette->raise(); palette->activateWindow();
  });

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
