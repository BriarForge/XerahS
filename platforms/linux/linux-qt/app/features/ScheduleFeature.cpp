// CORE-007, CORE-016: schedule commands. raiseWindow comes from the schedule, not the title.
#include "features/ScheduleFeature.h"
#include "features/CaptureFeature.h"

#include "AppContext.h"
#include "CaptureController.h"
#include "RecordingController.h"
#include "ScheduledCapture.h"
#include "ScreenLockWatcher.h"
#include "WorkflowShortcutActions.h"
#include "shell/ActivationDispatcher.h"

#include <QApplication>
#include <QDBusConnection>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSystemTrayIcon>

namespace xerahs::app {
namespace {

using xerahs::shell::ActivationAction;
using xerahs::shell::ActivationReplyState;
using xerahs::shell::makeActivationRequest;

void adopt(AppContext &ctx, const QString &id, QAction *action) {
  ctx.commands.adopt(CommandSpec{id, action->text(), CommandGroup::Schedule, CommandSurface::Tray | CommandSurface::CaptureMenu}, action);
}

}  // namespace

xerahs::capture::AutoCondition automaticCaptureCondition(AppContext &ctx, bool locked,
    const std::function<qint64(const QString &)> &freeBytes) {
  const auto choice = resolveShellWorkflow(ctx);
  if (!choice.workflow) return xerahs::capture::AutoCondition::WorkflowUnavailable;
  auto folder = choice.workflow->output.directory;
  if (folder.isEmpty()) folder = ctx.capture.saveDirectory();
  xerahs::capture::ConditionSources sources;
  sources.screenLocked = [locked] { return locked; };
  sources.freeBytes = [folder, freeBytes] {
    if (freeBytes) return freeBytes(folder);
    QStorageInfo info(folder);
    return info.isValid() ? info.bytesAvailable() : qint64(-1);
  };
  return xerahs::capture::evaluate(sources);
}

void installSchedule(AppContext &ctx) {
  auto *lock = new xerahs::capture::ScreenLockState(&ctx.app);
  new ScreenLockWatcher(*lock, QDBusConnection::sessionBus(), QDBusConnection::systemBus(), &ctx.app);
  auto *schedule = new ScheduledCapture(ScheduledCapturePorts{
      [&ctx] {
        return ctx.capture.busy() || ctx.recording.busy() || QApplication::activeModalWidget()
            || (ctx.shortcuts && ctx.shortcuts->pending());
      },
      [&ctx](xerahs::capture::CaptureSource source) {
        const auto action = source == xerahs::capture::CaptureSource::Monitor ? ActivationAction::CaptureMonitor
            : source == xerahs::capture::CaptureSource::ActiveWindow ? ActivationAction::CaptureActiveWindow
            : source == xerahs::capture::CaptureSource::Region ? ActivationAction::CaptureRegion
                                                               : ActivationAction::CaptureFullScreen;
        if (!ctx.activation) return QStringLiteral("activation-unavailable");
        const auto local = makeActivationRequest(action);
        const auto admitted = ctx.activation->admit(local);
        if (admitted != ActivationReplyState::Accepted)
          return admitted == ActivationReplyState::Busy ? QStringLiteral("busy") : QStringLiteral("activation-unavailable");
        const auto diagnostic = ctx.activation->dispatch(local);
        return !diagnostic.isEmpty() ? diagnostic : ctx.capture.busy() ? QString() : QStringLiteral("capture-not-started");
      },
      [&ctx](const QString &title, const QString &body, bool ok, bool raiseWindow) {
        if (ctx.notify) ctx.notify(title, body, ok, raiseWindow);
      },
      [&ctx](const QString &tooltip) { ctx.tray.setToolTip(tooltip); },
      [&ctx, lock] { return automaticCaptureCondition(ctx, lock->locked()); },
      [&ctx] { ctx.capture.cancel(); }},
      &ctx.app);
  // CORE-016: the delay and auto-capture choices persist in the settings store.
  schedule->setPreferences(settings::capturePreferencesPort(ctx.settings));
  ctx.schedule = schedule;
  QObject::connect(lock, &xerahs::capture::ScreenLockState::lockedChanged, schedule, &ScheduledCapture::screenLockedChanged);
  adopt(ctx, QStringLiteral("capture-after-delay"), schedule->delayAction());
  adopt(ctx, QStringLiteral("auto-capture"), schedule->autoAction());
  adopt(ctx, QStringLiteral("stop-auto-capture"), schedule->stopAction());
  QObject::connect(&ctx.capture, &CaptureController::busyChanged, schedule, &ScheduledCapture::captureBusyChanged);
  QObject::connect(&ctx.capture, &CaptureController::capturePermissionDenied, schedule, &ScheduledCapture::capturePermissionDenied);
  QObject::connect(&ctx.capture, &CaptureController::notify, schedule, [schedule](const QString &, const QString &, bool ok) {
    schedule->captureFinished(ok);
  });
}

}  // namespace xerahs::app
