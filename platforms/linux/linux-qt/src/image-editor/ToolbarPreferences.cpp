#include "ToolbarPreferences.h"
#include <QRegularExpression>
#include <QSet>

namespace xerahs::editor {
const QVector<ToolbarAction> &toolbarActions() {
  // Native candidate defaults retain the existing editor's visible commands.
  // Shared ID/default metadata and the full action catalogue remain review work.
  static const QVector<ToolbarAction> actions{
    {"save", true}, {"save-as", true}, {"undo", true, true}, {"redo", true},
    {"delete-selection", true}, {"rotate-selection", true},
    {"stroke-colour", true, true}, {"fill-colour", true},
    {"increase-stroke", true}, {"decrease-stroke", true},
    {"zoom-in", true, true}, {"zoom-out", true}, {"reset-zoom", true}, {"fit-image", true},
    {"new-image"}, {"open-image"}, {"crop-image"}, {"crop-tool"}, {"auto-crop"},
    {"resize-image"}, {"resize-canvas"}, {"rotate-image"}, {"rotate-clockwise"},
    {"rotate-counter-clockwise"}, {"rotate-180"}, {"flip-horizontal"}, {"flip-vertical"},
    {"flatten-image"}, {"clear-annotations"}, {"clear-image"}, {"compare-images"},
    {"drawing-defaults"}, {"appearance-preferences"}, {"remember-window-state"},
    {"customize-toolbar"}, {"toggle-toolbars"}, {"close-editor"}, {"copy-rendered-image"}, {"print"},
    {"copy-annotation"}, {"cut-annotation"}, {"paste"}, {"duplicate-selection"},
    {"select-tool"}, {"rectangle-tool"}, {"rectangle-properties"}, {"create-rectangle"}
  };
  return actions;
}
bool knownToolbarAction(const QString &id) {
  for (const auto &action : toolbarActions()) if (action.id == id) return true;
  return false;
}
QJsonArray defaultToolbarItems() {
  QJsonArray result;
  for (const auto &action : toolbarActions())
    result.append(QJsonObject{{"Id", action.id}, {"IsVisible", action.visible}, {"BeginGroup", QJsonValue::Null}, {"Hotkey", ""}});
  return result;
}
std::optional<ToolbarHotkey> parseToolbarHotkey(const QString &text) {
  if (text.isEmpty()) return ToolbarHotkey{};
  if (text.size() > 64 || text != text.trimmed()) return {};
  QStringList parts = text.split(QLatin1Char('+'));
  // Accept Qt's spelling for the plus key, and persist a named key instead.
  if (parts.size() >= 2 && parts.constLast().isEmpty() && parts.at(parts.size() - 2).isEmpty()) {
    parts.removeLast(); parts.last() = QStringLiteral("Plus");
  }
  if (parts.isEmpty() || parts.constLast().isEmpty()) return {};
  ToolbarHotkey result;
  for (int i = 0; i + 1 < parts.size(); ++i) {
    const auto modifier = parts.at(i).toLower();
    Qt::KeyboardModifier flag;
    if (modifier == u"ctrl") flag = Qt::ControlModifier;
    else if (modifier == u"alt") flag = Qt::AltModifier;
    else if (modifier == u"shift") flag = Qt::ShiftModifier;
    else if (modifier == u"meta") flag = Qt::MetaModifier;
    else return {};
    if (result.modifiers.testFlag(flag)) return {};
    result.modifiers |= flag;
  }
  const auto key = parts.constLast().toUpper();
  QString name;
  if (key.size() == 1 && ((key.front() >= u'A' && key.front() <= u'Z') || (key.front() >= u'0' && key.front() <= u'9'))) {
    result.key = Qt::Key(key.front().unicode()); name = key;
  } else if (QRegularExpression(QStringLiteral("^F([1-9]|[12][0-9]|3[0-5])$")).match(key).hasMatch()) {
    result.key = Qt::Key(Qt::Key_F1 + key.mid(1).toInt() - 1); name = key;
  } else {
    static const QMap<QString, std::pair<Qt::Key, QString>> named{
      {"ESCAPE", {Qt::Key_Escape, "Escape"}}, {"ESC", {Qt::Key_Escape, "Escape"}},
      {"ENTER", {Qt::Key_Enter, "Enter"}}, {"RETURN", {Qt::Key_Return, "Return"}},
      {"TAB", {Qt::Key_Tab, "Tab"}}, {"BACKTAB", {Qt::Key_Backtab, "Backtab"}},
      {"SPACE", {Qt::Key_Space, "Space"}}, {"LEFT", {Qt::Key_Left, "Left"}},
      {"RIGHT", {Qt::Key_Right, "Right"}}, {"UP", {Qt::Key_Up, "Up"}}, {"DOWN", {Qt::Key_Down, "Down"}},
      {"HOME", {Qt::Key_Home, "Home"}}, {"END", {Qt::Key_End, "End"}},
      {"PAGEUP", {Qt::Key_PageUp, "PageUp"}}, {"PAGEDOWN", {Qt::Key_PageDown, "PageDown"}},
      {"DELETE", {Qt::Key_Delete, "Delete"}}, {"BACKSPACE", {Qt::Key_Backspace, "Backspace"}},
      {"INSERT", {Qt::Key_Insert, "Insert"}}, {"PLUS", {Qt::Key_Plus, "Plus"}},
      {"EQUAL", {Qt::Key_Equal, "Equal"}}, {"=", {Qt::Key_Equal, "Equal"}},
      {"MINUS", {Qt::Key_Minus, "Minus"}}, {"-", {Qt::Key_Minus, "Minus"}},
      {"COMMA", {Qt::Key_Comma, "Comma"}}, {",", {Qt::Key_Comma, "Comma"}},
      {"PERIOD", {Qt::Key_Period, "Period"}}, {".", {Qt::Key_Period, "Period"}},
      {"SLASH", {Qt::Key_Slash, "Slash"}}, {"/", {Qt::Key_Slash, "Slash"}},
      {"BACKSLASH", {Qt::Key_Backslash, "Backslash"}}, {"SEMICOLON", {Qt::Key_Semicolon, "Semicolon"}},
      {"APOSTROPHE", {Qt::Key_Apostrophe, "Apostrophe"}},
      {"BRACKETLEFT", {Qt::Key_BracketLeft, "BracketLeft"}}, {"[", {Qt::Key_BracketLeft, "BracketLeft"}},
      {"BRACKETRIGHT", {Qt::Key_BracketRight, "BracketRight"}}, {"]", {Qt::Key_BracketRight, "BracketRight"}}
    };
    const auto found = named.constFind(key); if (found == named.cend()) return {};
    result.key = found->first; name = found->second;
  }
  // Plus is a logical key; Qt also matches it on keyboards that require Shift.
  if (result.key == Qt::Key_Plus) result.modifiers &= ~Qt::ShiftModifier;
  for (const auto &entry : {std::pair{Qt::ControlModifier, QStringLiteral("Ctrl+")},
      {Qt::AltModifier, QStringLiteral("Alt+")}, {Qt::ShiftModifier, QStringLiteral("Shift+")},
      {Qt::MetaModifier, QStringLiteral("Meta+")}})
    if (result.modifiers.testFlag(entry.first)) result.text += entry.second;
  result.text += name; return result;
}
QString toolbarHotkeyDiagnostic(const ToolbarHotkey &hotkey, const QString &action) {
  if (hotkey.text.isEmpty()) return {};
  // Protect the catalogue, including commands not yet implemented natively.
  static const QMap<QString, QString> protectedActions{
    {"Delete", "delete-selection"}, {"Shift+Delete", "clear-annotations"},
    {"Ctrl+Z", "undo"}, {"Ctrl+Y", "redo"}, {"Ctrl+Shift+Z", "redo"},
    {"Ctrl+X", "cut-annotation"}, {"Ctrl+Shift+C", "copy-annotation"},
    {"Ctrl+C", "copy-rendered-image"}, {"Ctrl+V", "paste"}, {"Ctrl+D", "duplicate-selection"},
    {"Ctrl+S", "save"}, {"Ctrl+Shift+S", "save-as"}, {"Ctrl+N", "new-image"}, {"Ctrl+O", "open-image"},
    {"Ctrl+P", "pin-to-screen"}, {"Ctrl+Shift+P", "print"}, {"Ctrl+U", "upload"},
    {"Ctrl+H", "toggle-toolbars"}, {"Ctrl+Shift+F", "flatten-image"},
    {"Home", "bring-to-front"}, {"End", "send-to-back"},
    {"PageUp", "bring-forward"}, {"PageDown", "send-backward"},
    {"Ctrl+Plus", "zoom-in"}, {"Ctrl+Equal", "zoom-in"}, {"Ctrl+Minus", "zoom-out"},
    {"Ctrl+0", "reset-zoom"}, {"Ctrl+9", "fit-image"}, {"Ctrl+W", "close-editor"}, {"Ctrl+F4", "close-editor"}
  };
  const auto found = protectedActions.constFind(hotkey.text);
  if (found != protectedActions.cend()) return *found == action ? QString() : QStringLiteral("toolbar-protected-hotkey");
  const auto mods = hotkey.modifiers;
  const auto key = hotkey.key;
  if (mods.testFlag(Qt::MetaModifier) || key == Qt::Key_Escape || key == Qt::Key_Enter || key == Qt::Key_Return ||
      key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Space ||
      key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down ||
      key == Qt::Key_Home || key == Qt::Key_End || key == Qt::Key_PageUp || key == Qt::Key_PageDown ||
      key == Qt::Key_Delete || key == Qt::Key_Backspace || key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight ||
      (key == Qt::Key_R && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) ||
      (mods.testFlag(Qt::AltModifier) && key == Qt::Key_F4) || hotkey.text == u"Ctrl+Q" ||
      (mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::AltModifier) && key >= Qt::Key_F1 && key <= Qt::Key_F35))
    return QStringLiteral("toolbar-reserved-hotkey");
  return {};
}
ToolbarValidation validateToolbarItems(const QJsonValue &value) {
  ToolbarValidation result;
  if (!value.isArray() || value.toArray().size() > maximumToolbarItems) {
    result.valid = false; result.diagnostics.append(QStringLiteral("toolbar-invalid-list")); return result;
  }
  QSet<QString> ids, hotkeys;
  static const QRegularExpression identifier(QStringLiteral("^[a-z][a-z0-9._:-]{0,79}$"));
  for (const auto &entry : value.toArray()) {
    if (!entry.isObject()) { result.valid = false; result.diagnostics.append(QStringLiteral("toolbar-invalid-item")); break; }
    auto item = entry.toObject(); const auto id = item.value(u"Id");
    const auto visible = item.value(u"IsVisible"), group = item.value(u"BeginGroup"), key = item.value(u"Hotkey");
    if (!id.isString() || !identifier.match(id.toString()).hasMatch() ||
        (!visible.isUndefined() && !visible.isBool()) ||
        (!group.isUndefined() && !group.isNull() && !group.isBool()) ||
        (!key.isUndefined() && (!key.isString() || key.toString().size() > 64))) {
      result.valid = false; result.diagnostics.append(QStringLiteral("toolbar-invalid-item")); break;
    }
    if (ids.contains(id.toString())) { result.valid = false; result.diagnostics.append(QStringLiteral("toolbar-duplicate-action")); break; }
    ids.insert(id.toString());
    if (!knownToolbarAction(id.toString())) {
      // Inactive future entries round-trip unchanged; their hotkeys are never
      // parsed, reserved, installed or dispatched by this edition.
      result.diagnostics.append(QStringLiteral("toolbar-unknown-action")); result.items.append(item); continue;
    }
    const auto hotkey = parseToolbarHotkey(key.toString());
    const QString diagnostic = hotkey ? toolbarHotkeyDiagnostic(*hotkey, id.toString()) : QStringLiteral("toolbar-invalid-hotkey");
    if (!diagnostic.isEmpty()) { result.valid = false; result.diagnostics.append(diagnostic); break; }
    if (!hotkey->text.isEmpty() && hotkeys.contains(hotkey->text)) {
      result.valid = false; result.diagnostics.append(QStringLiteral("toolbar-duplicate-hotkey")); break;
    }
    if (!hotkey->text.isEmpty()) hotkeys.insert(hotkey->text);
    if (!key.isUndefined()) item.insert(QStringLiteral("Hotkey"), hotkey->text);
    result.items.append(item);
  }
  if (!result.valid) result.items = {};
  result.diagnostics.removeDuplicates(); return result;
}
QString validateToolbarChange(const QJsonArray &before, const QJsonArray &after) {
  QMap<QString, QJsonObject> retained;
  for (const auto &value : before) {
    const auto item = value.toObject(); const auto id = item.value(u"Id").toString();
    if (!knownToolbarAction(id)) retained.insert(id, item);
  }
  for (const auto &value : after) {
    const auto item = value.toObject(); const auto id = item.value(u"Id").toString();
    if (!knownToolbarAction(id) && (!retained.contains(id) || retained.take(id) != item))
      return QStringLiteral("toolbar-unknown-action-change");
  }
  return retained.isEmpty() ? QString() : QStringLiteral("toolbar-unknown-action-change");
}
QJsonArray resetToolbarItems(const QJsonArray &before) {
  QMap<QString, QJsonObject> retained;
  QJsonArray unknown;
  for (const auto &entry : before) {
    const auto item = entry.toObject(); const auto id = item.value(u"Id").toString();
    if (knownToolbarAction(id)) retained.insert(id, item); else unknown.append(item);
  }
  QJsonArray result;
  for (const auto &entry : defaultToolbarItems()) {
    if (result.size() + unknown.size() >= maximumToolbarItems) break;
    const auto defaults = entry.toObject(); auto item = retained.value(defaults.value(u"Id").toString());
    for (auto it = defaults.begin(); it != defaults.end(); ++it) item.insert(it.key(), it.value());
    result.append(item);
  }
  for (const auto &entry : unknown) result.append(entry);
  return result;
}
QVector<ToolbarPlacement> effectiveToolbarItems(const QJsonArray &items) {
  QVector<ToolbarPlacement> result;
  for (const auto &value : items) {
    const auto item = value.toObject(); const auto id = item.value(u"Id").toString();
    if (!knownToolbarAction(id)) continue;
    bool group = false; for (const auto &action : toolbarActions()) if (action.id == id) group = action.beginGroup;
    const auto storedGroup = item.value(u"BeginGroup");
    const auto hotkey = parseToolbarHotkey(item.value(u"Hotkey").toString());
    if (!hotkey) continue; // All callers normally receive validated preferences.
    result.append({id, item.value(u"IsVisible").toBool(true), storedGroup.isBool() ? storedGroup.toBool() : group, *hotkey});
  }
  return result;
}
} // namespace xerahs::editor
