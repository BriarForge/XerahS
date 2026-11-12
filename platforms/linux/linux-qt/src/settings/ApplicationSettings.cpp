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
bool hasControlCharacter(const QString &text) {
  for (const auto c : text) if (c.unicode() < 32 || c.unicode() == 127) return true;
  return false;
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
  return {{"saveFolder", Section::General, "profile", "workflow"}, {"filenamePattern", Section::General, "profile", "workflow"},
    {"startAtLogin", Section::General, "profile"}, {"showTrayIcon", Section::General, "profile"},
    {"regionCapture.quickConfirm", Section::Capture, "profile"}, {"regionCapture.snapping", Section::Capture, "profile"},
    {"playCaptureSound", Section::Capture, "profile"}, {"showNotification", Section::Capture, "profile"}, {"privateNotifications", Section::Capture, "profile"},
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
QStringList settingScopes() { return {"global", "profile", "workflow", "task", "provider", "device", "window"}; }
QStringList precedenceOrder(const QString &key) {
  for (const auto &field : settingFields()) if (field.key == key) {
    QStringList order; if (!field.overriddenBy.isEmpty()) order.append(field.overriddenBy);
    order.append(field.scope); order.append("default"); return order;
  }
  return {};
}
QString fieldStateName(FieldState state) {
  switch (state) {
    case FieldState::Missing: return "missing";
    case FieldState::Valid: return "valid";
    case FieldState::Invalid: return "invalid";
    case FieldState::Unknown: return "unknown";
    case FieldState::Deprecated: return "deprecated";
    case FieldState::NewerVersion: return "newer-version";
  }
  return "invalid";
}
QStringList deprecatedSettingKeys() { return {}; }
QJsonObject defaultSettings(const QString &saveFolder) {
  QJsonObject bindings; const auto defaults = HotkeyBindings::defaults();
  for (auto it = defaults.entries.cbegin(); it != defaults.entries.cend(); ++it) bindings.insert(it.key(), it.value());
  return {{"saveFolder", saveFolder}, {"filenamePattern", workflows::CaptureOutput{}.pattern}, {"startAtLogin", false},
    {"regionCapture.quickConfirm", true}, {"regionCapture.snapping", true},
    {"showTrayIcon", true}, {"playCaptureSound", false}, {"showNotification", true}, {"privateNotifications", false},
    {"delayedCapture.seconds", 3}, {"delayedCapture.source", "full-screen"}, {"autoCapture.source", "full-screen"},
    {"autoCapture.intervalMs", 5000}, {"autoCapture.boundKind", "count"}, {"autoCapture.count", 10}, {"autoCapture.durationMinutes", 60},
    {"scrollingCapture.settleMs", 300}, {"scrollingCapture.wheelSteps", 5}, {"scrollingCapture.headerBand", 0},
    {"scrollingCapture.footerBand", 0}, {"scrollingCapture.maxHeight", 30000}, {"scrollingCapture.maxFrames", 40},
    {"scrollingCapture.positionPointer", true},
    {"hotkeys", bindings}, {"systemAudioDevice", ""}, {"microphoneDevice", ""}};
}
OutputPrecedence describeOutputPrecedence(const xerahs::workflows::CaptureWorkflow &workflow, bool savedDefinition) {
  return {workflow.output.directory.isEmpty() ? "profile" : "workflow", savedDefinition ? "workflow" : "profile"};
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
SettingsRead repairSettings(const QJsonObject &input, const QString &saveFolder, const QStringList &deprecated) {
  SettingsRead result{input, {}, true, {}}; const auto defaults = defaultSettings(saveFolder);
  for (auto it = input.begin(); it != input.end(); ++it) {
    const bool unknown = !defaults.contains(it.key());
    if ((unknown && compatibility::containsTraversal(it.value(), it.key())) ||
        (unknown && compatibility::unsafeUnknown(it.key())) ||
        ((unknown || it.key() == "hotkeys") && compatibility::containsUnsafeUnknown(it.value()))) {
      result.values = defaults; result.writable = false;
      result.diagnostics = {QStringLiteral("settings-unsafe-unknown")};
      result.report.add(compatibility::ImportDisposition::Rejected, input.size());
      result.report.add(compatibility::ImportDisposition::Defaulted, defaults.size());
      return result;
    }
  }
  for (const auto &field : settingFields()) {
    const auto value = input.value(field.key); bool valid = true;
    if (value.isUndefined()) { result.values.insert(field.key, defaults.value(field.key)); result.fields.insert(field.key, FieldState::Missing); continue; }
    if (field.key == "saveFolder") valid = value.isString() && !value.toString().isEmpty() && workflows::captureDirectoryDiagnostic(value.toString()).isEmpty();
    else if (field.key == "filenamePattern") {
      workflows::CaptureOutput output; output.pattern = value.toString();
      valid = value.isString() && !output.pattern.isEmpty() && !hasControlCharacter(output.pattern) && workflows::previewCaptureOutput(output).diagnostic.isEmpty();
    } else if (field.key == "hotkeys") valid = value.isObject() && value.toObject().size() <= maximumHotkeyBindings;
    else if (field.section == Section::Recording) valid = value.isString() && value.toString().size() <= 4096 && !value.toString().contains(QChar(0));
    else if (field.section == Section::Capture) valid = validCaptureValue(field.key, value);
    else valid = value.isBool();
    result.fields.insert(field.key, valid ? FieldState::Valid : FieldState::Invalid);
    if (!valid) { result.values.insert(field.key, defaults.value(field.key)); result.diagnostics.append(field.key + ": invalid value repaired"); }
  }
  for (auto it = input.begin(); it != input.end(); ++it) if (!result.fields.contains(it.key()))
    result.fields.insert(it.key(), deprecated.contains(it.key()) ? FieldState::Deprecated : FieldState::Unknown);
  auto bindings = result.values.value("hotkeys").toObject(); HotkeyBindings checked;
  // Validate individually; an invalid field never discards unrelated bindings.
  for (auto it = bindings.begin(); it != bindings.end(); ++it) {
    const auto state = "hotkeys." + it.key();
    if (!stableActionId(it.key())) { result.fields.insert(state, FieldState::Unknown); continue; } // Compatible future action IDs round-trip, never execute.
    const auto accepted = it.value().isString() ? checked.assign(it.key(), it.value().toString()) : BindingResult{};
    result.fields.insert(state, accepted.ok ? FieldState::Valid : FieldState::Invalid);
    if (!accepted.ok) { result.diagnostics.append("hotkeys." + it.key() + ": clear invalid/conflicting preference; change in desktop settings"); it.value() = ""; checked.entries.insert(it.key(), ""); }
    else it.value() = checked.entries.value(it.key());
  }
  const auto defaultBindings = defaults.value("hotkeys").toObject();
  for (auto it = defaultBindings.begin(); it != defaultBindings.end(); ++it) if (!bindings.contains(it.key())) {
    result.fields.insert("hotkeys." + it.key(), FieldState::Missing);
    const auto accepted = checked.assign(it.key(), it.value().toString()); bindings.insert(it.key(), accepted.ok ? it.value() : QJsonValue(""));
  }
  result.values.insert("hotkeys", bindings);
  using D = compatibility::ImportDisposition;
  qsizetype fieldIndex = -1;
  for (auto it = result.fields.begin(); it != result.fields.end(); ++it) {
    ++fieldIndex;
    const bool hotkey = it.key().startsWith("hotkeys.");
    const QString locator = hotkey && !defaultBindings.contains(it.key().mid(8)) ? QString() : it.key();
    // COMPCAT-003: locate non-retained schema fields by key; Unknown keys come from the payload and are never echoed.
    if (it.value() == FieldState::Invalid) result.report.addRecord(fieldIndex, QStringLiteral("setting"), locator, D::Rejected, QStringLiteral("invalid-value"));
    else if (it.value() == FieldState::NewerVersion) result.report.addRecord(fieldIndex, QStringLiteral("setting"), locator, D::Rejected, QStringLiteral("newer-version"));
    else if (it.value() == FieldState::Deprecated) result.report.addRecord(fieldIndex, QStringLiteral("setting"), locator, D::Quarantined, QStringLiteral("deprecated"));
    switch (it.value()) {
      case FieldState::Missing: result.report.add(D::Defaulted); break;
      case FieldState::Invalid: result.report.add(D::Rejected); result.report.add(D::Defaulted); break;
      case FieldState::Deprecated: result.report.add(D::Quarantined); break;
      case FieldState::Unknown: result.report.add(D::Retained); break;
      case FieldState::NewerVersion: result.report.add(D::Rejected); break;
      case FieldState::Valid: {
        const bool nested = it.key().startsWith("hotkeys.");
        const auto key = nested ? it.key().mid(8) : it.key();
        const auto before = nested ? input.value("hotkeys").toObject().value(key) : input.value(key);
        const auto after = nested ? bindings.value(key) : result.values.value(key);
        result.report.add(before == after ? D::Retained : D::Transformed); break;
      }
    }
  }
  return result;
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
QJsonObject resetAll(const QJsonObject &values, const QString &saveFolder) {
  auto next = values; for (const auto section : {Section::General, Section::Capture, Section::Hotkeys, Section::Recording}) next = resetSection(next, section, saveFolder);
  return next;
}
QStringList resetAllPreview(const QJsonObject &values, const QString &saveFolder) {
  QStringList changes; for (const auto section : {Section::General, Section::Capture, Section::Hotkeys, Section::Recording}) changes += resetPreview(values, section, saveFolder);
  return changes;
}
ApplicationSettingsStore::ApplicationSettingsStore(QString path, QString saveFolder) : m_path(std::move(path)), m_saveFolder(std::move(saveFolder)) {
  const auto read = settingsFile(m_path).read();
  const auto refuse = [&](QString diagnostic, FieldState versionState, qsizetype rejected = 1) {
    m_current = {defaultSettings(m_saveFolder), {std::move(diagnostic)}, false, {}};
    m_current.report.add(compatibility::ImportDisposition::Rejected, rejected);
    m_current.fields.insert(QStringLiteral("version"), versionState);
    if (versionState == FieldState::NewerVersion) for (const auto &field : settingFields()) m_current.fields.insert(field.key, FieldState::NewerVersion);
  };
  if (read.status != storage::StoreStatus::Missing) {
    QString diagnostic;
    if (!read.diagnostic.isEmpty()) diagnostic = "settings-" + read.diagnostic;
    bool valuesDamaged = false;
    if (diagnostic.isEmpty() && (read.status == storage::StoreStatus::Ok || read.status == storage::StoreStatus::Invalid || read.status == storage::StoreStatus::NewerVersion)) {
      const auto format = read.document.value("format");
      const bool formatOk = format.isUndefined() || format == QJsonValue(QStringLiteral("com.xerahs.app/linux-qt/application-settings"));
      if (!formatOk || !read.document.value("values").isObject())
        diagnostic = "settings-invalid-restore-previous-or-repair-file";
      else if (read.document.value("version") != QJsonValue(1)) diagnostic = "settings-version-unsupported";
      // Only a readable version-1 envelope may substitute previous for a non-object values record.
      valuesDamaged = formatOk && read.document.value("version") == QJsonValue(1) && !read.document.value("values").isObject();
    } else if (diagnostic.isEmpty()) diagnostic = read.status == storage::StoreStatus::TooLarge ? "settings-too-large" : "settings-read-failed";
    if (!diagnostic.isEmpty() && !valuesDamaged) {
      const auto version = read.document.value("version");
      // A future envelope is read-only: its fields are not interpreted, never downgraded.
      const auto state = diagnostic == "settings-version-unsupported" && version.isDouble() && version.toDouble() > 1 ? FieldState::NewerVersion : FieldState::Invalid;
      refuse(diagnostic, state);
      return;
    }
    auto unknownEnvelope = read.document;
    for (const auto *key : {"format", "version", "values", "previous"}) unknownEnvelope.remove(QLatin1String(key));
    const auto previous = read.document.value(QStringLiteral("previous"));
    const auto previousRead = previous.isObject() ? repairSettings(previous.toObject(), m_saveFolder) : SettingsRead{};
    if (compatibility::containsUnsafeUnknown(unknownEnvelope) || compatibility::containsTraversal(unknownEnvelope)) {
      m_current = {defaultSettings(m_saveFolder), {"settings-unsafe-unknown"}, false, {}};
      m_current.report.add(compatibility::ImportDisposition::Rejected, unknownEnvelope.size()); return;
    }
    if ((read.document.contains("previous") && !previous.isObject()) || (previous.isObject() && !previousRead.writable)) {
      m_current = {defaultSettings(m_saveFolder), {"settings-unsafe-unknown"}, false, {}};
      m_current.report.add(compatibility::ImportDisposition::Rejected); return;
    }
    if (valuesDamaged) {
      if (!previous.isObject()) { refuse("settings-invalid-restore-previous-or-repair-file", FieldState::Invalid); return; }
      // Preview the previous record in memory. Leave the damaged file untouched until an explicit save.
      m_snapshot = read.snapshot;
      m_envelope = read.document;
      m_current = previousRead;
      m_current.diagnostics.prepend(recoveredPreviousSettingsCode());
      m_current.fields.insert(QStringLiteral("values"), FieldState::Invalid);
      m_current.report.add(compatibility::ImportDisposition::Rejected);
      m_current.report.add(compatibility::ImportDisposition::Transformed);
      auto retainedEnvelope = m_envelope; retainedEnvelope.remove("values"); retainedEnvelope.remove("previous");
      m_current.report.add(compatibility::ImportDisposition::Retained, retainedEnvelope.size());
      return;
    }
    m_snapshot = read.snapshot;
    m_envelope = read.document;
  }
  m_current = repairSettings(m_envelope.value("values").toObject(), m_saveFolder);
  if (m_current.writable && !m_envelope.isEmpty()) {
    // The recovery snapshot stays inert, but is retained in the atomic envelope.
    auto retainedEnvelope = m_envelope; retainedEnvelope.remove("values"); retainedEnvelope.remove("previous");
    m_current.report.add(compatibility::ImportDisposition::Retained,
        retainedEnvelope.size() + m_envelope.value("previous").toObject().size());
  }
}
QString ApplicationSettingsStore::save(const QJsonObject &values, compatibility::ExportReport *report) {
  if (report) *report = {QStringLiteral("com.xerahs.app/linux-qt/application-settings"), 1, {QStringLiteral("profile-local-save-folder")}};
  if (!m_current.writable) return "settings-read-only";
  const auto repaired = repairSettings(values, m_saveFolder);
  if (!repaired.writable || !repaired.diagnostics.isEmpty()) return repaired.diagnostics.join('\n');
  if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return "settings-directory-failed";
  auto envelope = m_envelope; envelope.insert("version", 1); envelope.insert("values", repaired.values);
  // Keep one previous good record in the same atomic artifact. A damaged non-object
  // values payload is not promoted into previous; the record recovery already used stays.
  const auto priorValues = m_envelope.value(QStringLiteral("values"));
  if (priorValues.isObject()) envelope.insert(QStringLiteral("previous"), priorValues);
  else if (m_envelope.value(QStringLiteral("previous")).isObject())
    envelope.insert(QStringLiteral("previous"), m_envelope.value(QStringLiteral("previous")));
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
