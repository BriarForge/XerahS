#pragma once
#include "ConfigurationTransfer.h"
namespace xerahs::settings {
struct ReferenceMigrationPlan {
  ConfigurationPlan configuration;
  QString fingerprint;
  QJsonObject versions;
  bool completed = false;
  bool valid() const { return configuration.valid(); }
};
// Source evidence (read-only reference revision 27b89b57771a4c6fb84191382a64926d8b5efec7):
// XerahS.Common/LinuxXdgDirectories.cs + PathsManager.cs put Linux settings at
// $XDG_CONFIG_HOME/xerahs (fallback ~/.config/xerahs), and history at
// $XDG_STATE_HOME/xerahs/History/History.db (fallback ~/.local/state).
// XerahS.Core/Managers/SettingsManager.cs names ApplicationConfig.json,
// WorkflowsConfig.json and optional machine-specific/custom uploader/workflow
// paths. We inspect the selected default files only; custom paths are reported.
// SettingsBase.cs emits named enums via SafeStringEnumConverter and optional
// inert $type/$values wrappers. Workflows have Id, HotkeyInfo and TaskSettings;
// legacy CustomUploaders have Id/Name/Item; uploader-instances.json has
// Instances with InstanceId/ProviderId/SettingsJson and DefaultInstances.
// Source-shaped synthetic fixtures are in tests/fixtures/reference-settings;
// none is a user profile or contains a real credential. Modern providers and
// history conversions need separate approved adapters and are reported unsupported.
// Constructing this service never looks at reference settings. Only an explicit
// inspectFolder/apply call reads the selected folder; SecretsStore is never opened.
class ReferenceMigration {
public:
  explicit ReferenceMigration(ConfigurationPaths paths) : m_paths(std::move(paths)) {}
  static QString defaultSourceFolder();
  ReferenceMigrationPlan inspectFolder(const QString &folder) const;
  QString apply(const QString &folder, const ReferenceMigrationPlan &plan, bool confirmed,
      const std::function<bool(const QString &, int)> &fault = {}) const;
  bool backupAvailable() const;
  QString backupRevision() const;
  QString restore(bool confirmed, const QString &expectedRevision = {}) const;
  QString dismissBackup(bool confirmed, const QString &expectedRevision = {}) const;
private:
  ConfigurationPaths m_paths;
};
}
