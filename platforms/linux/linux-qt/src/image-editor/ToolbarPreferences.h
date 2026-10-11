// ESET-010/011/016: portable toolbar data and gesture policy (Qt Core only).
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>
#include <optional>

namespace xerahs::editor {
struct ToolbarAction {
  QString id;
  bool visible = false;
  bool beginGroup = false;
};
const QVector<ToolbarAction> &toolbarActions();
bool knownToolbarAction(const QString &id);
QJsonArray defaultToolbarItems();
struct ToolbarHotkey {
  Qt::Key key = Qt::Key_unknown;
  Qt::KeyboardModifiers modifiers{};
  QString text;
};
// Empty means no additional hotkey. Nonempty gestures contain one key chord.
std::optional<ToolbarHotkey> parseToolbarHotkey(const QString &text);
QString toolbarHotkeyDiagnostic(const ToolbarHotkey &hotkey, const QString &action);
struct ToolbarValidation {
  QJsonArray items;
  QStringList diagnostics;
  bool valid = true;
};
constexpr int maximumToolbarItems = 256;
ToolbarValidation validateToolbarItems(const QJsonValue &value);
// Inactive compatible future actions may move, but cannot be added/modified by
// this edition. Reset preserves them and compatible extension properties.
QString validateToolbarChange(const QJsonArray &before, const QJsonArray &after);
QJsonArray resetToolbarItems(const QJsonArray &before);
struct ToolbarPlacement {
  QString id;
  bool visible = true;
  bool beginGroup = false;
  ToolbarHotkey hotkey;
};
QVector<ToolbarPlacement> effectiveToolbarItems(const QJsonArray &items);
} // namespace xerahs::editor
