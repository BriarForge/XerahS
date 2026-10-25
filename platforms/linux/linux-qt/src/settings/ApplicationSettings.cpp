#include "ApplicationSettings.h"
#include "storage/PrivateJsonFile.h"
#include "workflows/CaptureWorkflows.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
namespace xerahs::settings {
namespace {
storage::PrivateJsonFile settingsFile(const QString &path) {
  return {path, {QStringLiteral("com.xerahs.app/linux-qt/application-settings"), 1, 1048576, 0, 30000, true}};
}
bool integerInRange(const QJsonValue &value, int minimum, int maximum) {
  if (!value.isDouble()) return false;
  const double number = value.toDouble();
  return number == std::floor(number) && number >= minimum && number <= maximum;
}
bool oneOf(const QJsonValue &value, std::initializer_list<const char *> allowed) {
  if (!value.isString()) return false;
  for (const char *candidate : allowed) if (value.toString() == QLatin1String(candidate)) return true;
  return false;
}
bool validCaptureValue(const QString &key, const QJsonValue &value) {
  if (key == "delayedCapture.seconds") return integerInRange(value, 1, 60);
  if (key == "delayedCapture.source") return oneOf(value, {"full-screen", "monitor", "active-window", "region"});
  if (key == "autoCapture.source") return oneOf(value, {"full-screen", "monitor", "active-window"});
  if (key == "autoCapture.intervalMs") return integerInRange(value, 1000, 24 * 60 * 60 * 1000);
  if (key == "autoCapture.boundKind") return oneOf(value, {"count", "time"});
  if (key == "autoCapture.count") return integerInRange(value, 1, 10000);
  if (key == "autoCapture.durationMinutes") return integerInRange(value, 1, 1440);
  if (key == "scrollingCapture.settleMs") return integerInRange(value, 0, 5000);
  if (key == "scrollingCapture.wheelSteps") return integerInRange(value, 1, 50);
  if (key == "scrollingCapture.headerBand" || key == "scrollingCapture.footerBand") return integerInRange(value, 0, 2000);
  if (key == "scrollingCapture.maxHeight") return integerInRange(value, 1000, 100000);
  if (key == "scrollingCapture.maxFrames") return integerInRange(value, 2, 500);
  return value.isBool();
}
}

QVector<SettingField> settingFields() {
  return {{"saveFolder", Section::General, "profile"}, {"filenamePattern", Section::General, "profile"},
    {"startAtLogin", Section::General, "profile"}, {"showTrayIcon", Section::General, "profile"},
    {"regionCapture.quickConfirm", Section::Capture, "profile"}, {"regionCapture.snapping", Section::Capture, "profile"},
    {"playCaptureSound", Section::Capture, "profile"}, {"showNotification", Section::Capture, "profile"},
    {"delayedCapture.seconds", Section::Capture, "profile"}, {"delayedCapture.source", Section::Capture, "profile"},
    {"autoCapture.source", Section::Capture, "profile"}, {"autoCapture.intervalMs", Section::Capture, "profile"},
    {"autoCapture.boundKind", Section::Capture, "profile"}, {"autoCapture.count", Section::Capture, "profile"},
    {"autoCapture.durationMinutes", Section::Capture, "profile"},
    {"scrollingCapture.settleMs", Section::Capture, "profile"}, {"scrollingCapture.wheelSteps", Section::Capture, "profile"},
    {"scrollingCapture.headerBand", Section::Capture, "profile"}, {"scrollingCapture.footerBand", Section::Capture, "profile"},
    {"scrollingCapture.maxHeight", Section::Capture, "profile"}, {"scrollingCapture.maxFrames", Section::Capture, "profile"},
    {"scrollingCapture.positionPointer", Section::Capture, "profile"},
    {"hotkeys", Section::Hotkeys, "profile"}, {"systemAudioDevice", Section::Recording, "profile"}, {"microphoneDevice", Section::Recording, "profile"}};
}
QJsonObject defaultSettings(const QString &saveFolder) {
  QJsonObject bindings; const auto defaults = HotkeyBindings::defaults();
  for (auto it = defaults.entries.cbegin(); it != defaults.entries.cend(); ++it) bindings.insert(it.key(), it.value());
  return {{"saveFolder", saveFolder}, {"filenamePattern", workflows::CaptureOutput{}.pattern}, {"startAtLogin", false},
    {"regionCapture.quickConfirm", true}, {"regionCapture.snapping", true},
    {"showTrayIcon", true}, {"playCaptureSound", false}, {"showNotification", true},
    {"delayedCapture.seconds", 3}, {"delayedCapture.source", "full-screen"}, {"autoCapture.source", "full-screen"},
    {"autoCapture.intervalMs", 5000}, {"autoCapture.boundKind", "count"}, {"autoCapture.count", 10}, {"autoCapture.durationMinutes", 60},
    {"scrollingCapture.settleMs", 300}, {"scrollingCapture.wheelSteps", 5}, {"scrollingCapture.headerBand", 0},
    {"scrollingCapture.footerBand", 0}, {"scrollingCapture.maxHeight", 30000}, {"scrollingCapture.maxFrames", 40},
    {"scrollingCapture.positionPointer", true},
    {"hotkeys", bindings}, {"systemAudioDevice", ""}, {"microphoneDevice", ""}};
}
xerahs::workflows::CaptureWorkflow withApplicationDefaults(xerahs::workflows::CaptureWorkflow workflow,
    const QJsonObject &values, bool savedDefinition) {
  if (workflow.output.directory.isEmpty()) workflow.output.directory = values.value("saveFolder").toString();
  if (!savedDefinition) workflow.output.pattern = values.value("filenamePattern").toString();
  return workflow;
}
xerahs::capture::SessionSettings regionSessionSettings(const QJsonObject &values) {
  // RC defaults are normative. Malformed/missing optional fields must not turn
  // either option off through JSON's implicit conversion.
  const auto enabled = [&](const QString &key) {
    const auto value = values.value(key); return value.isBool() ? value.toBool() : true;
  };
  return {enabled("regionCapture.quickConfirm"), enabled("regionCapture.snapping")};
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
    else if (field.section == Section::Capture) valid = validCaptureValue(field.key, value);
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
  const auto read = settingsFile(m_path).read();
  if (read.status != storage::StoreStatus::Missing) {
    QString diagnostic;
    if (read.status == storage::StoreStatus::Ok || read.status == storage::StoreStatus::Invalid || read.status == storage::StoreStatus::NewerVersion) {
      const auto format = read.document.value("format");
      if (!read.document.value("values").isObject() || (!format.isUndefined() && format != QJsonValue(QStringLiteral("com.xerahs.app/linux-qt/application-settings"))))
        diagnostic = "settings-invalid-restore-previous-or-repair-file";
      else if (read.document.value("version") != QJsonValue(1)) diagnostic = "settings-version-unsupported";
    } else diagnostic = "settings-read-failed";
    if (!diagnostic.isEmpty()) { m_current = {defaultSettings(m_saveFolder), {diagnostic}, false}; return; }
    m_snapshot = read.snapshot;
    m_envelope = read.document;
  }
  m_current = repairSettings(m_envelope.value("values").toObject(), m_saveFolder);
}
QString ApplicationSettingsStore::save(const QJsonObject &values) {
  if (!m_current.writable) return "settings-read-only";
  const auto repaired = repairSettings(values, m_saveFolder);
  if (!repaired.diagnostics.isEmpty()) return repaired.diagnostics.join('\n');
  if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return "settings-directory-failed";
  auto envelope = m_envelope; envelope.insert("version", 1); envelope.insert("values", repaired.values);
  // Keep the previous version inside the same atomic artifact.
  if (m_envelope.contains("values")) envelope.insert("previous", m_envelope.value("values"));
  const auto status = settingsFile(m_path).write(envelope, 1, m_snapshot, &m_snapshot);
  switch (status) {
    case storage::StoreStatus::Ok: break;
    case storage::StoreStatus::Busy: return "settings-busy";
    case storage::StoreStatus::Invalid:
    case storage::StoreStatus::NewerVersion:
    case storage::StoreStatus::Changed: return "settings-changed-reopen-settings";
    case storage::StoreStatus::ReadFailed: return "settings-read-failed";
    case storage::StoreStatus::TooLarge: return "settings-too-large";
    default: return "settings-write-failed";
  }
  m_envelope = envelope; m_current = repaired; return {};
}
}
