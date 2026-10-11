#include "EditorPreferences.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace xerahs::editor {
namespace {
const QJsonObject defaults{
  {QStringLiteral("schemaVersion"), 1}, {QStringLiteral("Theme"), QStringLiteral("Dark")},
  {QStringLiteral("BlurStrength"), 30}, {QStringLiteral("Thickness"), 4},
  {QStringLiteral("BorderColorHex"), QStringLiteral("#FFFF0000")},
  {QStringLiteral("FillColorHex"), QStringLiteral("#00000000")},
  {QStringLiteral("RememberRecentImagePaths"), false}, {QStringLiteral("MaxRecentImageFiles"), RecentImages::defaultMaximum},
  {QStringLiteral("RecentImageFiles"), QJsonArray{}}};
const QStringList nonPersistentFields{QStringLiteral("BorderColor"), QStringLiteral("FillColor")};
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
  for (const auto &key : {QStringLiteral("Theme"), QStringLiteral("BlurStrength"), QStringLiteral("Thickness"),
                         QStringLiteral("RememberRecentImagePaths"), QStringLiteral("MaxRecentImageFiles"),
                         QStringLiteral("BorderColorHex"), QStringLiteral("FillColorHex")}) {
    if (!stored.contains(key)) continue;
    const auto value = stored.value(key);
    if (key == u"BorderColorHex" || key == u"FillColorHex") {
      const auto color = preferenceColor(value);
      if (color) result.values.insert(key, formatColor(*color)); else invalid(result, key);
      continue;
    }
    const bool valid = key == u"Theme" ? (value.isString() && QStringList{"Dark", "Light", "System"}.contains(value.toString())) :
        key == u"BlurStrength" ? number(value, 0, 100, false) :
        key == u"Thickness" ? number(value, 1, 10000, true) :
        key == u"RememberRecentImagePaths" ? value.isBool() :
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
  return result;
}
QJsonObject serializeEditorPreferences(const EditorPreferences &preferences) {
  QJsonObject values = preferences.values;
  for (const auto &key : nonPersistentFields) values.remove(key);
  const auto recents = recentPreferenceValues(preferences.recent);
  for (auto it = recents.begin(); it != recents.end(); ++it) values.insert(it.key(), it.value());
  return values;
}
PreferenceChange changeEditorPreferences(const EditorPreferences &current, const QJsonObject &changes) {
  if (current.readOnly) return {{}, {QStringLiteral("settings-read-only")}};
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
}  // namespace xerahs::editor
