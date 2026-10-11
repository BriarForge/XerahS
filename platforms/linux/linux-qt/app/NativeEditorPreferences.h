// GUI-thread native preference host. An explicit empty path selects in-memory
// fixtures; the application initializes the isolated native path at startup.
#pragma once
#include "image-editor/EditorPreferenceStore.h"

namespace xerahs::app {
QString nativeEditorPreferencePath();
QStringList initializeNativeEditorPreferences(const QString &path = {});
const xerahs::editor::EditorPreferences &nativeEditorPreferences();
bool nativeEditorPreferencePersistence();
xerahs::editor::RecentImages &recentImageHistory();
xerahs::editor::PreferenceWrite applyRecentImagePreferences(
    const xerahs::editor::RecentImages &draft, quint64 expectedRevision);
QString rememberEditorImage(const QString &absolutePath);
QString editorPreferenceDiagnosticMessage(const QString &diagnostic);
}  // namespace xerahs::app
