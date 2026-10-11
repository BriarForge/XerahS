// ESET-001/002/003/009/012: validated preferences shared by the app and adapter.
#pragma once
#include "AnnotationDocument.h"
#include "RecentImages.h"
#include <QJsonObject>
#include <QMap>
#include <QSizeF>
#include <optional>

namespace xerahs::editor {
enum class PreferenceFieldState { Missing, Valid, Invalid, Unknown, NewerVersion, NonPersistent };
struct EditorPreferences {
  QJsonObject values;
  RecentImages recent;
  QMap<QString, PreferenceFieldState> fields;
  QStringList diagnostics;
  bool readOnly = false;
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
// ESET-006/007: saved drawing defaults are separate from annotation state.
RectangleStyle drawingStyleFromPreferences(const EditorPreferences &preferences);
constexpr double maximumEditorWindowExtent = 16777215; // Qt's QWidget extent limit, in logical pixels.
struct EditorWindowPreferences {
  bool remember = true;
  QSizeF normalSize{1280, 720};
  bool maximized = true;
};
EditorWindowPreferences windowPreferencesFromPreferences(const EditorPreferences &preferences);
}  // namespace xerahs::editor
