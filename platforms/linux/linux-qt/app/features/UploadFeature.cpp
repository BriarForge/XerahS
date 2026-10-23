// CORE-007, CORE-020, CORE-024: upload commands. Window drops share the upload-file controller.
#include "features/UploadFeature.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "FileUploadController.h"
#include "UploadDestinationsDialog.h"
#include "WatchFolderService.h"
#include "WatchFoldersDialog.h"
#include "settings/ApplicationSettings.h"
#include "uploads/DestinationCatalog.h"
#include "watch/WatchFolders.h"

#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QSystemTrayIcon>

#include <memory>

namespace xerahs::app {
namespace {

struct UploadLifetime {
  QAction *uploadFile = nullptr;
  QAction *destinations = nullptr;
  QAction *watch = nullptr;
  QPointer<UploadDestinationsDialog> destinationsDialog;
  std::shared_ptr<xerahs::uploads::DestinationCatalog> catalog;
  std::unique_ptr<xerahs::watch::WatchFolders> store;
  FileUploadController *files = nullptr;
  FileUploadController *watchUploads = nullptr;
  WatchFolderService *service = nullptr;
};

UploadLifetime &lifetime() {
  static UploadLifetime state;
  return state;
}

void showDestinations(AppContext &ctx, QWidget *parent) {
  auto &state = lifetime();
  if (!state.destinationsDialog) {
    state.catalog = std::make_shared<xerahs::uploads::DestinationCatalog>(ctx.uploadCatalogPath);
    auto *dialog = new UploadDestinationsDialog(*state.catalog, *ctx.uploadCredentials, nativeUploadDestinationsPorts(), parent);
    // Keep the single non-modal window and its referenced stores until shutdown.
    QObject::connect(dialog, &QObject::destroyed, [catalog = state.catalog, credentials = ctx.uploadCredentials] {});
    state.destinationsDialog = dialog;
  }
  state.destinationsDialog->show();
  state.destinationsDialog->raise();
  state.destinationsDialog->activateWindow();
}

}  // namespace

void installUpload(AppContext &ctx) {
  auto &state = lifetime();
  state.uploadFile = ctx.commands.add(CommandSpec{QStringLiteral("upload-file"), QStringLiteral("Upload file…"),
      CommandGroup::Upload, CommandSurface::Tray | CommandSurface::Window | CommandSurface::FileMenu});
  state.destinations = ctx.commands.add(CommandSpec{QStringLiteral("upload-destinations"), QStringLiteral("Upload destinations…"),
      CommandGroup::Upload, CommandSurface::Tray | CommandSurface::Window | CommandSurface::PreferencesMenu});
  state.watch = ctx.commands.add(CommandSpec{QStringLiteral("watch-folders"), QStringLiteral("Watch folders…"),
      CommandGroup::Upload, CommandSurface::Tray});
  QObject::connect(state.destinations, &QAction::triggered, &ctx.app, [&ctx] {
    showDestinations(ctx, ctx.dialogParent ? ctx.dialogParent() : nullptr);
  });
}

void attachUploadWindow(AppContext &ctx, ApplicationWindow &window) {
  auto &state = lifetime();
  state.files = new FileUploadController(FileUploadPorts{ctx.uploadCatalogPath, ctx.uploadCredentials,
      [&ctx] { showDestinations(ctx, ctx.dialogParent ? ctx.dialogParent() : nullptr); }, {}}, &window, &window);
  QObject::connect(state.uploadFile, &QAction::triggered, state.files, &FileUploadController::chooseAndUpload);
  ctx.uploadFiles = [files = QPointer<FileUploadController>(state.files)](const QStringList &paths) { if (files) files->upload(paths); };
  QObject::connect(&window, &ApplicationWindow::filesDropped, state.files, &FileUploadController::upload);

  state.store = std::make_unique<xerahs::watch::WatchFolders>(
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("watch")));
  state.watchUploads = new FileUploadController(FileUploadPorts{ctx.uploadCatalogPath, ctx.uploadCredentials,
      [&ctx] { showDestinations(ctx, ctx.dialogParent ? ctx.dialogParent() : nullptr); }, {}}, &window, &ctx.app);
  state.service = new WatchFolderService(*state.store, *state.watchUploads, [&ctx] {
    return ctx.settings.current().values.value(QStringLiteral("saveFolder")).toString();
  }, {}, &ctx.app);
  QObject::connect(state.service, &WatchFolderService::notify, &window, [&ctx](const QString &message, bool success) {
    if (auto *home = qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr))
      home->showResult(QStringLiteral("Watch folders"), message, success);
    ctx.tray.showMessage(QStringLiteral("Watch folders"), message, success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning);
  });
  QObject::connect(state.watch, &QAction::triggered, &window, [&ctx, &window] {
    WatchFoldersDialog dialog(*lifetime().service, &window);
    dialog.exec();
  });
  QObject::connect(&ctx.app, &QCoreApplication::aboutToQuit, state.service, &WatchFolderService::stop);
  state.service->start();
}

}  // namespace xerahs::app
