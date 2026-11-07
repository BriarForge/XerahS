#include "HotkeyBindings.h"
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
namespace xerahs::settings {
QVector<HotkeyAction> captureHotkeyActions() {
  return {{"capture-region", "Capture region", "Print"}, {"capture-full-screen", "Capture full screen", "Ctrl+Print"},
    {"capture-monitor", "Capture monitor under pointer", "Shift+Print"},
    {"capture-active-window", "Capture active window", {}},
    {"capture-scrolling", "Capture scrolling", {}},
    {"capture-region-choose-actions", "Capture region and choose actions", "Alt+Print"},
    {"start-region-recording", "Record region", {}}, {"start-recording", "Start screen recording", {}}, {"start-gif-recording", "Start GIF recording", {}}, {"stop-recording", "Stop recording", {}}};
}
bool stableActionId(const QString &id) {
  for (const auto &action : captureHotkeyActions()) if (action.id == id) return true;
  const QUuid uuid(id); return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}
QString normalizeTrigger(const QString &trigger) {
  if (trigger.isEmpty()) return {};
  if (trigger.size() > 128) return {};
  auto parts = trigger.split('+'); const auto key = parts.takeLast(); QSet<QString> modifiers;
  for (const auto &part : parts) {
    if (!QStringList{"Ctrl", "Alt", "Shift", "Meta"}.contains(part) || modifiers.contains(part)) return {};
    modifiers.insert(part);
  }
  static const QRegularExpression keys("^(Print|Space|Tab|Escape|Return|Home|End|Insert|Delete|PageUp|PageDown|Up|Down|Left|Right|[A-Z0-9]|F([1-9]|[12][0-9]|3[0-5]))$");
  if (!keys.match(key).hasMatch()) return {};
  QStringList normalized;
  for (const auto &mod : {"Ctrl", "Alt", "Shift", "Meta"}) if (modifiers.contains(mod)) normalized.append(mod);
  normalized.append(key); return normalized.join('+');
}

namespace {
// Desktop-reserved gestures in the portable spelling (Meta is the Super/Windows key).
// Sources are the stock defaults of the supported Linux desktops:
//   GNOME  = GNOME Shell/Mutter org.gnome.desktop.wm.keybindings and org.gnome.shell.keybindings
//   Plasma = KDE KWin/KGlobalAccel default global shortcuts
//   Sway   = wlroots sway default config (Mod4 bindings are user config, so only compositor-wide ones listed)
//   VT     = Linux kernel/console and X server virtual-terminal switching
struct ReservedGesture { const char *trigger; const char *owner; };
const ReservedGesture reservedGestures[] = {
  {"Alt+Tab", "window switcher (GNOME, Plasma, Sway-compatible shells)"},      // GNOME switch-applications; Plasma Walk Through Windows
  {"Alt+Shift+Tab", "reverse window switcher (GNOME, Plasma)"},                // GNOME switch-applications-backward; Plasma
  {"Alt+F4", "close window (GNOME, Plasma)"},                                  // GNOME close; Plasma Window Close
  {"Alt+F2", "run command dialog (GNOME, Plasma)"},                            // GNOME panel-run-dialog; Plasma KRunner
  {"Ctrl+Alt+Delete", "session logout or power dialog (GNOME, Plasma)"},       // GNOME logout; Plasma Show Logout Prompt
  {"Ctrl+Alt+T", "open terminal (GNOME)"},                                     // GNOME terminal key on common distributions
  {"Ctrl+Alt+L", "lock screen (Plasma)"},                                      // Plasma Lock Session
  {"Meta+L", "lock screen (GNOME, Plasma)"},                                   // GNOME screensaver; Plasma Lock Session
  {"Meta+D", "show desktop (GNOME, Plasma)"},                                  // GNOME show-desktop; Plasma Show Desktop
  {"Meta+Tab", "application switcher (GNOME, Plasma)"},                        // GNOME switch-applications; Plasma Walk Through Windows
  {"Meta+A", "application overview (GNOME)"},                                  // GNOME toggle-application-view
  {"Meta+Up", "maximize or tile window (GNOME, Plasma)"},                      // GNOME maximize; Plasma Quick Tile
  {"Meta+Down", "restore or minimize window (GNOME, Plasma)"},                 // GNOME unmaximize; Plasma Quick Tile
  {"Meta+Left", "tile window left (GNOME, Plasma)"},                           // GNOME toggle-tiled-left; Plasma Quick Tile
  {"Meta+Right", "tile window right (GNOME, Plasma)"},                         // GNOME toggle-tiled-right; Plasma Quick Tile
  {"Ctrl+Alt+Left", "previous workspace (GNOME)"},                             // GNOME switch-to-workspace-left
  {"Ctrl+Alt+Right", "next workspace (GNOME)"},                                // GNOME switch-to-workspace-right
  {"Ctrl+Alt+Up", "previous workspace (GNOME)"},                               // GNOME switch-to-workspace-up
  {"Ctrl+Alt+Down", "next workspace (GNOME)"},                                 // GNOME switch-to-workspace-down
};
}
QString reservedGestureOwner(const QString &normalizedTrigger) {
  for (const auto &g : reservedGestures) if (normalizedTrigger == QLatin1String(g.trigger)) return QString::fromLatin1(g.owner);
  // VT: Ctrl+Alt+F1..F12 switch virtual terminals (Linux console/X server).
  static const QRegularExpression vt("^Ctrl\\+Alt\\+F([1-9]|1[0-2])$");
  if (vt.match(normalizedTrigger).hasMatch()) return QStringLiteral("virtual terminal switch (Linux console)");
  return {};
}
HotkeyBindings HotkeyBindings::defaults() {
  HotkeyBindings result; for (const auto &a : captureHotkeyActions()) result.entries.insert(a.id, a.preferred); return result;
}
BindingResult HotkeyBindings::assign(const QString &id, const QString &trigger) {
  if (!stableActionId(id)) return {false, "hotkey-action-invalid", {}};
  if (!entries.contains(id) && entries.size() >= maximumHotkeyBindings) return {false, "hotkey-too-many", {}};
  const auto normalized = normalizeTrigger(trigger);
  if (!trigger.isEmpty() && normalized.isEmpty()) return {false, "hotkey-trigger-invalid", {}};
  if (!normalized.isEmpty() && !reservedGestureOwner(normalized).isEmpty()) return {false, "hotkey-reserved-use-desktop-settings", {}, reservedGestureOwner(normalized)};
  for (auto it = entries.cbegin(); it != entries.cend(); ++it)
    if (it.key() != id && !normalized.isEmpty() && normalizeTrigger(it.value()) == normalized)
      return {false, "hotkey-conflict-change-or-clear-binding", it.key()};
  entries.insert(id, normalized); return {true, {}, {}};
}
BindingResult HotkeyBindings::reassign(const QString &id, const QString &trigger) {
  // Explicit user-confirmed replacement: the other action is cleared, never silently.
  auto candidate = *this; const auto first = candidate.assign(id, trigger);
  if (first.ok || first.diagnostic != "hotkey-conflict-change-or-clear-binding") return first;
  candidate.entries.insert(first.conflictingAction, {});
  auto second = candidate.assign(id, trigger); if (!second.ok) return second;
  *this = candidate; second.conflictingAction = first.conflictingAction; return second;
}
BindingResult HotkeyBindings::validate() const {
  if (entries.size() > maximumHotkeyBindings) return {false, "hotkey-too-many", {}};
  HotkeyBindings checked;
  for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
    const auto result = checked.assign(it.key(), it.value()); if (!result.ok) return result;
  }
  return {true, {}, {}};
}
}
