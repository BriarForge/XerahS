#pragma once
#include "image-editor/EditorPreferenceStore.h"
#include "naming/FilenameGenerator.h"
#include <QJsonObject>
#include <QUuid>
#include <QVector>

namespace xerahs::workflows {
struct CaptureOutput {
  // Empty directory inherits the application's standard capture folder.
  QString directory;
  QString pattern = QStringLiteral("%y%mo%dT%h%mi_%ra{10}");
  QJsonObject extra;
};
struct CaptureOutputPreview {
  QString filename, diagnostic;
  std::optional<xerahs::naming::ExpansionError> error;
};
QString captureDirectoryDiagnostic(const QString &directory);
xerahs::naming::ExpansionContext captureOutputExample();
CaptureOutputPreview previewCaptureOutput(const CaptureOutput &output, xerahs::naming::ExpansionContext context = captureOutputExample());
struct CaptureWorkflow {
  QUuid id;
  QString name;
  QString capture = QStringLiteral("region");
  QStringList actions;
  bool chooseActions = false;
  QJsonObject extra;
  CaptureOutput output;
};
struct WorkflowChoice { std::optional<CaptureWorkflow> workflow; QString diagnostic; };
struct CaptureWorkflows {
  QVector<CaptureWorkflow> entries;
  QUuid defaultId;
  QJsonObject extra;
};
struct WorkflowRead { std::optional<CaptureWorkflows> catalog; QString diagnostic; };
CaptureWorkflows defaultCaptureWorkflows();
QJsonObject workflowJson(const CaptureWorkflow &workflow, bool retainUnknown = true);
QByteArray workflowsJson(const CaptureWorkflows &catalog);
WorkflowRead parseCaptureWorkflows(const QByteArray &bytes);
QString workflowAvailability(const CaptureWorkflow &workflow, const QStringList &available);
QStringList workflowActions(const CaptureWorkflow &workflow);
// Report only the known selection fields; unknown extensions may be private.
QJsonObject workflowSnapshot(const CaptureWorkflow &workflow);
class CaptureWorkflowStore {
public:
  explicit CaptureWorkflowStore(xerahs::editor::PreferenceStorage &storage) : m_storage(storage) { reload(); }
  void reload();
  const WorkflowRead &current() const { return m_current; }
  quint64 revision() const { return m_revision; }
  xerahs::editor::PreferenceWrite save(const CaptureWorkflows &draft, quint64 expectedRevision);
  QString unchanged() const;
  WorkflowChoice resolve(const QUuid &requested, const QStringList &available) const;
private:
  xerahs::editor::PreferenceStorage &m_storage;
  xerahs::editor::PreferenceRead m_snapshot;
  WorkflowRead m_current;
  quint64 m_revision = 0;
};
} // namespace xerahs::workflows
