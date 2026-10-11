#include "EditorPreferenceStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <unistd.h>

namespace xerahs::editor {
namespace {
const QString format = QStringLiteral("com.xerahs.app/linux-qt/editor-settings");
constexpr auto privateFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto privateDirectory = privateFile | QFileDevice::ExeOwner;
constexpr auto sharedPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
    QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
PreferenceRead failure(const QString &code) { return {PreferenceRead::Kind::Failed, {}, code}; }
bool owned(const QFileInfo &info) { return info.ownerId() == ::geteuid(); }
QJsonObject previousFor(const EditorPreferences &previous, const EditorPreferences &next) {
  QJsonObject values = serializeEditorPreferences(previous);
  RecentImages retained; retained.setPreferences(true, next.recent.maximum());
  // Membership uses the already normalized POSIX paths. Never restore paths
  // intentionally removed, trimmed or cleared in the new current version.
  for (auto it = previous.recent.paths().crbegin(); it != previous.recent.paths().crend(); ++it)
    if (next.recent.paths().contains(*it)) retained.remember(*it);
  retained.setPreferences(previous.recent.enabled() && next.recent.enabled(), previous.recent.maximum());
  const auto recents = recentPreferenceValues(retained);
  for (auto it = recents.begin(); it != recents.end(); ++it) values.insert(it.key(), it.value());
  return values;
}
}  // namespace
PreferenceRead FilePreferenceStorage::read() {
  const QFileInfo info(m_path), directory(info.absolutePath());
  if (!info.isAbsolute() || info.isSymLink() || directory.isSymLink()) return failure(QStringLiteral("settings-unsafe-path"));
  if (!info.exists()) return {};
  if (!info.isFile() || !owned(info) || !owned(directory) || (info.permissions() & sharedPermissions) || (directory.permissions() & sharedPermissions))
    return failure(QStringLiteral("settings-private-storage-required"));
  if (info.size() > maximumPreferenceBytes) return failure(QStringLiteral("settings-too-large"));
  QFile file(m_path); if (!file.open(QIODevice::ReadOnly)) return failure(QStringLiteral("settings-read-failed"));
  QByteArray bytes;
  while (!file.atEnd()) {
    const auto chunk = file.read(65536);
    if (file.error() != QFile::NoError) return failure(QStringLiteral("settings-read-failed"));
    if (bytes.size() + chunk.size() > maximumPreferenceBytes) return failure(QStringLiteral("settings-too-large"));
    bytes.append(chunk);
  }
  return {PreferenceRead::Kind::Readable, bytes, {}};
}
PreferenceWrite FilePreferenceStorage::replace(const PreferenceRead &expected, const QByteArray &bytes) {
  if (bytes.size() > maximumPreferenceBytes) return {false, QStringLiteral("settings-too-large")};
  const QFileInfo info(m_path); const QString directoryPath = info.absolutePath();
  QFileInfo directory(directoryPath);
  if (!info.isAbsolute() || info.isSymLink() || directory.isSymLink() || (directory.exists() && !owned(directory)))
    return {false, QStringLiteral("settings-unsafe-path")};
  if (!QDir().mkpath(directoryPath) || !QFile::setPermissions(directoryPath, privateDirectory))
    return {false, QStringLiteral("settings-write-failed")};
  QLockFile lock(m_path + QStringLiteral(".lock"));
  if (!lock.tryLock(0)) return {false, QStringLiteral("settings-locked")};
  const auto actual = read();
  if (actual.kind == PreferenceRead::Kind::Failed) return {false, actual.diagnostic};
  if (actual.kind != expected.kind || actual.bytes != expected.bytes) return {false, QStringLiteral("settings-changed-externally")};
  QSaveFile file(m_path); file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(privateFile) || file.write(bytes) != bytes.size() || !file.commit())
    return {false, QStringLiteral("settings-write-failed")};
  return {true, {}};
}
void EditorPreferenceStore::reload() {
  m_snapshot = m_storage.read(); m_envelope = {}; m_diagnostics.clear(); m_preferences = loadEditorPreferences({});
  if (m_snapshot.kind == PreferenceRead::Kind::Missing) return;
  if (m_snapshot.kind == PreferenceRead::Kind::Failed) { m_preferences.readOnly = true; m_diagnostics.append(m_snapshot.diagnostic); return; }
  if (m_snapshot.bytes.size() > maximumPreferenceBytes) {
    m_preferences.readOnly = true; m_diagnostics.append(QStringLiteral("settings-too-large")); return;
  }
  QJsonParseError error; const auto document = QJsonDocument::fromJson(m_snapshot.bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    m_preferences.readOnly = true; m_diagnostics.append(QStringLiteral("settings-invalid-json")); return;
  }
  m_envelope = document.object();
  if (m_envelope.value(u"format") != format || m_envelope.value(u"version") != QJsonValue(1)) {
    m_preferences.readOnly = true; m_diagnostics.append(m_envelope.value(u"version").toDouble() > 1 ?
        QStringLiteral("settings-newer-version") : QStringLiteral("settings-invalid-format")); return;
  }
  const auto current = m_envelope.value(u"current");
  if (current.isObject()) m_preferences = loadEditorPreferences(current.toObject());
  else {
    const auto previous = m_envelope.value(u"previous");
    if (!previous.isObject() || (m_preferences = loadEditorPreferences(previous.toObject())).readOnly) {
      m_preferences.readOnly = true; m_diagnostics.append(QStringLiteral("settings-invalid-current")); return;
    }
    m_diagnostics.append(QStringLiteral("recovered-previous-settings"));
  }
  m_diagnostics.append(m_preferences.diagnostics);
}
PreferenceWrite EditorPreferenceStore::save(const EditorPreferences &preferences) {
  if (m_preferences.readOnly || preferences.readOnly) return {false, QStringLiteral("settings-read-only")};
  const auto verified = loadEditorPreferences(serializeEditorPreferences(preferences));
  if (verified.readOnly || !verified.diagnostics.isEmpty()) return {false, QStringLiteral("settings-invalid-change")};
  QJsonObject envelope = m_envelope;
  envelope.insert(QStringLiteral("format"), format); envelope.insert(QStringLiteral("version"), 1);
  envelope.insert(QStringLiteral("current"), serializeEditorPreferences(verified));
  envelope.insert(QStringLiteral("previous"), previousFor(m_preferences, verified));
  const QByteArray bytes = QJsonDocument(envelope).toJson(QJsonDocument::Indented);
  if (bytes.size() > maximumPreferenceBytes) return {false, QStringLiteral("settings-too-large")};
  const auto written = m_storage.replace(m_snapshot, bytes); if (!written.saved) return written;
  m_snapshot = {PreferenceRead::Kind::Readable, bytes, {}}; m_envelope = std::move(envelope); m_preferences = verified;
  return written;
}
}  // namespace xerahs::editor
