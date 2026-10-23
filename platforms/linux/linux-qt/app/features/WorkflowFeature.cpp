// CORE-007: workflow commands. Shortcut actions stay owned by WorkflowShortcutActions.
#include "features/WorkflowFeature.h"

#include "AppContext.h"
#include "ApplicationWindow.h"
#include "features/CaptureFeature.h"
#include "CaptureActions.h"
#include "CaptureController.h"
#include "RecordingController.h"
#include "CaptureWorkflows.h"
#include "WorkflowShortcutActions.h"
#include "settings/ApplicationSettings.h"

#include <QApplication>
#include <QMap>
#include <QScopedValueRollback>
#include <QUuid>

#include <algorithm>

namespace xerahs::app {
namespace {

ApplicationWindow *commandWindow(AppContext &ctx) {
  return qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr);
}

void adoptShortcut(AppContext &ctx, const QString &id, QAction *action) {
  ctx.commands.adopt(CommandSpec{id, action->text(), CommandGroup::Workflows,
      CommandSurface::Tray | CommandSurface::Window | CommandSurface::CaptureMenu}, action);
}

}  // namespace

xerahs::workflows::WorkflowShortcutPlan shortcutPlan(AppContext &ctx) {
  const auto diagnostic = ctx.workflows->store().unchanged();
  if (!diagnostic.isEmpty()) return xerahs::workflows::WorkflowShortcutPlan{{}, 0, diagnostic};
  return xerahs::workflows::captureShortcutPlan(*ctx.workflows->store().current().catalog,
      CaptureActions::selectableActions(), xerahs::settings::hotkeys(ctx.settings.current().values));
}

void installWorkflows(AppContext &ctx) {
  ctx.shortcuts = new WorkflowShortcutActions([&ctx] { return shortcutPlan(ctx); },
      [&ctx] { return !ctx.capture.busy() && !QApplication::activeModalWidget(); },
      QDBusConnection::sessionBus(), {}, &ctx.app);
  auto *configure = ctx.commands.add(CommandSpec{QStringLiteral("capture-workflows"), QStringLiteral("Capture workflows…"),
      CommandGroup::Workflows, CommandSurface::Tray | CommandSurface::Window | CommandSurface::CaptureMenu});
  adoptShortcut(ctx, QStringLiteral("enable-workflow-shortcuts"), ctx.shortcuts->enableAction());
  adoptShortcut(ctx, QStringLiteral("disable-workflow-shortcuts"), ctx.shortcuts->disableAction());

  QObject::connect(configure, &QAction::triggered, &ctx.app, [&ctx] {
    const auto selected = manageCaptureWorkflows(*ctx.workflows, commandWindow(ctx), [&ctx] { return !ctx.capture.busy(); });
    if (selected) ctx.invoke(xerahs::shell::ActivationAction::RunWorkflow, {}, false, selected->id.toString(QUuid::WithoutBraces), QStringLiteral("native-shell"));
  });
  QObject::connect(ctx.shortcuts, &WorkflowShortcutActions::notify, &ctx.app, [&ctx](const QString &body, bool success) {
    if (auto *window = commandWindow(ctx)) window->showResult(QStringLiteral("Workflow shortcuts"), body, success);
  });
  QObject::connect(ctx.workflows, &NativeCaptureWorkflows::changed, ctx.shortcuts, &WorkflowShortcutActions::invalidate);

  QMap<QString, QAction *> builtins;
  for (const char *id : {"capture-region", "capture-full-screen", "capture-monitor", "capture-active-window",
       "capture-region-choose-actions", "start-region-recording", "start-recording", "start-gif-recording", "stop-recording"})
    builtins.insert(QString::fromLatin1(id), ctx.commands.action(QString::fromLatin1(id)));
  QObject::connect(ctx.shortcuts, &WorkflowShortcutActions::workflowRequested, &ctx.app, [&ctx, builtins](const QString &id) {
    const QScopedValueRollback<QString> origin(ctx.invocationOrigin, QStringLiteral("global-shortcut"));
    if (!routeCaptureShortcut(id, builtins,
            [&ctx, id] { return !ctx.capture.busy() && (!ctx.recording.busy() || id == QLatin1String("stop-recording")) && !QApplication::activeModalWidget(); },
            [&ctx] {
              if (auto *window = commandWindow(ctx)) {
                window->showResult(QStringLiteral("Capture shortcut"), activationFailureMessage(QStringLiteral("activation-busy")), false);
                window->activate();
              }
            }))
      ctx.invoke(xerahs::shell::ActivationAction::RunWorkflow, {}, false, id, QStringLiteral("global-shortcut"));
  });
}

}  // namespace xerahs::app
