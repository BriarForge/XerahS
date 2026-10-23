#pragma once
#include <QMap>
#include <QStringList>
namespace xerahs::settings {
struct HotkeyAction { QString id, label, preferred; };
QVector<HotkeyAction> captureHotkeyActions();
QString normalizeTrigger(const QString &trigger);
bool stableActionId(const QString &id);
struct BindingResult {
  bool ok = false;
  QString diagnostic, conflictingAction;
};
// Empty triggers mean unassigned. These are preferences, never key-grab authority.
class HotkeyBindings {
public:
  QMap<QString, QString> entries;
  static HotkeyBindings defaults();
  BindingResult assign(const QString &id, const QString &trigger);
  BindingResult validate() const;
};
}
