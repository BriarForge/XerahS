#include "NativeEditorPreferences.h"
#include <QDir>
#include <QStandardPaths>
#include <memory>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
struct PreferenceHost {
  EditorPreferences current = loadEditorPreferences({});
  std::unique_ptr<FilePreferenceStorage> storage;
  std::unique_ptr<EditorPreferenceStore> store;
  quint64 revision = 0;
};
PreferenceHost &host() { static PreferenceHost instance; return instance; }
bool drawingChanged(const EditorPreferences &before, const EditorPreferences &after) {
  for (const auto *key : {"BorderColorHex", "FillColorHex", "Thickness"})
    if (before.values.value(QLatin1String(key)) != after.values.value(QLatin1String(key))) return true;
  return false;
}
bool appearanceChanged(const EditorPreferences &before, const EditorPreferences &after) {
  for (const auto *key : {"Theme", "UseSystemTheme", "UseSystemAccentColor", "AccentColorHex"})
    if (before.values.value(QLatin1String(key)) != after.values.value(QLatin1String(key))) return true;
  return false;
}
PreferenceWrite publish(EditorPreferences next) {
  auto &state = host();
  if (state.current.readOnly) return {false, QStringLiteral("settings-read-only")};
  if (state.store) {
    const auto written = state.store->save(next);
    if (!written.saved) return written;
  }
  // Keep the live model's monotonic revision. Re-loading normalized JSON here
  // would reset it and allow an older modal draft to overwrite an incoming Open.
  const bool changed = drawingChanged(state.current, next);
  const bool appearance = appearanceChanged(state.current, next);
  const bool memoryChanged = state.current.values.value(u"RememberWindowState") != next.values.value(u"RememberWindowState");
  state.current = std::move(next); ++state.revision;
  if (changed) emit nativeEditorPreferenceEvents().drawingDefaultsChanged();
  if (appearance) emit nativeEditorPreferenceEvents().appearanceChanged();
  if (memoryChanged) emit nativeEditorPreferenceEvents().windowMemoryChanged(state.current.values.value(u"RememberWindowState").toBool());
  return {true, {}};
}
}
NativeEditorPreferenceEvents &nativeEditorPreferenceEvents() { static NativeEditorPreferenceEvents events; return events; }
quint64 nativeEditorPreferenceRevision() { return host().revision; }
QString nativeEditorPreferencePath() {
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("com.xerahs.app/linux-qt/editor-settings.json"));
}
QStringList initializeNativeEditorPreferences(const QString &path) {
  auto &state = host(); const auto before = state.current;
  state.store.reset(); state.storage.reset();
  state.current = loadEditorPreferences({});
  if (!path.isEmpty()) {
    state.storage = std::make_unique<FilePreferenceStorage>(path);
    state.store = std::make_unique<EditorPreferenceStore>(*state.storage);
    state.current = state.store->preferences();
  }
  ++state.revision;
  if (drawingChanged(before, state.current)) emit nativeEditorPreferenceEvents().drawingDefaultsChanged();
  if (appearanceChanged(before, state.current)) emit nativeEditorPreferenceEvents().appearanceChanged();
  if (before.values.value(u"RememberWindowState") != state.current.values.value(u"RememberWindowState"))
    emit nativeEditorPreferenceEvents().windowMemoryChanged(state.current.values.value(u"RememberWindowState").toBool());
  return state.store ? state.store->diagnostics() : QStringList{};
}
const EditorPreferences &nativeEditorPreferences() { return host().current; }
bool nativeEditorPreferencePersistence() { return bool(host().store); }
RecentImages &recentImageHistory() { return host().current.recent; }
PreferenceWrite applyRecentImagePreferences(const RecentImages &draft, quint64 expectedRevision) {
  const auto &current = host().current;
  if (current.recent.revision() != expectedRevision) return {false, QStringLiteral("settings-changed-in-session")};
  auto changed = changeEditorPreferences(current, recentPreferenceValues(draft));
  if (!changed.preferences) return {false, changed.diagnostics.value(0)};
  changed.preferences->recent = draft;
  return publish(std::move(*changed.preferences));
}
PreferenceWrite applyDrawingDefaults(const QJsonObject &changes, quint64 expectedRevision) {
  auto &state = host();
  if (state.revision != expectedRevision) return {false, QStringLiteral("settings-changed-in-session")};
  for (auto it = changes.begin(); it != changes.end(); ++it)
    if (it.key() != u"BorderColorHex" && it.key() != u"FillColorHex" && it.key() != u"Thickness")
      return {false, QStringLiteral("invalid-drawing-defaults")};
  auto changed = changeEditorPreferences(state.current, changes);
  if (!changed.preferences) return {false, changed.diagnostics.value(0)};
  // A style update cannot reset the recent list's monotonic draft revision.
  changed.preferences->recent = state.current.recent;
  return publish(std::move(*changed.preferences));
}
PreferenceWrite applyWindowPreferences(const QJsonObject &changes, quint64 expectedRevision) {
  auto &state = host();
  if (state.revision != expectedRevision) return {false, QStringLiteral("settings-changed-in-session")};
  for (auto it = changes.begin(); it != changes.end(); ++it)
    if (it.key() != u"RememberWindowState" && it.key() != u"WindowWidth" && it.key() != u"WindowHeight" && it.key() != u"IsWindowMaximized")
      return {false, QStringLiteral("invalid-window-state")};
  auto changed = changeEditorPreferences(state.current, changes);
  if (!changed.preferences) return {false, changed.diagnostics.value(0)};
  changed.preferences->recent = state.current.recent;
  return publish(std::move(*changed.preferences));
}
PreferenceWrite applyAppearancePreferences(const QJsonObject &changes, quint64 expectedRevision) {
  auto &state = host();
  if (state.revision != expectedRevision) return {false, QStringLiteral("settings-changed-in-session")};
  for (auto it = changes.begin(); it != changes.end(); ++it)
    if (it.key() != u"Theme" && it.key() != u"UseSystemTheme" && it.key() != u"UseSystemAccentColor" && it.key() != u"AccentColorHex")
      return {false, QStringLiteral("invalid-appearance-preferences")};
  auto changed = changeEditorPreferences(state.current, changes);
  if (!changed.preferences) return {false, changed.diagnostics.value(0)};
  changed.preferences->recent = state.current.recent;
  return publish(std::move(*changed.preferences));
}
QString rememberEditorWindowState(const QJsonObject &window) {
  const auto &current = host().current;
  if (!current.values.value(u"RememberWindowState").toBool()) return {};
  if (window.isEmpty()) return QStringLiteral("settings-window-display-unavailable");
  if (window.contains(u"RememberWindowState")) return QStringLiteral("invalid-window-state");
  bool changed = false;
  for (auto it = window.begin(); it != window.end(); ++it) if (current.values.value(it.key()) != it.value()) changed = true;
  if (!changed) return {};
  return applyWindowPreferences(window, host().revision).diagnostic;
}
QString rememberEditorImage(const QString &absolutePath) {
  const auto &current = host().current;
  if (!current.recent.enabled()) return {};
  auto next = current;
  if (!next.recent.remember(absolutePath)) return QStringLiteral("invalid-RecentImageFiles");
  if (next.recent.revision() == current.recent.revision()) return {};
  auto changed = changeEditorPreferences(current, recentPreferenceValues(next.recent));
  if (!changed.preferences) return changed.diagnostics.value(0);
  changed.preferences->recent = std::move(next.recent);
  return publish(std::move(*changed.preferences)).diagnostic;
}
QString editorPreferenceDiagnosticMessage(const QString &diagnostic) {
  if (diagnostic == u"invalid-schemaVersion")
    return QStringLiteral("The stored preference version is invalid. Defaults are in use and the file was left untouched.");
  if (diagnostic == u"settings-read-only" || diagnostic == u"settings-newer-version")
    return QStringLiteral("The stored preferences cannot be safely updated by this editor version. The file was left untouched.");
  if (diagnostic == u"settings-changed-externally")
    return QStringLiteral("Stored preferences changed in another process. Reopen the application before changing them.");
  if (diagnostic == u"settings-changed-in-session")
    return QStringLiteral("Preferences changed while this window was open. Reopen it to apply your changes.");
  if (diagnostic == u"settings-locked")
    return QStringLiteral("Another process is saving preferences. Try again after it finishes.");
  if (diagnostic == u"settings-private-storage-required" || diagnostic == u"settings-unsafe-path")
    return QStringLiteral("The preferences location does not provide safe private storage. The file was left untouched.");
  if (diagnostic == u"recovered-previous-settings")
    return QStringLiteral("The current preference record is damaged. A compatible previous record was recovered.");
  if (diagnostic == u"recent-paths-ignored-without-opt-in")
    return QStringLiteral("Stored recent paths were ignored because remembering images was not enabled.");
  if (diagnostic == u"invalid-drawing-defaults") return QStringLiteral("The drawing preference change is not supported.");
  if (diagnostic == u"invalid-window-state") return QStringLiteral("The window preference change is not supported.");
  if (diagnostic == u"invalid-appearance-preferences") return QStringLiteral("The appearance preference change is not supported.");
  if (diagnostic == u"settings-window-display-unavailable") return QStringLiteral("No usable display is available to save the window state.");
  if (diagnostic.startsWith(u"invalid-")) {
    const QString field = diagnostic.mid(8);
    const QString label = field == u"BlurStrength" ? QStringLiteral("Blur strength") :
        field == u"Thickness" ? QStringLiteral("Thickness") : field == u"Theme" ? QStringLiteral("Theme") :
        field == u"BorderColorHex" ? QStringLiteral("Stroke colour") : field == u"FillColorHex" ? QStringLiteral("Fill colour") :
        field == u"AccentColorHex" ? QStringLiteral("Accent colour") : field == u"UseSystemTheme" ? QStringLiteral("Desktop theme preference") :
        field == u"UseSystemAccentColor" ? QStringLiteral("Desktop accent preference") :
        field == u"RememberWindowState" ? QStringLiteral("Remember window state") :
        field == u"WindowWidth" ? QStringLiteral("Window width") : field == u"WindowHeight" ? QStringLiteral("Window height") :
        field == u"IsWindowMaximized" ? QStringLiteral("Maximized window state") :
        field == u"MaxRecentImageFiles" ? QStringLiteral("Maximum recent images") :
        field == u"RememberRecentImagePaths" ? QStringLiteral("Remember recent images") :
        field == u"RecentImageFiles" ? QStringLiteral("Recent image list") : QStringLiteral("Preference version");
    return QStringLiteral("%1 was invalid and is using its default.").arg(label);
  }
  if (diagnostic == u"settings-too-large") return QStringLiteral("The preferences file exceeds the storage limit.");
  if (diagnostic == u"settings-invalid-json" || diagnostic == u"settings-invalid-format" || diagnostic == u"settings-invalid-current")
    return QStringLiteral("Stored preferences are damaged or incompatible. Defaults are in use and the file was left untouched.");
  if (diagnostic == u"settings-read-failed") return QStringLiteral("Stored preferences could not be read. Defaults are in use and the file was left untouched.");
  return QStringLiteral("The preference change could not be saved. Your previous preferences remain active.");
}
}  // namespace xerahs::app
