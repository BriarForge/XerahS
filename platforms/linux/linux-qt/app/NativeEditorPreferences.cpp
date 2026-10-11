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
};
PreferenceHost &host() { static PreferenceHost instance; return instance; }
PreferenceWrite publish(EditorPreferences next) {
  auto &state = host();
  if (state.current.readOnly) return {false, QStringLiteral("settings-read-only")};
  if (state.store) {
    const auto written = state.store->save(next);
    if (!written.saved) return written;
  }
  // Keep the live model's monotonic revision. Re-loading normalized JSON here
  // would reset it and allow an older modal draft to overwrite an incoming Open.
  state.current = std::move(next);
  return {true, {}};
}
}
QString nativeEditorPreferencePath() {
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("com.xerahs.app/linux-qt/editor-settings.json"));
}
QStringList initializeNativeEditorPreferences(const QString &path) {
  auto &state = host(); state.store.reset(); state.storage.reset();
  state.current = loadEditorPreferences({});
  if (path.isEmpty()) return {};
  state.storage = std::make_unique<FilePreferenceStorage>(path);
  state.store = std::make_unique<EditorPreferenceStore>(*state.storage);
  state.current = state.store->preferences();
  return state.store->diagnostics();
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
    return QStringLiteral("Recent images changed while this window was open. Reopen it to apply your changes.");
  if (diagnostic == u"settings-locked")
    return QStringLiteral("Another process is saving preferences. Try again after it finishes.");
  if (diagnostic == u"settings-private-storage-required" || diagnostic == u"settings-unsafe-path")
    return QStringLiteral("The preferences location does not provide safe private storage. The file was left untouched.");
  if (diagnostic == u"recovered-previous-settings")
    return QStringLiteral("The current preference record is damaged. A compatible previous record was recovered.");
  if (diagnostic == u"recent-paths-ignored-without-opt-in")
    return QStringLiteral("Stored recent paths were ignored because remembering images was not enabled.");
  if (diagnostic.startsWith(u"invalid-")) {
    const QString field = diagnostic.mid(8);
    const QString label = field == u"BlurStrength" ? QStringLiteral("Blur strength") :
        field == u"Thickness" ? QStringLiteral("Thickness") : field == u"Theme" ? QStringLiteral("Theme") :
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
