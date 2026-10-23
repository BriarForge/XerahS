// CORE-007: preferences commands and tray visibility.
#include "features/PreferencesFeature.h"

#include "AppContext.h"
#include "AppearanceDialog.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "CaptureWorkflows.h"
#include "SettingsDialog.h"
#include "WorkflowShortcutActions.h"
#include "features/WorkflowFeature.h"
#include "settings/ApplicationSettings.h"

#include <QApplication>
#include <QSystemTrayIcon>
#include <QUuid>

#include <algorithm>

namespace xerahs::app {

void applyShellVisibility(AppContext &ctx) {
  ctx.capture.setSaveDirectory(ctx.settings.current().values.value(QStringLiteral("saveFolder")).toString());
  const bool visible = ctx.settings.current().values.value(QStringLiteral("showTrayIcon")).toBool();
  ctx.tray.setVisible(visible);
  ctx.app.setQuitOnLastWindowClosed(!visible || !QSystemTrayIcon::isSystemTrayAvailable());
  if (!visible || !QSystemTrayIcon::isSystemTrayAvailable())
    if (auto *window = qobject_cast<ApplicationWindow *>(ctx.dialogParent ? ctx.dialogParent() : nullptr)) window->activate();
}

void installPreferences(AppContext &ctx) {
  auto *settingsAction = ctx.commands.add(CommandSpec{QStringLiteral("settings"), QStringLiteral("Settings…"),
      CommandGroup::Preferences, CommandSurface::Tray | CommandSurface::PreferencesMenu});
  auto *appearance = ctx.commands.add(CommandSpec{QStringLiteral("editor-appearance"), QStringLiteral("Editor appearance…"),
      CommandGroup::Preferences, CommandSurface::Tray | CommandSurface::Window | CommandSurface::PreferencesMenu});
  QObject::connect(settingsAction, &QAction::triggered, &ctx.app, [&ctx] {
    auto displayPlan = shortcutPlan(ctx);
    if (displayPlan.entries.isEmpty())
      for (const auto &action : xerahs::settings::captureHotkeyActions())
        displayPlan.entries.append({action.id, action.label, xerahs::settings::hotkeys(ctx.settings.current().values).entries.value(action.id)});
    if (ctx.workflows->store().current().catalog)
      for (const auto &workflow : ctx.workflows->store().current().catalog->entries) {
        const auto id = workflow.id.toString(QUuid::WithoutBraces);
        const bool listed = std::any_of(displayPlan.entries.cbegin(), displayPlan.entries.cend(),
            [&](const auto &entry) { return entry.id == id; });
        if (!listed) displayPlan.entries.append({id, workflow.name + QStringLiteral(" (unavailable)"), {}});
      }
    SettingsDialog dialog(ctx.settings, ctx.defaultCaptureFolder, displayPlan, *ctx.shortcuts,
        [&ctx] { applyShellVisibility(ctx); }, qobject_cast<QWidget *>(ctx.dialogParent ? ctx.dialogParent() : nullptr));
    dialog.exec();
  });
  QObject::connect(appearance, &QAction::triggered, &ctx.app, [] { manageEditorAppearance(); });
}

}  // namespace xerahs::app
