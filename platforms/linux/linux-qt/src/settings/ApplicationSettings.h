#pragma once
#include "HotkeyBindings.h"
#include "workflows/CaptureWorkflows.h"
#include <QJsonObject>
namespace xerahs::settings {
enum class Section { General, Capture, Hotkeys };
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
}
