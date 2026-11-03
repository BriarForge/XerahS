// ESET-001/002/003/009/012: validated preferences shared by the app and adapter.
#pragma once
#include "AnnotationDocument.h"
#include "RecentImages.h"
#include "storage/Compatibility.h"
#include <QJsonObject>
#include <QMap>
#include <QSizeF>
#include <functional>
#include <optional>

namespace xerahs::editor {
enum class PreferenceFieldState { Missing, Valid, Invalid, Unknown, NewerVersion, NonPersistent };
struct EditorPreferences {
  QJsonObject values;
  RecentImages recent;
  QMap<QString, PreferenceFieldState> fields;
  QStringList diagnostics;
  bool readOnly = false;
  compatibility::ImportReport report = {};
};
EditorPreferences loadEditorPreferences(const QJsonObject &stored, RecentPathRules rules = RecentPathRules::Posix);
QJsonObject serializeEditorPreferences(const EditorPreferences &preferences);
struct PreferenceChange {
  std::optional<EditorPreferences> preferences;
  QStringList diagnostics;
};
// Invalid changes are rejected together. Loading repairs individual fields.
PreferenceChange changeEditorPreferences(const EditorPreferences &current, const QJsonObject &changes);
QJsonObject recentPreferenceValues(const RecentImages &recent);
// ESET-006/009: one documented inclusive range per numeric preference, shared by load, change,
// the native controls and any automation path. `integer` fields reject fractions.
struct PreferenceRange { double minimum = 0, maximum = 0; bool integer = false; };
std::optional<PreferenceRange> preferenceNumericRange(const QString &key);
bool preferenceNumberValid(const QString &key, const QJsonValue &value);
// ESET-006/007: saved drawing defaults are separate from annotation state. They carry the shared
// stroke/fill/thickness plus the corner radius, border style, arrow style and shadow fields.
RectangleStyle drawingStyleFromPreferences(const EditorPreferences &preferences);
// The preference fields that persist `style` as creation defaults (the inverse of the above).
QJsonObject drawingPreferenceValues(const RectangleStyle &style);
// ESET-006: the stored LastUsedAnnotationTool, resolved like the reference
// GetInitialAnnotationTool: only creation tools are remembered, anything else is Rectangle.
// Names are the reference EditorTool members (also the native canvas tool names).
QString lastAnnotationToolFromPreferences(const EditorPreferences &preferences);
bool rememberableAnnotationTool(const QString &name);
// Cursor choices this build can draw; every other reference CursorType is kept as inert data.
bool cursorTypeImplemented(const QString &name);
// ESET-008: saved Text/SpeechBalloon/Step defaults. `familyAvailable` reports
// whether a family is a usable native font; a missing family resolves to the
// native default without touching the stored preference.
ContentCreationDefaults contentDefaultsFromPreferences(const EditorPreferences &preferences,
    const std::function<bool(const QString &)> &familyAvailable);
// Implemented Step styles; other saved names stay inert preference data.
bool stepStyleImplemented(const QString &style);
constexpr double maximumEditorWindowExtent = 16777215; // Qt's QWidget extent limit, in logical pixels.
struct EditorWindowPreferences {
  bool remember = true;
  QSizeF normalSize{1280, 720};
  bool maximized = true;
};
EditorWindowPreferences windowPreferencesFromPreferences(const EditorPreferences &preferences);
// Convenience settings never authorize consent or destructive-confirmation bypass.
struct EditorConveniencePreferences {
  bool autoCopy = false;
  bool autoClose = false;
  bool zoomToFit = false;
  bool exitConfirmation = false;
};
EditorConveniencePreferences conveniencePreferencesFromPreferences(const EditorPreferences &preferences);
QStringList conveniencePreferenceFields();
}  // namespace xerahs::editor
