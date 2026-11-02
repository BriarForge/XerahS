#include "EditorPreferences.h"
#include "ToolbarPreferences.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace xerahs::editor {
namespace {
const QJsonObject defaults{
  {QStringLiteral("schemaVersion"), 1}, {QStringLiteral("Theme"), QStringLiteral("Dark")},
  {QStringLiteral("UseSystemTheme"), true}, {QStringLiteral("UseSystemAccentColor"), true},
  {QStringLiteral("AccentColorHex"), QStringLiteral("#FF3E83F2")},
  {QStringLiteral("BlurStrength"), 30}, {QStringLiteral("Thickness"), 4},
  {QStringLiteral("BorderColorHex"), QStringLiteral("#FFFF0000")},
  {QStringLiteral("FillColorHex"), QStringLiteral("#00000000")},
  {QStringLiteral("RememberWindowState"), true}, {QStringLiteral("WindowWidth"), 1280},
  {QStringLiteral("WindowHeight"), 720}, {QStringLiteral("IsWindowMaximized"), true},
  {QStringLiteral("RememberRecentImagePaths"), false}, {QStringLiteral("MaxRecentImageFiles"), RecentImages::defaultMaximum},
  {QStringLiteral("RecentImageFiles"), QJsonArray{}},
  {QStringLiteral("TextFontFamily"), QString()}, {QStringLiteral("TextFontSize"), 24},
  {QStringLiteral("TextBold"), true}, {QStringLiteral("TextItalic"), false},
  {QStringLiteral("TextHorizontalAlignment"), QStringLiteral("Center")},
  {QStringLiteral("SpeechBalloonFontFamily"), QString()}, {QStringLiteral("SpeechBalloonFontSize"), 24},
  {QStringLiteral("SpeechBalloonTextBold"), true}, {QStringLiteral("SpeechBalloonTextItalic"), false},
  {QStringLiteral("SpeechBalloonTextHorizontalAlignment"), QStringLiteral("Center")},
  {QStringLiteral("StepFontSize"), 0}, {QStringLiteral("StepType"), QStringLiteral("Numeric")},
  {QStringLiteral("ToolbarItems"), defaultToolbarItems()}};
// ESET-008 field kinds. Names follow the reference ImageEditorOptions.
enum class TextField { None, Family, Size, StepSize, Flag, Alignment, StepStyle };
TextField textFieldKind(const QString &key) {
  if (key == u"TextFontFamily" || key == u"SpeechBalloonFontFamily") return TextField::Family;
  if (key == u"TextFontSize" || key == u"SpeechBalloonFontSize") return TextField::Size;
  if (key == u"StepFontSize") return TextField::StepSize;
  if (key == u"TextBold" || key == u"TextItalic" || key == u"SpeechBalloonTextBold" || key == u"SpeechBalloonTextItalic") return TextField::Flag;
  if (key == u"TextHorizontalAlignment" || key == u"SpeechBalloonTextHorizontalAlignment") return TextField::Alignment;
  if (key == u"StepType") return TextField::StepStyle;
  return TextField::None;
}
constexpr int maximumFamilyLength = 256;
bool plainShortString(const QJsonValue &value, int maximum, bool allowEmpty) {
  if (!value.isString()) return false;
  const QString text = value.toString();
  if (text.size() > maximum || (!allowEmpty && text.isEmpty())) return false;
  for (const QChar c : text) if (c.unicode() < 0x20 || c.unicode() == 0x7F) return false;
  return true;
}
bool textFieldValid(TextField kind, const QJsonValue &value) {
  switch (kind) {
    case TextField::Family: return plainShortString(value, maximumFamilyLength, true);
    case TextField::Size: return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 1 && value.toDouble() <= 2000;
    case TextField::StepSize: return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0 && value.toDouble() <= 2000;
    case TextField::Flag: return value.isBool();
    case TextField::Alignment: return value.isString() && QStringList{"Left", "Center", "Right"}.contains(value.toString());
    // Unknown or not-yet-implemented style names stay inert, preserved data.
    case TextField::StepStyle: return plainShortString(value, 64, false);
    case TextField::None: break;
  }
  return false;
}
const QStringList nonPersistentFields{QStringLiteral("BorderColor"), QStringLiteral("FillColor"), QStringLiteral("AccentColor")};
std::optional<Argb> preferenceColor(const QJsonValue &value) {
  if (!value.isString()) return {};
  QString text = value.toString();
  // Compatible opaque RGB input becomes explicit ARGB on the next save.
  if (text.size() == 7 && text.startsWith(QLatin1Char('#'))) text.insert(1, QStringLiteral("FF"));
  if (text.size() != 9) return {};
  return parseColor(text);
}
bool number(const QJsonValue &value, double minimum, double maximum, bool integer) {
  if (!value.isDouble()) return false;
  const double n = value.toDouble();
  return std::isfinite(n) && n >= minimum && n <= maximum && (!integer || std::floor(n) == n);
}
void invalid(EditorPreferences &result, const QString &key) {
  result.fields[key] = PreferenceFieldState::Invalid; result.diagnostics.append(QStringLiteral("invalid-") + key);
}
}  // namespace
QJsonObject recentPreferenceValues(const RecentImages &recent) {
  return {{QStringLiteral("RememberRecentImagePaths"), recent.enabled()},
      {QStringLiteral("MaxRecentImageFiles"), recent.maximum()},
      {QStringLiteral("RecentImageFiles"), QJsonArray::fromStringList(recent.paths())}};
}
EditorPreferences loadEditorPreferences(const QJsonObject &stored, RecentPathRules rules) {
  EditorPreferences result; result.recent = RecentImages(rules); result.values = defaults;
  for (auto it = defaults.begin(); it != defaults.end(); ++it)
    result.fields[it.key()] = stored.contains(it.key()) ? PreferenceFieldState::Valid : PreferenceFieldState::Missing;
  for (auto it = stored.begin(); it != stored.end(); ++it) if (nonPersistentFields.contains(it.key())) {
    result.fields[it.key()] = PreferenceFieldState::NonPersistent;
  } else if (!defaults.contains(it.key())) {
    result.values.insert(it.key(), it.value()); result.fields[it.key()] = PreferenceFieldState::Unknown;
  }
  if (stored.contains(u"schemaVersion") && stored.value(u"schemaVersion") != QJsonValue(1)) {
    const bool newer = number(stored.value(u"schemaVersion"), 2, 2147483647, true);
    result.readOnly = true;
    result.fields[QStringLiteral("schemaVersion")] = newer ? PreferenceFieldState::NewerVersion : PreferenceFieldState::Invalid;
    result.diagnostics.append(newer ? QStringLiteral("settings-newer-version") : QStringLiteral("invalid-schemaVersion"));
    return result;
  }
  if (stored.contains(u"ToolbarItems")) {
    const auto toolbar = validateToolbarItems(stored.value(u"ToolbarItems"));
    if (toolbar.valid) {
      result.values.insert(QStringLiteral("ToolbarItems"), toolbar.items);
      result.diagnostics.append(toolbar.diagnostics);
    } else {
      invalid(result, QStringLiteral("ToolbarItems"));
      // A damaged known placement must not erase compatible inactive future
      // entries when the repaired preferences are explicitly saved later.
      if (stored.value(u"ToolbarItems").isArray() && stored.value(u"ToolbarItems").toArray().size() <= maximumToolbarItems) {
        QJsonArray retained; QStringList ids;
        for (const auto &entry : stored.value(u"ToolbarItems").toArray()) {
          const auto id = entry.toObject().value(u"Id").toString();
          if (!knownToolbarAction(id) && !ids.contains(id) && validateToolbarItems(QJsonArray{entry}).valid) {
            retained.append(entry); ids.append(id);
          }
        }
        result.values.insert(QStringLiteral("ToolbarItems"), resetToolbarItems(retained));
      }
    }
  }
  for (auto it = defaults.begin(); it != defaults.end(); ++it) {
    const auto kind = textFieldKind(it.key());
    if (kind == TextField::None || !stored.contains(it.key())) continue;
    if (textFieldValid(kind, stored.value(it.key()))) result.values.insert(it.key(), stored.value(it.key()));
    else invalid(result, it.key());
  }
  for (const auto &key : {QStringLiteral("Theme"), QStringLiteral("BlurStrength"), QStringLiteral("Thickness"),
                         QStringLiteral("RememberRecentImagePaths"), QStringLiteral("MaxRecentImageFiles"),
                         QStringLiteral("BorderColorHex"), QStringLiteral("FillColorHex"), QStringLiteral("AccentColorHex"),
                         QStringLiteral("UseSystemTheme"), QStringLiteral("UseSystemAccentColor"),
                         QStringLiteral("RememberWindowState"), QStringLiteral("WindowWidth"),
                         QStringLiteral("WindowHeight"), QStringLiteral("IsWindowMaximized")}) {
    if (!stored.contains(key)) continue;
    const auto value = stored.value(key);
    if (key == u"BorderColorHex" || key == u"FillColorHex" || key == u"AccentColorHex") {
      const auto color = preferenceColor(value);
      if (color) result.values.insert(key, formatColor(*color)); else invalid(result, key);
      continue;
    }
    const bool valid = key == u"Theme" ? (value.isString() && QStringList{"Dark", "Light", "System"}.contains(value.toString())) :
        key == u"BlurStrength" ? number(value, 0, 100, false) :
        key == u"Thickness" ? number(value, 1, 10000, true) :
        key == u"RememberRecentImagePaths" || key == u"RememberWindowState" || key == u"IsWindowMaximized" ||
        key == u"UseSystemTheme" || key == u"UseSystemAccentColor" ? value.isBool() :
        key == u"WindowWidth" || key == u"WindowHeight" ? number(value, 1, maximumEditorWindowExtent, false) :
        number(value, RecentImages::minimumMaximum, RecentImages::maximumMaximum, true);
    if (valid) result.values.insert(key, value); else invalid(result, key);
  }
  const bool enabled = result.values.value(u"RememberRecentImagePaths").toBool();
  const int maximum = result.values.value(u"MaxRecentImageFiles").toInt();
  RecentImages validated(rules); validated.setPreferences(true, RecentImages::maximumMaximum);
  const QString pathsKey = QStringLiteral("RecentImageFiles");
  if (stored.contains(pathsKey)) {
    const auto value = stored.value(pathsKey); const auto paths = value.toArray();
    bool valid = value.isArray() && paths.size() <= RecentImages::maximumMaximum;
    if (valid) for (int i = paths.size() - 1; i >= 0; --i) {
      if (!paths.at(i).isString() || !validated.remember(paths.at(i).toString())) { valid = false; break; }
    }
    if (validated.paths().size() > maximum) valid = false;
    if (!valid) { invalid(result, pathsKey); validated.clear(); }
    else if (!enabled && !validated.paths().isEmpty()) result.diagnostics.append(QStringLiteral("recent-paths-ignored-without-opt-in"));
  }
  validated.setPreferences(enabled, maximum); result.recent = std::move(validated);
  const auto recents = recentPreferenceValues(result.recent);
  for (auto it = recents.begin(); it != recents.end(); ++it) result.values.insert(it.key(), it.value());
  if (!result.values.value(u"RememberWindowState").toBool())
    for (const auto *key : {"WindowWidth", "WindowHeight", "IsWindowMaximized"})
      result.values.insert(QLatin1String(key), defaults.value(QLatin1String(key)));
  return result;
}
QJsonObject serializeEditorPreferences(const EditorPreferences &preferences) {
  QJsonObject values = preferences.values;
  for (const auto &key : nonPersistentFields) values.remove(key);
  if (!values.value(u"RememberWindowState").toBool())
    for (const auto *key : {"WindowWidth", "WindowHeight", "IsWindowMaximized"}) values.remove(QLatin1String(key));
  const auto recents = recentPreferenceValues(preferences.recent);
  for (auto it = recents.begin(); it != recents.end(); ++it) values.insert(it.key(), it.value());
  return values;
}
PreferenceChange changeEditorPreferences(const EditorPreferences &current, const QJsonObject &changes) {
  if (current.readOnly) return {{}, {QStringLiteral("settings-read-only")}};
  if (changes.contains(u"ToolbarItems")) {
    const auto toolbar = validateToolbarItems(changes.value(u"ToolbarItems"));
    if (!toolbar.valid) return {{}, {toolbar.diagnostics.constLast()}};
    const auto diagnostic = validateToolbarChange(current.values.value(u"ToolbarItems").toArray(), toolbar.items);
    if (!diagnostic.isEmpty()) return {{}, {diagnostic}};
  }
  QJsonObject merged = serializeEditorPreferences(current);
  for (auto it = changes.begin(); it != changes.end(); ++it) merged.insert(it.key(), it.value());
  auto next = loadEditorPreferences(merged, current.recent.pathRules());
  if (next.readOnly || std::any_of(next.fields.cbegin(), next.fields.cend(), [](PreferenceFieldState state) { return state == PreferenceFieldState::Invalid; }))
    return {{}, next.diagnostics};
  return {std::move(next), {}};
}
RectangleStyle drawingStyleFromPreferences(const EditorPreferences &preferences) {
  RectangleStyle style;
  style.strokeColor = preferenceColor(preferences.values.value(u"BorderColorHex")).value_or(style.strokeColor);
  style.fillColor = preferenceColor(preferences.values.value(u"FillColorHex")).value_or(style.fillColor);
  const auto thickness = preferences.values.value(u"Thickness");
  if (number(thickness, 1, 10000, true)) style.strokeWidth = thickness.toDouble();
  return style;
}
bool stepStyleImplemented(const QString &style) { return style == u"Numeric"; }
ContentCreationDefaults contentDefaultsFromPreferences(const EditorPreferences &preferences,
    const std::function<bool(const QString &)> &familyAvailable) {
  const auto &values = preferences.values;
  const auto text = [&](const char *family, const char *size, const char *bold, const char *italic, const char *alignment) {
    TextCreationDefaults out;
    const auto fallback = [&](const char *key) { return defaults.value(QLatin1String(key)); };
    const auto pick = [&](const char *key, TextField kind) {
      const auto value = values.value(QLatin1String(key));
      return textFieldValid(kind, value) ? value : fallback(key);
    };
    out.requestedFamily = pick(family, TextField::Family).toString();
    out.familyUnavailable = !out.requestedFamily.isEmpty() && !(familyAvailable && familyAvailable(out.requestedFamily));
    out.fontFamily = out.familyUnavailable ? QString() : out.requestedFamily;
    out.fontSize = pick(size, TextField::Size).toDouble();
    out.bold = pick(bold, TextField::Flag).toBool(); out.italic = pick(italic, TextField::Flag).toBool();
    out.alignment = pick(alignment, TextField::Alignment).toString().toLower();
    return out;
  };
  ContentCreationDefaults result;
  result.text = text("TextFontFamily", "TextFontSize", "TextBold", "TextItalic", "TextHorizontalAlignment");
  result.balloon = text("SpeechBalloonFontFamily", "SpeechBalloonFontSize", "SpeechBalloonTextBold",
      "SpeechBalloonTextItalic", "SpeechBalloonTextHorizontalAlignment");
  const auto step = values.value(u"StepFontSize");
  result.stepFontSize = textFieldValid(TextField::StepSize, step) ? step.toDouble() : 0;
  const auto style = values.value(u"StepType");
  result.requestedStepStyle = textFieldValid(TextField::StepStyle, style) ? style.toString() : QStringLiteral("Numeric");
  result.stepStyleInert = !stepStyleImplemented(result.requestedStepStyle);
  return result;
}
EditorWindowPreferences windowPreferencesFromPreferences(const EditorPreferences &preferences) {
  EditorWindowPreferences window;
  window.remember = preferences.values.value(u"RememberWindowState").toBool(true);
  window.maximized = preferences.values.value(u"IsWindowMaximized").toBool(true);
  const auto width = preferences.values.value(u"WindowWidth"), height = preferences.values.value(u"WindowHeight");
  if (number(width, 1, maximumEditorWindowExtent, false)) window.normalSize.setWidth(width.toDouble());
  if (number(height, 1, maximumEditorWindowExtent, false)) window.normalSize.setHeight(height.toDouble());
  return window;
}
}  // namespace xerahs::editor
