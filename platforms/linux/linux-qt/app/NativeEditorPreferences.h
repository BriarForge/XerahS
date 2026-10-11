// GUI-thread native preference host. An explicit empty path selects in-memory
// fixtures; the application initializes the isolated native path at startup.
#pragma once
#include "image-editor/EditorPreferenceStore.h"
#include <QObject>

namespace xerahs::app {
class NativeEditorPreferenceEvents final : public QObject {
  Q_OBJECT
signals:
  void drawingDefaultsChanged();
  void windowMemoryChanged(bool enabled);
};
NativeEditorPreferenceEvents &nativeEditorPreferenceEvents();
quint64 nativeEditorPreferenceRevision();
QString nativeEditorPreferencePath();
QStringList initializeNativeEditorPreferences(const QString &path = {});
const xerahs::editor::EditorPreferences &nativeEditorPreferences();
bool nativeEditorPreferencePersistence();
xerahs::editor::RecentImages &recentImageHistory();
xerahs::editor::PreferenceWrite applyRecentImagePreferences(
    const xerahs::editor::RecentImages &draft, quint64 expectedRevision);
xerahs::editor::PreferenceWrite applyDrawingDefaults(const QJsonObject &changes, quint64 expectedRevision);
xerahs::editor::PreferenceWrite applyWindowPreferences(const QJsonObject &changes, quint64 expectedRevision);
QString rememberEditorWindowState(const QJsonObject &state);
QString rememberEditorImage(const QString &absolutePath);
QString editorPreferenceDiagnosticMessage(const QString &diagnostic);
}  // namespace xerahs::app
