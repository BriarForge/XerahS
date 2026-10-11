// Qt Core storage boundary. Current and privacy-pruned previous preferences
// form one atomic artifact; a failed replace never publishes a new state.
#pragma once
#include "EditorPreferences.h"
#include <QByteArray>

namespace xerahs::editor {
constexpr qint64 maximumPreferenceBytes = 1048576;
struct PreferenceRead {
  enum class Kind { Missing, Readable, Failed } kind = Kind::Missing;
  QByteArray bytes;
  QString diagnostic;
};
struct PreferenceWrite { bool saved = false; QString diagnostic; };
class PreferenceStorage {
public:
  virtual ~PreferenceStorage() = default;
  virtual PreferenceRead read() = 0;
  virtual PreferenceWrite replace(const PreferenceRead &expected, const QByteArray &bytes) = 0;
};
class FilePreferenceStorage final : public PreferenceStorage {
public:
  explicit FilePreferenceStorage(QString path) : m_path(std::move(path)) {}
  PreferenceRead read() override;
  PreferenceWrite replace(const PreferenceRead &expected, const QByteArray &bytes) override;
private:
  QString m_path;
};
class EditorPreferenceStore {
public:
  explicit EditorPreferenceStore(PreferenceStorage &storage) : m_storage(storage) { reload(); }
  const EditorPreferences &preferences() const { return m_preferences; }
  const QStringList &diagnostics() const { return m_diagnostics; }
  void reload();
  PreferenceWrite save(const EditorPreferences &preferences);
private:
  PreferenceStorage &m_storage;
  PreferenceRead m_snapshot;
  EditorPreferences m_preferences;
  QJsonObject m_envelope;
  QStringList m_diagnostics;
};
}  // namespace xerahs::editor
