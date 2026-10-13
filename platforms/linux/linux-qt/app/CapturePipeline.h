#pragma once
#include "CaptureActions.h"
#include "NativePipelineJournal.h"
#include "actions/PipelineRetry.h"
#include <QObject>
#include <QPointer>
#include <memory>
class QTimer;
class QWidget;
namespace xerahs::app {
class ActionSelectionDialog;
class CaptureSaveAsDialog;
struct CapturePipelineResult {
  xerahs::actions::PipelineResult pipeline;
  std::optional<CaptureFile> file;
  std::vector<CaptureFile> files;
  QImage workingMedia;
  QString diagnostic;
  // Immutable native settings used by this capture, including after save
  // failure. Retry never reads the current preferences or decodes a saved path.
  QString saveDirectory, filenamePattern;
  QJsonObject canonicalResult() const;
  xerahs::actions::PipelineRetryPlan retryPlan() const;
  std::vector<CaptureFile> savedFiles() const;
};
using CapturePipelineReceipt = std::shared_ptr<const CapturePipelineResult>;
struct CapturePipelineState;
// Worker-owned action engine and native file I/O; clipboard and asynchronous
// selection UI stay on this QObject's GUI thread. Destruction cancels without
// joining, and closes any owned selection window.
class CapturePipeline final : public QObject {
  Q_OBJECT
public:
  CapturePipeline(QImage media, QString directory, QString pattern, QStringList selected,
                  QObject *parent = nullptr, CaptureActionPorts ports = {}, PipelineJournalStorage journal = {}, QWidget *selectionParent = nullptr);
  CapturePipeline(CapturePipelineReceipt source, QObject *parent = nullptr, CaptureActionPorts ports = {}, PipelineJournalStorage journal = {}, QWidget *selectionParent = nullptr);
  ~CapturePipeline() override;
  void start();
  void cancel();
signals:
  void activityChanged(const QString &text);
  void finished(const xerahs::app::CapturePipelineReceipt &result);
  void choosingActions();
private:
  void poll();
  std::shared_ptr<CapturePipelineState> m_state;
  QImage m_media;
  QString m_directory, m_pattern;
  QStringList m_selected;
  CaptureActionPorts m_ports;
  PipelineJournalStorage m_journal;
  CapturePipelineReceipt m_retrySource;
  bool m_retryRequested = false;
  QString m_activity;
  QTimer *m_timer = nullptr;
  bool m_started = false;
  bool m_finished = false;
  QPointer<ActionSelectionDialog> m_selectionDialog;
  QPointer<QWidget> m_selectionParent;
  QPointer<CaptureSaveAsDialog> m_saveAsDialog;
};
} // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::CapturePipelineReceipt)
