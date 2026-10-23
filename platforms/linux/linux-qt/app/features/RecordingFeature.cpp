// CORE-007: recording actions keep their owner and are shared by the tray and Capture menu.
#include "features/RecordingFeature.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "RecordingActions.h"
#include "settings/ApplicationSettings.h"

#include <QApplication>
#include <QRectF>
#include <QSystemTrayIcon>

namespace xerahs::app {
namespace {

ApplicationWindow *commandWindow(AppContext &ctx) {
  return qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr);
}

}  // namespace

void startScreenRecording(AppContext &ctx) {
  if (ctx.recordingActions) ctx.recordingActions->start(xerahs::recording::Format::Mp4);
}

void startRegionRecording(AppContext &ctx) {
  if (ctx.recordingActions) ctx.recordingActions->start(xerahs::recording::Format::Mp4, true);
}

void installRecording(AppContext &ctx) {
  auto *actions = new RecordingActions(ctx.recording, ctx.tray, [&ctx] {
    return ctx.settings.current().values.value(QStringLiteral("saveFolder")).toString();
  }, &ctx.app);
  ctx.recordingActions = actions;
  actions->setInputs([&ctx] { return ctx.settings.current().values; }, [&ctx](std::function<void(QRectF)> selected) {
    ctx.capture.selectRecordingRegion(std::move(selected));
  });
  for (auto *action : actions->orderedActions()) {
    ctx.commands.adopt(CommandSpec{action->objectName(), action->text(), CommandGroup::Record,
        CommandSurface::Tray | CommandSurface::CaptureMenu}, action);
  }
  QObject::connect(&ctx.recording, &RecordingController::notify, &ctx.app,
      [&ctx](const QString &title, const QString &body, bool success) {
        if (auto *window = commandWindow(ctx)) window->showResult(title, body, success);
        ctx.tray.showMessage(title, body, success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning);
      });
  QObject::connect(&ctx.recording, &RecordingController::changed, &ctx.app, [&ctx] {
    if (auto *window = commandWindow(ctx))
      window->setWindowTitle(ctx.recording.busy() ? ctx.recording.indicator() : QStringLiteral("XerahS"));
  });
  QObject::connect(&ctx.app, &QCoreApplication::aboutToQuit, &ctx.recording, &RecordingController::abort);
}

}  // namespace xerahs::app
