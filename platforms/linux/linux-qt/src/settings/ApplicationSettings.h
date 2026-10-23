#pragma once
#include "HotkeyBindings.h"
#include "workflows/CaptureWorkflows.h"
#include <QJsonObject>
#include <functional>
namespace xerahs::settings {
enum class Section { General, Capture, Hotkeys, Recording };
struct SettingField { QString key; Section section; QString scope; };
QVector<SettingField> settingFields();
QJsonObject defaultSettings(const QString &saveFolder);
struct SettingsRead { QJsonObject values; QStringList diagnostics; bool writable = true; };
SettingsRead repairSettings(const QJsonObject &input, const QString &saveFolder);
// Saved workflow fields win. Seeded, unsaved definitions inherit profile naming;
// a blank workflow folder always inherits the profile folder.
xerahs::workflows::CaptureWorkflow withApplicationDefaults(xerahs::workflows::CaptureWorkflow workflow,
    const QJsonObject &values, bool savedDefinition);
HotkeyBindings hotkeys(const QJsonObject &values);
QJsonObject resetSection(const QJsonObject &values, Section section, const QString &saveFolder);
QStringList resetPreview(const QJsonObject &values, Section section, const QString &saveFolder);
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
  const SettingsRead &current() const { return m_current; }
  QString save(const QJsonObject &values);
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
