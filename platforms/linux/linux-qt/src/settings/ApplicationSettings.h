#pragma once
#include "HotkeyBindings.h"
#include "storage/Compatibility.h"
#include "capture/RegionSession.h"
#include "workflows/CaptureWorkflows.h"
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <functional>
namespace xerahs::settings {
enum class Section { General, Capture, Hotkeys, Recording };
// scope is where the value lives; overriddenBy names a more specific scope whose
// explicit value wins when present (SETCAT-009). Precedence is the same for UI,
// CLI and automation: they all resolve through withApplicationDefaults().
struct SettingField { QString key; Section section; QString scope; QString overriddenBy = {}; };
QVector<SettingField> settingFields();
QStringList settingScopes();                       // the vocabulary a field scope must come from
QStringList precedenceOrder(const QString &key);  // most specific first, ending with "default"
// SETCAT-003: how each stored field was read. Missing/Unknown/Deprecated/NewerVersion are
// informational and never block saving; only Invalid repairs add a diagnostic.
enum class FieldState { Missing, Valid, Invalid, Unknown, Deprecated, NewerVersion };
QString fieldStateName(FieldState state);
// Keys an earlier build wrote that no longer have an effect. They are kept for round-trip.
QStringList deprecatedSettingKeys();
// SETCAT-004: one previous version is retained inside the same atomic file. ShareX keeps
// a single optional .bak plus dated weekly copies; weekly archives belong to export (SETCAT-019).
constexpr int retainedPreviousVersions = 1;
QJsonObject defaultSettings(const QString &saveFolder);
struct SettingsRead { QJsonObject values; QStringList diagnostics; bool writable = true; QMap<QString, FieldState> fields = {}; compatibility::ImportReport report = {}; };
// Diagnostics name fields only; they never repeat a stored value. `deprecated` is injectable for tests.
SettingsRead repairSettings(const QJsonObject &input, const QString &saveFolder, const QStringList &deprecated = deprecatedSettingKeys());
// Saved workflow fields win. Seeded, unsaved definitions inherit profile naming;
// a blank workflow folder always inherits the profile folder.
xerahs::workflows::CaptureWorkflow withApplicationDefaults(xerahs::workflows::CaptureWorkflow workflow,
    const QJsonObject &values, bool savedDefinition);
// Where each output field of a resolved workflow came from: "workflow" or "profile".
struct OutputPrecedence { QString directory, pattern; };
OutputPrecedence describeOutputPrecedence(const xerahs::workflows::CaptureWorkflow &workflow, bool savedDefinition);
xerahs::capture::SessionSettings regionSessionSettings(const QJsonObject &values);
HotkeyBindings hotkeys(const QJsonObject &values);
QJsonObject resetSection(const QJsonObject &values, Section section, const QString &saveFolder);
QStringList resetPreview(const QJsonObject &values, Section section, const QString &saveFolder);
// Full reset: every declared field of every section returns to its default. Unknown and
// deprecated fields survive; nothing outside this settings object is reachable from here.
QJsonObject resetAll(const QJsonObject &values, const QString &saveFolder);
QStringList resetAllPreview(const QJsonObject &values, const QString &saveFolder);
// How delayed and automatic capture read and save their remembered choices.
struct CapturePreferencesPort {
  std::function<QJsonObject()> values;
  std::function<QString(const QJsonObject &)> save;
  explicit operator bool() const { return values && save; }
};
class ApplicationSettingsStore;
CapturePreferencesPort capturePreferencesPort(ApplicationSettingsStore &store);
class ApplicationSettingsStore {
public:
  ApplicationSettingsStore(QString path, QString saveFolder);
  const QString &path() const { return m_path; }
  const SettingsRead &current() const { return m_current; }
  QString save(const QJsonObject &values, compatibility::ExportReport *report = nullptr);
  // The values before the last successful save; empty when there is none.
  QJsonObject previousValues() const { return m_envelope.value("previous").toObject(); }
private:
  QString m_path, m_saveFolder;
  QByteArray m_snapshot;
  QJsonObject m_envelope;
  SettingsRead m_current;
};
inline CapturePreferencesPort capturePreferencesPort(ApplicationSettingsStore &store) {
  return {[&store] { return store.current().values; }, [&store](const QJsonObject &values) { return store.save(values); }};
}
}
