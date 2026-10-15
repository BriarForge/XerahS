#pragma once
#include "workflows/CaptureWorkflows.h"
#include <QObject>
#include <QWidget>
#include <functional>
namespace xerahs::app {
using WorkflowChoice = xerahs::workflows::WorkflowChoice;
class NativeCaptureWorkflows final : public QObject {
  Q_OBJECT
public:
  explicit NativeCaptureWorkflows(const QString &path, QObject *parent = nullptr) : QObject(parent), m_storage(path), m_store(m_storage) {}
  static QString defaultPath();
  xerahs::workflows::CaptureWorkflowStore &store() { return m_store; }
  void reload() { m_store.reload(); emit changed(); }
  xerahs::editor::PreferenceWrite save(const xerahs::workflows::CaptureWorkflows &draft, quint64 revision);
  WorkflowChoice resolve(const QUuid &id = {}) const;
signals:
  void changed();
private:
  xerahs::editor::FilePreferenceStorage m_storage;
  xerahs::workflows::CaptureWorkflowStore m_store;
};
QString workflowMessage(const QString &diagnostic);
std::optional<xerahs::workflows::CaptureWorkflow> manageCaptureWorkflows(NativeCaptureWorkflows &host,
    QWidget *parent, const std::function<bool()> &canRun);
} // namespace xerahs::app
