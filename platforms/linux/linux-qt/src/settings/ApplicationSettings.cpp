#include "ApplicationSettings.h"
#include "workflows/CaptureWorkflows.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QSaveFile>
namespace xerahs::settings {
QVector<SettingField> settingFields() {
  return {{"saveFolder", Section::General, "profile"}, {"filenamePattern", Section::General, "profile"},
    {"startAtLogin", Section::General, "profile"}, {"showTrayIcon", Section::General, "profile"},
    {"playCaptureSound", Section::Capture, "profile"}, {"showNotification", Section::Capture, "profile"},
    {"hotkeys", Section::Hotkeys, "profile"}, {"systemAudioDevice", Section::Recording, "profile"}, {"microphoneDevice", Section::Recording, "profile"}};
}
QJsonObject defaultSettings(const QString &saveFolder) {
  QJsonObject bindings; const auto defaults = HotkeyBindings::defaults();
  for (auto it = defaults.entries.cbegin(); it != defaults.entries.cend(); ++it) bindings.insert(it.key(), it.value());
  return {{"saveFolder", saveFolder}, {"filenamePattern", workflows::CaptureOutput{}.pattern}, {"startAtLogin", false},
    {"showTrayIcon", true}, {"playCaptureSound", false}, {"showNotification", true}, {"hotkeys", bindings}, {"systemAudioDevice", ""}, {"microphoneDevice", ""}};
}
xerahs::workflows::CaptureWorkflow withApplicationDefaults(xerahs::workflows::CaptureWorkflow workflow,
    const QJsonObject &values, bool savedDefinition) {
  if (workflow.output.directory.isEmpty()) workflow.output.directory = values.value("saveFolder").toString();
  if (!savedDefinition) workflow.output.pattern = values.value("filenamePattern").toString();
  return workflow;
}
HotkeyBindings hotkeys(const QJsonObject &values) {
  HotkeyBindings result; const auto object = values.value("hotkeys").toObject();
  for (auto it = object.begin(); it != object.end(); ++it) if (stableActionId(it.key())) result.entries.insert(it.key(), it.value().toString());
  return result;
}
SettingsRead repairSettings(const QJsonObject &input, const QString &saveFolder) {
  SettingsRead result{input, {}, true}; const auto defaults = defaultSettings(saveFolder);
  for (const auto &field : settingFields()) {
    const auto value = input.value(field.key); bool valid = true;
    if (value.isUndefined()) { result.values.insert(field.key, defaults.value(field.key)); continue; }
    if (field.key == "saveFolder") valid = value.isString() && !value.toString().isEmpty() && workflows::captureDirectoryDiagnostic(value.toString()).isEmpty();
    else if (field.key == "filenamePattern") {
      workflows::CaptureOutput output; output.pattern = value.toString();
      valid = value.isString() && !output.pattern.isEmpty() && workflows::previewCaptureOutput(output).diagnostic.isEmpty();
    } else if (field.key == "hotkeys") valid = value.isObject() && value.toObject().size() <= maximumHotkeyBindings;
    else if (field.section == Section::Recording) valid = value.isString() && value.toString().size() <= 4096 && !value.toString().contains(QChar(0));
    else valid = value.isBool();
    if (!valid) { result.values.insert(field.key, defaults.value(field.key)); result.diagnostics.append(field.key + ": invalid value repaired"); }
  }
  auto bindings = result.values.value("hotkeys").toObject(); HotkeyBindings checked;
  // Validate individually; an invalid field never discards unrelated bindings.
  for (auto it = bindings.begin(); it != bindings.end(); ++it) {
    if (!stableActionId(it.key())) continue; // Compatible future action IDs round-trip, never execute.
    const auto accepted = it.value().isString() ? checked.assign(it.key(), it.value().toString()) : BindingResult{};
    if (!accepted.ok) { result.diagnostics.append("hotkeys." + it.key() + ": clear invalid/conflicting preference; change in desktop settings"); it.value() = ""; checked.entries.insert(it.key(), ""); }
    else it.value() = checked.entries.value(it.key());
  }
  const auto defaultBindings = defaults.value("hotkeys").toObject();
  for (auto it = defaultBindings.begin(); it != defaultBindings.end(); ++it) if (!bindings.contains(it.key())) {
    const auto accepted = checked.assign(it.key(), it.value().toString()); bindings.insert(it.key(), accepted.ok ? it.value() : QJsonValue(""));
  }
  result.values.insert("hotkeys", bindings); return result;
}
QJsonObject resetSection(const QJsonObject &values, Section section, const QString &saveFolder) {
  auto next = values; const auto defaults = defaultSettings(saveFolder);
  for (const auto &field : settingFields()) if (field.section == section) next.insert(field.key, defaults.value(field.key));
  return next;
}
QStringList resetPreview(const QJsonObject &values, Section section, const QString &saveFolder) {
  const auto next = resetSection(values, section, saveFolder); QStringList changes;
  for (const auto &field : settingFields()) if (field.section == section && values.value(field.key) != next.value(field.key))
    changes.append(field.key + " (" + field.scope + "): " + QString::fromUtf8(QJsonDocument(QJsonArray{values.value(field.key), next.value(field.key)}).toJson(QJsonDocument::Compact)));
  return changes;
}
ApplicationSettingsStore::ApplicationSettingsStore(QString path, QString saveFolder) : m_path(std::move(path)), m_saveFolder(std::move(saveFolder)) {
  QFile file(m_path);
  if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1048576) { m_current = {defaultSettings(m_saveFolder), {"settings-read-failed"}, false}; return; }
    m_snapshot = file.readAll(); QJsonParseError error; const auto doc = QJsonDocument::fromJson(m_snapshot, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value("values").isObject()) {
      m_current = {defaultSettings(m_saveFolder), {"settings-invalid-restore-previous-or-repair-file"}, false}; return;
    }
    m_envelope = doc.object();
    if (m_envelope.value("version") != QJsonValue(1)) { m_current = {defaultSettings(m_saveFolder), {"settings-version-unsupported"}, false}; return; }
  }
  m_current = repairSettings(m_envelope.value("values").toObject(), m_saveFolder);
}
QString ApplicationSettingsStore::save(const QJsonObject &values) {
  if (!m_current.writable) return "settings-read-only";
  const auto repaired = repairSettings(values, m_saveFolder);
  if (!repaired.diagnostics.isEmpty()) return repaired.diagnostics.join('\n');
  if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return "settings-directory-failed";
  QLockFile lock(m_path + ".lock"); if (!lock.tryLock(0)) return "settings-busy";
  QFile existing(m_path); QByteArray disk;
  if (existing.exists()) { if (!existing.open(QIODevice::ReadOnly)) return "settings-read-failed"; disk = existing.readAll(); }
  if (disk != m_snapshot) return "settings-changed-reopen-settings";
  auto envelope = m_envelope; envelope.insert("version", 1); envelope.insert("values", repaired.values);
  // Keep the previous version inside the same atomic artifact.
  if (m_envelope.contains("values")) envelope.insert("previous", m_envelope.value("values"));
  const auto bytes = QJsonDocument(envelope).toJson(); if (bytes.size() > 1048576) return "settings-too-large";
  QSaveFile file(m_path); file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) return "settings-write-failed";
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  if (file.write(bytes) != bytes.size() || !file.commit()) return "settings-write-failed";
  m_snapshot = bytes; m_envelope = envelope; m_current = repaired; return {};
}
}
