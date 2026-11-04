// SETCAT-019: isolated, reviewed profile transfer. No credentials or authority are imported.
// Bundle v1: {format:com.xerahs.app/configuration, version:1, target:com.xerahs.app,
// stores:{application,editor,workflows,destinations}, omissions:[stable codes]}.
// Store schemas retain their explicit versions; unsupported versions are refused,
// with one explicit workflow-v1 -> v2 upgrade. Ordering/UUID/reference/enum/path/
// colour/geometry limits come from the production readers, never Qt defaults.
// Export omits native folders/devices/autostart, editor recents/window placement,
// credential material and opaque destination bodies/extensions. Preview lists
// dispositions without values. Missing local fields retain the receiver's values.
// Publication stages every changed file under the existing store locks and an
// expected-byte fence. A private recovery journal retains exact prior bytes until
// success; startup rolls back interrupted publication before loading GUI stores.
// Backups and migration receipts are private local state, never bundle contents.
#pragma once
#include "storage/Compatibility.h"
#include <QByteArray>
#include <QMap>
#include <functional>

namespace xerahs::settings {
struct ConfigurationPaths {
  QString application, editor, workflows, destinations;
  QString defaultFolder;
  bool configured() const { return !application.isEmpty() && !editor.isEmpty() && !workflows.isEmpty() && !destinations.isEmpty(); }
};
struct ConfigurationPlan {
  QJsonObject candidate;
  QMap<QString, QByteArray> expected;
  compatibility::ImportReport report;
  compatibility::ExportReport exportReport;
  QStringList preservedStores; // internal migration scope; these snapshots are not replaced
  QStringList fields; // schema-owned paths and dispositions only; never input values/unknown keys
  QString code;
  bool valid() const { return code.isEmpty() && !candidate.isEmpty(); }
  QString preview() const;
};
class ConfigurationTransfer {
public:
  explicit ConfigurationTransfer(ConfigurationPaths paths) : m_paths(std::move(paths)) {}
  ConfigurationPlan exportPlan() const;
  QString exportFile(const QString &path, compatibility::ExportReport *report = nullptr) const;
  ConfigurationPlan inspect(const QByteArray &bytes) const;
  // No effect without confirmation. Revalidates the plan and all disk snapshots.
  // fault is a test seam: prepare/publish plus zero-based store index.
  QString apply(const ConfigurationPlan &plan, bool confirmed,
      const std::function<bool(const QString &, int)> &fault = {}) const;
  // Interrupted publication is rolled back before stores are loaded at startup.
  QString recover() const;
  QString restoreSnapshots(const QMap<QString, QByteArray> &snapshots, bool confirmed) const;
  static QString target();
private:
  ConfigurationPaths m_paths;
};
}
