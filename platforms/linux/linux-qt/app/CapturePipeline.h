#pragma once
#include "CaptureActions.h"
#include <QObject>
#include <memory>
class QTimer;
namespace xerahs::app {
struct CapturePipelineResult {
  xerahs::actions::PipelineResult pipeline;
  std::optional<CaptureFile> file;
  QImage workingMedia;
  QString diagnostic;
  QJsonObject canonicalResult() const;
};
using CapturePipelineReceipt = std::shared_ptr<const CapturePipelineResult>;
struct CapturePipelineState;
// Worker-owned action engine and native file I/O; clipboard is dispatched only
// by this QObject on its Qt GUI thread. Destruction cancels without joining.
class CapturePipeline final : public QObject {
  Q_OBJECT
public:
  CapturePipeline(QImage media, QString directory, QString pattern, QStringList selected,
                  QObject *parent = nullptr, CaptureActionPorts ports = {});
  ~CapturePipeline() override;
  void start();
  void cancel();
signals:
  void activityChanged(const QString &text);
  void finished(const xerahs::app::CapturePipelineReceipt &result);
private:
  void poll();
  std::shared_ptr<CapturePipelineState> m_state;
  QImage m_media;
  QString m_directory, m_pattern;
  QStringList m_selected;
  CaptureActionPorts m_ports;
  QString m_activity;
  QTimer *m_timer = nullptr;
  bool m_started = false;
  bool m_finished = false;
};
} // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::CapturePipelineReceipt)
