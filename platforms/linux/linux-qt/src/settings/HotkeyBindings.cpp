#include "HotkeyBindings.h"
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
namespace xerahs::settings {
QVector<HotkeyAction> captureHotkeyActions() {
  return {{"capture-region", "Capture region", "Print"}, {"capture-full-screen", "Capture full screen", "Ctrl+Print"},
    {"capture-monitor", "Capture monitor under pointer", "Shift+Print"},
    {"capture-region-choose-actions", "Capture region and choose actions", "Alt+Print"},
    {"start-recording", "Start screen recording", {}}, {"start-gif-recording", "Start GIF recording", {}}, {"stop-recording", "Stop recording", {}}};
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
HotkeyBindings HotkeyBindings::defaults() {
  HotkeyBindings result; for (const auto &a : captureHotkeyActions()) result.entries.insert(a.id, a.preferred); return result;
}
BindingResult HotkeyBindings::assign(const QString &id, const QString &trigger) {
  if (!stableActionId(id)) return {false, "hotkey-action-invalid", {}};
  if (!entries.contains(id) && entries.size() >= 68) return {false, "hotkey-too-many", {}};
  const auto normalized = normalizeTrigger(trigger);
  if (!trigger.isEmpty() && normalized.isEmpty()) return {false, "hotkey-trigger-invalid", {}};
  if ((QStringList{"Ctrl+Alt+Delete", "Alt+F4", "Alt+Tab", "Meta+L"}.contains(normalized) || QRegularExpression("^Ctrl\\+Alt\\+F([1-9]|1[0-2])$").match(normalized).hasMatch())) return {false, "hotkey-reserved-use-desktop-settings", {}};
  for (auto it = entries.cbegin(); it != entries.cend(); ++it)
    if (it.key() != id && !normalized.isEmpty() && normalizeTrigger(it.value()) == normalized)
      return {false, "hotkey-conflict-change-or-clear-binding", it.key()};
  entries.insert(id, normalized); return {true, {}, {}};
}
BindingResult HotkeyBindings::validate() const {
  if (entries.size() > 68) return {false, "hotkey-too-many", {}};
  HotkeyBindings checked;
  for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
    const auto result = checked.assign(it.key(), it.value()); if (!result.ok) return result;
  }
  return {true, {}, {}};
}
}
