// CORE-007: recording actions keep their owner and are shared by the tray and Capture menu.
#include "features/RecordingFeature.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "RecordingActions.h"
#include "features/CaptureFeature.h"
#include "settings/ApplicationSettings.h"

#include <QApplication>
#include <QRectF>
#include <QSystemTrayIcon>

namespace xerahs::app {
namespace {

ApplicationWindow *commandWindow(AppContext &ctx) {
  return qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr);
}

void reportActivationBusy(AppContext &ctx) {
  if (auto *window = commandWindow(ctx)) {
    window->showResult(QStringLiteral("Activation"), activationFailureMessage(QStringLiteral("activation-busy")), false);
    window->activate();
  }
}

bool recordingVideoAvailable(const AppContext &ctx) {
  return ctx.recording.unavailable(xerahs::recording::Format::Mp4).isEmpty()
      || ctx.recording.unavailable(xerahs::recording::Format::WebM).isEmpty();
}

}  // namespace

void startScreenRecording(AppContext &ctx) {
  if (!ctx.recordingActions) return;
  // Admitted dispatch owns the reservation (m_dispatching). Do not refuse it here.
  const auto format = ctx.recordingActions->takeRequestedFormat(xerahs::recording::Format::Mp4);
  ctx.recordingActions->beginAdmittedStart(format, false, ctx.invocationOrigin);
}

void startRegionRecording(AppContext &ctx) {
  if (!ctx.recordingActions) return;
  const auto format = ctx.recordingActions->takeRequestedFormat(xerahs::recording::Format::Mp4);
  ctx.recordingActions->beginAdmittedStart(format, true, ctx.invocationOrigin);
}

void installRecording(AppContext &ctx) {
  auto *actions = new RecordingActions(ctx.recording, ctx.tray, [&ctx] {
    return ctx.settings.current().values.value(QStringLiteral("saveFolder")).toString();
  }, &ctx.app);
  ctx.recordingActions = actions;
  actions->setForeignAdmission([&ctx] {
    return ctx.activation && ctx.activation->reserved();
  }, [&ctx] { reportActivationBusy(ctx); });
  // Clicks admit through the shared dispatcher. The dispatch body is startScreenRecording
  // or startRegionRecording, which must keep running while its own reservation is visible.
  actions->setStartRoute([&ctx](xerahs::recording::Format format, bool region) {
    if (ctx.recordingActions->blockedByForeignAdmission()) { reportActivationBusy(ctx); return; }
    const bool gifOnly = format == xerahs::recording::Format::Gif && !region && !recordingVideoAvailable(ctx);
    if (gifOnly || !ctx.invoke) {
      ctx.recordingActions->start(format, region, ctx.invocationOrigin);
      return;
    }
    ctx.recordingActions->armRequestedFormat(format);
    ctx.invoke(region ? xerahs::shell::ActivationAction::StartRegionRecording : xerahs::shell::ActivationAction::StartRecording,
        {}, false, {}, QStringLiteral("native-shell"));
    ctx.recordingActions->disarmRequestedFormat();
  });
  actions->setInputs([&ctx] { return ctx.settings.current().values; }, [&ctx](std::function<void(QRectF)> selected) {
    if (ctx.recordingActions->blockedByForeignAdmission()) { reportActivationBusy(ctx); return; }
    ctx.capture.selectRecordingRegion(std::move(selected));
  });
  for (auto *action : actions->orderedActions()) {
    ctx.commands.adopt(CommandSpec{action->objectName(), action->text(), CommandGroup::Record,
        CommandSurface::Tray | CommandSurface::CaptureMenu}, action);
  }
  QObject::connect(&ctx.recording, &RecordingController::notify, &ctx.app,
      [&ctx](const QString &title, const QString &body, bool success) {
        postTrayMessage(ctx, title, body, success);
      });
  QObject::connect(&ctx.recording, &RecordingController::activityChanged, &ctx.app, [&ctx](const QString &text) {
    if (auto *window = commandWindow(ctx)) window->showProgress(text, -1);
  });
  QObject::connect(&ctx.recording, &RecordingController::changed, &ctx.app, [&ctx] {
    if (auto *window = commandWindow(ctx))
      window->setWindowTitle(ctx.recording.busy() ? ctx.recording.indicator() : QStringLiteral("XerahS"));
  });
  QObject::connect(&ctx.app, &QCoreApplication::aboutToQuit, &ctx.recording, &RecordingController::abort);
}

}  // namespace xerahs::app
