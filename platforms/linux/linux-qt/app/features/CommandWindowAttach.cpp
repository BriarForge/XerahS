// CORE-007: registration finishes before the window exists. This connects the two.
#include "features/CommandWindowAttach.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "ScheduledCapture.h"
#include "features/CaptureFeature.h"
#include "features/FilesFeature.h"
#include "features/UploadFeature.h"

namespace xerahs::app {

void attachCommandWindow(AppContext &ctx, ApplicationWindow &window, QMenu &tray) {
  if (ctx.schedule) ctx.schedule->setDialogParent(&window);
  ctx.capture.setActionSelectionParent(&window);
  attachUploadWindow(ctx, window);
  insertRecentImagesMenu(tray, ctx.commands.action(QStringLiteral("captures-folder")));
  installRetainedCapture(ctx, window, tray);
}

}  // namespace xerahs::app
