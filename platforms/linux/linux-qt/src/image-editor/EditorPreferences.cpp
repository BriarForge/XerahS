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
  {QStringLiteral("AutoCopyImageToClipboard"), false}, {QStringLiteral("AutoCloseEditorOnTask"), false},
  {QStringLiteral("ZoomToFitOnOpen"), false}, {QStringLiteral("ShowExitConfirmation"), false},
  {QStringLiteral("RememberRecentImagePaths"), false}, {QStringLiteral("MaxRecentImageFiles"), RecentImages::defaultMaximum},
  {QStringLiteral("RecentImageFiles"), QJsonArray{}},
  {QStringLiteral("TextFontFamily"), QString()}, {QStringLiteral("TextFontSize"), 24},
  {QStringLiteral("TextBold"), true}, {QStringLiteral("TextItalic"), false},
  {QStringLiteral("TextHorizontalAlignment"), QStringLiteral("Center")},
  {QStringLiteral("SpeechBalloonFontFamily"), QString()}, {QStringLiteral("SpeechBalloonFontSize"), 24},
  {QStringLiteral("SpeechBalloonTextBold"), true}, {QStringLiteral("SpeechBalloonTextItalic"), false},
  {QStringLiteral("SpeechBalloonTextHorizontalAlignment"), QStringLiteral("Center")},
  {QStringLiteral("StepFontSize"), 0}, {QStringLiteral("StepType"), QStringLiteral("Numeric")},
  // ESET-006 creation defaults. Names follow the reference ImageEditorOptions. Defaults follow the
  // reference except CornerRadius (0, the EA-005 undecorated rectangle; the reference 4 would change
  // the shipped look of new rectangles) and ArrowStyle/BorderStyle, which agree with it.
  {QStringLiteral("LastUsedAnnotationTool"), QStringLiteral("Rectangle")},
  {QStringLiteral("CornerRadius"), 0}, {QStringLiteral("Shadow"), false},
  {QStringLiteral("ShadowColorHex"), QStringLiteral("#80000000")}, {QStringLiteral("ShadowBlurRadius"), 4},
  {QStringLiteral("ShadowOpacity"), 1}, {QStringLiteral("ShadowOffsetX"), 3}, {QStringLiteral("ShadowOffsetY"), 3},
  {QStringLiteral("BorderStyle"), QStringLiteral("Solid")}, {QStringLiteral("ArrowStyle"), QStringLiteral("Classic")},
  {QStringLiteral("CursorType"), QStringLiteral("Default")},
  {QStringLiteral("PixelateStrength"), 20}, {QStringLiteral("MagnifierStrength"), 2}, {QStringLiteral("MagnifierEllipse"), false},
  {QStringLiteral("SpotlightStrength"), 30}, {QStringLiteral("SpotlightBlur"), 0}, {QStringLiteral("SpotlightEllipse"), false},
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
// ESET-006/009 creation fields beyond the text defaults. Numbers have one inclusive range each.
const QMap<QString, PreferenceRange> &numericRanges() {
  static const QMap<QString, PreferenceRange> ranges{
      {QStringLiteral("BlurStrength"), {0, 100, false}}, {QStringLiteral("Thickness"), {1, 10000, true}},
      {QStringLiteral("CornerRadius"), {0, kMaxCornerRadius, true}},
      {QStringLiteral("ShadowBlurRadius"), {0, kMaxShadowBlur, false}}, {QStringLiteral("ShadowOpacity"), {0, 1, false}},
      {QStringLiteral("ShadowOffsetX"), {-kMaxShadowOffset, kMaxShadowOffset, false}},
      {QStringLiteral("ShadowOffsetY"), {-kMaxShadowOffset, kMaxShadowOffset, false}},
      // Reference toolbar maxima (Pixelate 200, Magnify 10, Spotlight 100, Spotlight blur 200); 0 keeps the renderer fallback.
      {QStringLiteral("PixelateStrength"), {0, 200, false}}, {QStringLiteral("MagnifierStrength"), {0, 10, false}},
      {QStringLiteral("SpotlightStrength"), {0, 100, false}}, {QStringLiteral("SpotlightBlur"), {0, 200, false}},
      {QStringLiteral("TextFontSize"), {1, 2000, false}}, {QStringLiteral("SpeechBalloonFontSize"), {1, 2000, false}},
      {QStringLiteral("StepFontSize"), {0, 2000, false}}};
  return ranges;
}
const QStringList &creationFlagFields() {
  static const QStringList keys{"Shadow", "MagnifierEllipse", "SpotlightEllipse"};
  return keys;
}
const QStringList &referenceToolNames() {
  static const QStringList names{"Select", "Rectangle", "Ellipse", "Line", "Arrow", "Freehand", "Text", "SpeechBalloon", "Step", "Image",
      "Emoji", "Cursor", "Highlight", "SmartEraser", "Blur", "Pixelate", "Magnify", "Spotlight", "Crop", "CutOut"};
  return names;
}
const QStringList &creationEnumFields() {
  static const QStringList keys{"LastUsedAnnotationTool", "BorderStyle", "ArrowStyle", "CursorType"};
  return keys;
}
QStringList enumChoices(const QString &key) {
  if (key == u"LastUsedAnnotationTool") return referenceToolNames();
  if (key == u"CursorType") return cursorTypeNames();
  QStringList names;
  if (key == u"BorderStyle") for (const auto s : {BorderStyle::Solid, BorderStyle::Dash, BorderStyle::Dot, BorderStyle::DashDot, BorderStyle::DashDotDot}) names << borderStyleName(s);
  else for (const auto s : {ArrowStyle::Classic, ArrowStyle::Modern, ArrowStyle::Double, ArrowStyle::Basic, ArrowStyle::Line}) names << arrowStyleName(s);
  for (auto &name : names) name[0] = name[0].toUpper();  // preferences store PascalCase names
  return names;
}
bool creationFieldValue(const QString &key, const QJsonValue &value, QJsonValue &normalized);
const QStringList &creationFieldKeys() {
  static QStringList keys = [] {
    QStringList all{"CornerRadius", "ShadowColorHex", "ShadowBlurRadius", "ShadowOpacity", "ShadowOffsetX", "ShadowOffsetY",
        "PixelateStrength", "MagnifierStrength", "SpotlightStrength", "SpotlightBlur"};
    all << creationFlagFields() << creationEnumFields();
    return all;
  }();
  return keys;
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
bool creationFieldValue(const QString &key, const QJsonValue &value, QJsonValue &normalized) {
  normalized = value;
  if (const auto range = numericRanges().find(key); range != numericRanges().end()) return number(value, range->minimum, range->maximum, range->integer);
  if (creationFlagFields().contains(key)) return value.isBool();
  if (key == u"ShadowColorHex") {
    const auto color = preferenceColor(value);
    if (color) normalized = formatColor(*color);
    return color.has_value();
  }
  if (creationEnumFields().contains(key)) return value.isString() && enumChoices(key).contains(value.toString());
  return false;
}
void invalid(EditorPreferences &result, const QString &key) {
  result.fields[key] = PreferenceFieldState::Invalid; result.diagnostics.append(QStringLiteral("invalid-") + key);
}
// Only declared fields carry behavior. Opaque extension records stay byte-for-byte
// JSON values; authority/secret-shaped extensions block the whole candidate.
bool unsafePreferences(const QJsonObject &stored) {
  for (auto it = stored.begin(); it != stored.end(); ++it) {
    if (!defaults.contains(it.key()) && !nonPersistentFields.contains(it.key()) &&
        (compatibility::unsafeUnknown(it.key()) || compatibility::containsUnsafeUnknown(it.value()))) return true;
    if (it.key() == u"ToolbarItems" && it.value().isArray())
      for (const auto &entry : it.value().toArray()) {
        auto extra = entry.toObject();
        for (const auto *key : {"Id", "IsVisible", "BeginGroup", "Hotkey"}) extra.remove(QLatin1String(key));
        if (compatibility::containsUnsafeUnknown(extra)) return true;
      }
  }
  return false;
}
void reportPreferences(EditorPreferences &result, const QJsonObject &stored) {
  using D = compatibility::ImportDisposition;
  result.report = {};
  for (auto it = result.fields.begin(); it != result.fields.end(); ++it) {
    switch (it.value()) {
      case PreferenceFieldState::Missing: result.report.add(D::Defaulted); break;
      case PreferenceFieldState::Invalid: result.report.add(D::Rejected); result.report.add(D::Defaulted); break;
      case PreferenceFieldState::NewerVersion: result.report.add(D::Rejected); break;
      case PreferenceFieldState::NonPersistent: result.report.add(D::Ignored); break;
      case PreferenceFieldState::Unknown: result.report.add(D::Retained); break;
      case PreferenceFieldState::Valid:
        if (it.key() == u"RecentImageFiles" && !result.recent.enabled() && !stored.value(it.key()).toArray().isEmpty())
          result.report.add(D::Ignored);
        else result.report.add(stored.value(it.key()) == result.values.value(it.key()) ? D::Retained : D::Transformed);
        break;
    }
  }
  for (const auto &entry : result.values.value(u"ToolbarItems").toArray())
    if (!knownToolbarAction(entry.toObject().value(u"Id").toString())) result.report.add(D::Quarantined);
}
}  // namespace
QJsonObject recentPreferenceValues(const RecentImages &recent) {
  return {{QStringLiteral("RememberRecentImagePaths"), recent.enabled()},
      {QStringLiteral("MaxRecentImageFiles"), recent.maximum()},
      {QStringLiteral("RecentImageFiles"), QJsonArray::fromStringList(recent.paths())}};
}
EditorPreferences loadEditorPreferences(const QJsonObject &stored, RecentPathRules rules) {
  EditorPreferences result; result.recent = RecentImages(rules); result.values = defaults;
  // RecentImageFiles are absolute filesystem identities the user opened; '..' keeps its kernel
  // meaning through symlinks (EditorWindowTest::recentEntryPreservesTheOpenedSymlinkParentSource)
  // and the path is never read while parsing. Every other embedded path field is checked.
  auto checked = stored; checked.remove(QStringLiteral("RecentImageFiles"));
  const bool traversal = compatibility::containsTraversal(checked);
  if (unsafePreferences(stored) || traversal) {
    result.readOnly = true; result.diagnostics.append(traversal ? QStringLiteral("settings-path-traversal") : QStringLiteral("settings-unsafe-unknown"));
    result.report.add(compatibility::ImportDisposition::Rejected, stored.size());
    result.report.add(compatibility::ImportDisposition::Defaulted, defaults.size());
    return result;
  }
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
    reportPreferences(result, stored); return result;
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
    if (textFieldValid(kind, stored.value(it.key()))) {
      const auto value = stored.value(it.key());
      result.values.insert(it.key(), kind == TextField::Family ? QJsonValue(value.toString().normalized(QString::NormalizationForm_C)) : value);
    }
    else invalid(result, it.key());
  }
  for (const auto &key : creationFieldKeys()) {
    if (!stored.contains(key)) continue;
    QJsonValue normalized;
    if (creationFieldValue(key, stored.value(key), normalized)) result.values.insert(key, normalized); else invalid(result, key);
  }
  for (const auto &key : conveniencePreferenceFields()) {
    if (!stored.contains(key)) continue;
    if (stored.value(key).isBool()) result.values.insert(key, stored.value(key));
    else invalid(result, key);
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
      if (!paths.at(i).isString() || !validated.remember(paths.at(i).toString())) invalid(result, pathsKey);
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
  reportPreferences(result, stored); return result;
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
std::optional<PreferenceRange> preferenceNumericRange(const QString &key) {
  const auto it = numericRanges().find(key);
  if (it == numericRanges().end()) return std::nullopt;
  return *it;
}
bool preferenceNumberValid(const QString &key, const QJsonValue &value) {
  const auto range = preferenceNumericRange(key);
  return range && number(value, range->minimum, range->maximum, range->integer);
}
QJsonObject drawingPreferenceValues(const RectangleStyle &style) {
  const auto pascal = [](QString name) { name[0] = name[0].toUpper(); return name; };
  return {{QStringLiteral("BorderColorHex"), formatColor(style.strokeColor)}, {QStringLiteral("FillColorHex"), formatColor(style.fillColor)},
      {QStringLiteral("Thickness"), style.strokeWidth}, {QStringLiteral("CornerRadius"), style.cornerRadius},
      {QStringLiteral("BorderStyle"), pascal(borderStyleName(style.borderStyle))}, {QStringLiteral("ArrowStyle"), pascal(arrowStyleName(style.arrowStyle))},
      {QStringLiteral("Shadow"), style.shadow.enabled}, {QStringLiteral("ShadowColorHex"), formatColor(style.shadow.color)},
      {QStringLiteral("ShadowBlurRadius"), style.shadow.blurRadius}, {QStringLiteral("ShadowOpacity"), style.shadow.opacity},
      {QStringLiteral("ShadowOffsetX"), style.shadow.offsetX}, {QStringLiteral("ShadowOffsetY"), style.shadow.offsetY}};
}
bool rememberableAnnotationTool(const QString &name) {
  return referenceToolNames().contains(name) && !QStringList{"Select", "Image", "Emoji", "Crop", "CutOut"}.contains(name);
}
QString lastAnnotationToolFromPreferences(const EditorPreferences &preferences) {
  const auto value = preferences.values.value(u"LastUsedAnnotationTool");
  return value.isString() && rememberableAnnotationTool(value.toString()) ? value.toString() : QStringLiteral("Rectangle");
}
bool cursorTypeImplemented(const QString &name) { return name == u"Default" || name == u"Arrow"; }
RectangleStyle drawingStyleFromPreferences(const EditorPreferences &preferences) {
  RectangleStyle style;
  style.strokeColor = preferenceColor(preferences.values.value(u"BorderColorHex")).value_or(style.strokeColor);
  style.fillColor = preferenceColor(preferences.values.value(u"FillColorHex")).value_or(style.fillColor);
  const auto thickness = preferences.values.value(u"Thickness");
  if (number(thickness, 1, 10000, true)) style.strokeWidth = thickness.toDouble();
  // Each decoration field falls back to its undecorated default on its own, as on load.
  const auto &values = preferences.values;
  const auto pick = [&](const char *key) {
    QJsonValue normalized; const QString name = QLatin1String(key);
    return creationFieldValue(name, values.value(name), normalized) ? normalized : defaults.value(name);
  };
  style.cornerRadius = pick("CornerRadius").toDouble();
  const auto enumName = [&](const char *key) { QString name = pick(key).toString(); name[0] = name[0].toLower(); return name; };
  style.borderStyle = parseBorderStyle(enumName("BorderStyle")).value_or(BorderStyle::Solid);
  style.arrowStyle = parseArrowStyle(enumName("ArrowStyle")).value_or(ArrowStyle::Classic);
  style.shadow.enabled = pick("Shadow").toBool();
  style.shadow.color = parseColor(pick("ShadowColorHex").toString()).value_or(style.shadow.color);
  style.shadow.blurRadius = pick("ShadowBlurRadius").toDouble(); style.shadow.opacity = pick("ShadowOpacity").toDouble();
  style.shadow.offsetX = pick("ShadowOffsetX").toDouble(); style.shadow.offsetY = pick("ShadowOffsetY").toDouble();
  return style;
}
namespace {
std::optional<StepStyle> preferenceStepStyle(const QString &style) {
  for (const auto candidate : {StepStyle::Numeric, StepStyle::UppercaseLetter, StepStyle::LowercaseLetter, StepStyle::UppercaseRoman, StepStyle::LowercaseRoman}) {
    QString name = stepStyleName(candidate); name[0] = name[0].toUpper();
    if (style == name) return candidate;  // preferences store the exact PascalCase name
  }
  return std::nullopt;
}
}  // namespace
bool stepStyleImplemented(const QString &style) { return preferenceStepStyle(style).has_value(); }
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
  result.stepStyle = preferenceStepStyle(result.requestedStepStyle).value_or(StepStyle::Numeric);
  const auto pick = [&](const char *key) {
    QJsonValue normalized; const QString name = QLatin1String(key);
    return creationFieldValue(name, values.value(name), normalized) ? normalized : defaults.value(name);
  };
  const auto blur = values.value(u"BlurStrength");
  result.emphasis.blurStrength = preferenceNumberValid(QStringLiteral("BlurStrength"), blur) ? blur.toDouble() : defaults.value(u"BlurStrength").toDouble();
  result.emphasis.pixelateStrength = pick("PixelateStrength").toDouble(); result.emphasis.magnifyStrength = pick("MagnifierStrength").toDouble();
  result.emphasis.magnifyEllipse = pick("MagnifierEllipse").toBool(); result.emphasis.spotlightStrength = pick("SpotlightStrength").toDouble();
  result.emphasis.spotlightBlur = pick("SpotlightBlur").toDouble(); result.emphasis.spotlightEllipse = pick("SpotlightEllipse").toBool();
  result.requestedCursorType = pick("CursorType").toString();
  result.cursorTypeInert = !cursorTypeImplemented(result.requestedCursorType);
  result.cursorType = result.cursorTypeInert ? QStringLiteral("Default") : result.requestedCursorType;
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
QStringList conveniencePreferenceFields() {
  return {QStringLiteral("AutoCopyImageToClipboard"), QStringLiteral("AutoCloseEditorOnTask"),
      QStringLiteral("ZoomToFitOnOpen"), QStringLiteral("ShowExitConfirmation")};
}
EditorConveniencePreferences conveniencePreferencesFromPreferences(const EditorPreferences &preferences) {
  return {preferences.values.value(u"AutoCopyImageToClipboard").toBool(false),
      preferences.values.value(u"AutoCloseEditorOnTask").toBool(false),
      preferences.values.value(u"ZoomToFitOnOpen").toBool(false),
      preferences.values.value(u"ShowExitConfirmation").toBool(false)};
}
}  // namespace xerahs::editor
