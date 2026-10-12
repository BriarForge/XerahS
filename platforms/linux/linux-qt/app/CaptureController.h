// Region capture flow for the linux-qt app: portal snapshot, overlays driven
// by RegionSession, pixel acquisition through RegionGeometry::composite, and
// the post-capture pipeline. Every decision lives in the src/ libraries; this
// class wires them to Qt.
#pragma once

#include "capture/RegionSession.h"
#include "SnapshotComposition.h"
#include "CapturePipeline.h"

#include <QImage>
#include <QObject>
#include <QPointer>

#include <memory>
#include <functional>
#include <optional>
#include <vector>

class QScreen;
class QTimer;

namespace xerahs::app {

class PortalScreenshot;
class RegionOverlay;
struct CaptureComposition;

class CaptureController final : public QObject {
  Q_OBJECT

public:
  using ScreenshotFactory = std::function<PortalScreenshot *(QObject *)>;
  explicit CaptureController(QString saveDirectory, QObject *parent = nullptr, ScreenshotFactory factory = {}, SnapshotComposer composer = {}, CaptureActionPorts actionPorts = {});
  ~CaptureController() override;

  static QString instructions();
  QString saveDirectory() const { return m_saveDirectory; }
  bool busy() const { return m_busy; }

  // Overlay input, in desktop physical pixels.
  void pointerDown(xerahs::capture::PhysicalPoint point);
  void pointerMove(xerahs::capture::PhysicalPoint point);
  void pointerUp(xerahs::capture::PhysicalPoint point);
  bool key(int key, Qt::KeyboardModifiers modifiers, const xerahs::capture::Display &display);
  void cancel();

  // Overlay painting state.
  std::optional<xerahs::capture::PhysicalRect> selection() const;
  bool selectionSettled() const;
  std::optional<xerahs::capture::PhysicalPoint> keyboardCursor() const;
  QScreen *hintScreen() const;
  std::optional<int> compositionProgress() const;
  QString pipelineActivity() const { return m_pipelineActivity; }
  CapturePipelineReceipt lastPipelineResult() const { return m_lastPipelineResult; }

public slots:
  void captureRegion();

signals:
  void busyChanged(bool busy);
  // CORE-010: the originating operation and its true result.
  void notify(const QString &title, const QString &body, bool success);
  void compositionProgressChanged(int percent);
  void pipelineActivityChanged(const QString &text);

private:
  void onSnapshot(int outcome, const QImage &image, const QString &detail);
  void onOutcome(const xerahs::capture::SessionOutcome &outcome);
  void afterInput();
  void acquire();
  void pollComposition();
  void runPipeline(const xerahs::capture::PhysicalRect &rect);
  void publishPipeline(const xerahs::capture::PhysicalRect &rect, const CapturePipelineReceipt &result);
  void closeOverlays();
  void finish();
  void repaint();
  void announce();

  QString m_saveDirectory;
  ScreenshotFactory m_screenshotFactory;
  SnapshotComposer m_composer;
  std::shared_ptr<CaptureComposition> m_composition;
  QTimer *m_compositionTimer = nullptr;
  int m_reportedProgress = -1;
  quint64 m_generation = 0;
  CaptureActionPorts m_actionPorts;
  QPointer<CapturePipeline> m_pipeline;
  QString m_pipelineActivity;
  CapturePipelineReceipt m_lastPipelineResult;
  bool m_pendingPipelineCancellation = false;
  bool m_busy = false;
  QPointer<PortalScreenshot> m_portal;
  QImage m_desktop;  // frozen snapshot in desktop physical pixels
  std::unique_ptr<xerahs::capture::RegionSession> m_session;
  std::vector<std::unique_ptr<RegionOverlay>> m_overlays;
  std::optional<xerahs::capture::PhysicalPoint> m_keyboardCursor;
  bool m_keyboardSelecting = false;
  std::optional<QImage> m_captured;
  std::vector<QMetaObject::Connection> m_screenConnections;
};

}  // namespace xerahs::app
