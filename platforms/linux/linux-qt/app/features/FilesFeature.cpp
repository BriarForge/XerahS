// CORE-007: file commands. The recent-images submenu stays immediately before Open captures folder.
#include "features/FilesFeature.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "CaptureHistoryWindow.h"
#include "CaptureWorkflows.h"
#include "EditorOpen.h"
#include "EditorWindow.h"
#include "PipelineHistoryDialog.h"
#include "RecentImageMenu.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QPointer>
#include <QUrl>

#include <memory>

namespace xerahs::app {
namespace {

ApplicationWindow *commandWindow(AppContext &ctx) {
  return qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr);
}

CommandSurfaces fileSurfaces() {
  return CommandSurface::Tray | CommandSurface::Window | CommandSurface::FileMenu;
}

}  // namespace

void insertRecentImagesMenu(QMenu &menu, QAction *before) {
  if (!before) return;
  auto *recent = addRecentImagesMenu(&menu, [](const QString &path) {
    if (auto *window = EditorWindow::open(path)) window->show();
  }, [] { manageRecentImages(); });
  menu.removeAction(recent->menuAction());
  menu.insertMenu(before, recent);
}

void installFiles(AppContext &ctx) {
  auto *created = ctx.commands.add(CommandSpec{QStringLiteral("new-image"), QStringLiteral("New image…"),
      CommandGroup::Files, fileSurfaces()});
  QObject::connect(created, &QAction::triggered, &ctx.app, [] {
    if (auto *window = EditorWindow::createNew()) window->show();
  });
  auto *open = ctx.commands.add(CommandSpec{QStringLiteral("open-image"), QStringLiteral("Open image…"),
      CommandGroup::Files, fileSurfaces()});
  QObject::connect(open, &QAction::triggered, &ctx.app, [&ctx] {
    const auto path = QFileDialog::getOpenFileName(commandWindow(ctx), QStringLiteral("Open image"),
        ctx.capture.saveDirectory(), imageFileFilter());
    if (!path.isEmpty()) ctx.invoke(xerahs::shell::ActivationAction::EditImage, path, false, {}, QStringLiteral("native-shell"));
  });
  auto *folder = ctx.commands.add(CommandSpec{QStringLiteral("captures-folder"), QStringLiteral("Open captures folder"),
      CommandGroup::Files, fileSurfaces()});
  QObject::connect(folder, &QAction::triggered, &ctx.app, [&ctx] {
    const auto choice = ctx.workflows->resolve();
    auto *window = commandWindow(ctx);
    if (!choice.workflow) {
      if (window) window->showResult(QStringLiteral("Capture folder unavailable"), workflowMessage(choice.diagnostic), false);
      return;
    }
    const auto directory = choice.workflow->output.directory.isEmpty() ? ctx.capture.saveDirectory() : choice.workflow->output.directory;
    if (!QDir().mkpath(directory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
      if (window) window->showResult(QStringLiteral("Capture folder unavailable"),
          QStringLiteral("The saved default workflow's folder could not be opened. Review its save folder in Capture workflows."), false);
  });

  auto historyWindow = std::make_shared<QPointer<CaptureHistoryWindow>>();
  auto historyDialog = std::make_shared<QPointer<PipelineHistoryDialog>>();
  auto *history = ctx.commands.add(CommandSpec{QStringLiteral("capture-history"), QStringLiteral("Capture history…"),
      CommandGroup::Files, fileSurfaces()});
  QObject::connect(history, &QAction::triggered, &ctx.app, [&ctx, historyWindow, historyDialog] {
    if (*historyWindow) { (*historyWindow)->show(); (*historyWindow)->raise(); (*historyWindow)->activateWindow(); return; }
    auto *parent = commandWindow(ctx);
    *historyWindow = new CaptureHistoryWindow(ctx.journal, [&ctx] { return ctx.capture.busy(); },
        [](const QString &path, QWidget *owner) { if (auto *editor = EditorWindow::open(path, owner)) editor->show(); }, parent);
    (*historyWindow)->setRecordsOpener([historyDialog, &ctx, historyWindow] {
      if (*historyDialog) { (*historyDialog)->show(); (*historyDialog)->raise(); (*historyDialog)->activateWindow(); return; }
      *historyDialog = new PipelineHistoryDialog(ctx.journal, [&ctx] { return ctx.capture.busy(); }, historyWindow->data());
      (*historyDialog)->show();
    });
    (*historyWindow)->show();
  });
  QObject::connect(&ctx.capture, &CaptureController::busyChanged, &ctx.app, [historyWindow](bool busy) {
    if (!busy && *historyWindow) (*historyWindow)->refresh();
  });
}

}  // namespace xerahs::app
