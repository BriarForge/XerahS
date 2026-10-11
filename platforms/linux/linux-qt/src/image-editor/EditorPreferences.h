// ESET-001/002/003/009/012: validated preferences shared by the app and adapter.
#pragma once
#include "RecentImages.h"
#include <QJsonObject>
#include <QMap>
#include <optional>

namespace xerahs::editor {
enum class PreferenceFieldState { Missing, Valid, Invalid, Unknown, NewerVersion };
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
}  // namespace xerahs::editor
