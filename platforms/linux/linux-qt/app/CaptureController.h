// Capture flow for the linux-qt app: portal snapshot, then either the whole
// desktop, the monitor under the pointer, the focused window (CORE-014), or
// overlays driven by RegionSession, pixel acquisition through
// RegionGeometry::composite, and the post-capture pipeline. Every decision
// lives in the src/ libraries; this class wires them to Qt.
#pragma once

#include "capture/RegionSession.h"
#include "SnapshotComposition.h"
#include "CapturePipeline.h"
#include "workflows/CaptureWorkflows.h"
#include "workflows/WorkflowExecution.h"

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
class WindowDiscovery;
struct CaptureComposition;

class CaptureController final : public QObject {
  Q_OBJECT

public:
  using ScreenshotFactory = std::function<PortalScreenshot *(QObject *)>;
  explicit CaptureController(QString saveDirectory, QObject *parent = nullptr, ScreenshotFactory factory = {}, SnapshotComposer composer = {}, CaptureActionPorts actionPorts = {}, PipelineJournalStorage journal = {});
  ~CaptureController() override;

  void selectRecordingRegion(std::function<void(QRectF)> selected);
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
  std::optional<xerahs::capture::PhysicalRect> snapPreview() const;
  bool selectionSettled() const;
  std::optional<xerahs::capture::PhysicalPoint> keyboardCursor() const;
  QScreen *hintScreen() const;
  std::optional<int> compositionProgress() const;
  QString pipelineActivity() const { return m_pipelineActivity; }
  CapturePipelineReceipt lastPipelineResult() const { return m_lastPipelineResult; }
  bool discardRetainedResult(const CapturePipelineReceipt &expected);
  bool retryFailedActions(const CapturePipelineReceipt &expected);
  void setActionSelectionParent(QWidget *parent);
  void setSaveDirectory(const QString &directory) { m_saveDirectory = directory; }
  void setDefaultWorkflowResolver(std::function<xerahs::workflows::WorkflowChoice()> resolver) { m_workflowResolver = std::move(resolver); }
  std::function<void()> prepareWorkflowCapture(const xerahs::workflows::CaptureWorkflow &workflow, const QString &requestId,
      const QString &origin, xerahs::workflows::WorkflowExecution::Writer writer);
  // Starts the workflow's capture source: region, full-screen, monitor or active-window.
  void captureWorkflow(const xerahs::workflows::CaptureWorkflow &workflow);
  QString captureSource() const { return m_source; }
  void setWindowDiscovery(std::unique_ptr<WindowDiscovery> discovery);
  // CORE-015: a long-running capture owned elsewhere (scrolling) holds the same
  // admission as a controller capture. cancel() invokes onCancel while held.
  bool acquireExternal(std::function<void()> onCancel);
  void releaseExternal();
  // Hands a finished image from the held external capture to the default
  // workflow's post-capture pipeline. Returns false (and releases) if none was held.
  bool submitExternalImage(const QImage &image, const QString &source);
  // Future pipelines copy this GUI-thread preference value; active workers
  // retain the style already captured by their pipeline constructor.
  void setAnnotationStyle(const xerahs::editor::RectangleStyle &style) { m_actionPorts.annotationStyle = style; }

public slots:
  void captureRegion();
  void captureRegionWithActions();

signals:
  void busyChanged(bool busy);
  // CORE-010: the originating operation and its true result.
  void notify(const QString &title, const QString &body, bool success);
  void compositionProgressChanged(int percent);
  void pipelineActivityChanged(const QString &text);
  void retainedResultChanged();
  void choosingActions();

private:
  void beginCapture(QStringList selected);
  void beginConfiguredCapture(bool chooseActions);
  void onSnapshot(int outcome, const QImage &image, const QString &detail);
  void onOutcome(const xerahs::capture::SessionOutcome &outcome);
  void captureWholeSource(const xerahs::capture::Topology &topology);
  QString sourceTitle(const QString &suffix) const;
  WindowDiscovery *windowDiscovery();
  void afterInput();
  void acquire();
  void pollComposition();
  void runPipeline();
  void connectPipeline(CapturePipeline *pipeline);
  void publishPipeline(const CapturePipelineReceipt &result, bool executionConfirmed = true);
  void closeOverlays();
  void finish();
  void recordCaptureOutcome(const QString &state, const QString &diagnostic = {});
  void repaint();
  void announce();

  std::function<void(QRectF)> m_regionSelected;
  QString m_saveDirectory;
  ScreenshotFactory m_screenshotFactory;
  SnapshotComposer m_composer;
  std::shared_ptr<CaptureComposition> m_composition;
  QTimer *m_compositionTimer = nullptr;
  int m_reportedProgress = -1;
  quint64 m_generation = 0;
  CaptureActionPorts m_actionPorts;
  QStringList m_selectedActions;
  QString m_source = QStringLiteral("region");
  std::unique_ptr<WindowDiscovery> m_windows;
  std::function<xerahs::workflows::WorkflowChoice()> m_workflowResolver;
  QJsonObject m_workflowSnapshot;
  std::shared_ptr<xerahs::workflows::WorkflowExecution> m_execution;
  xerahs::workflows::CaptureOutput m_captureOutput;
  QPointer<QWidget> m_selectionParent;
  PipelineJournalStorage m_journal;
  QPointer<CapturePipeline> m_pipeline;
  QString m_pipelineActivity;
  CapturePipelineReceipt m_lastPipelineResult;
  bool m_pendingPipelineCancellation = false;
  bool m_busy = false;
  std::function<void()> m_externalCancel;
  bool m_external = false;
  QPointer<PortalScreenshot> m_portal;
  QImage m_desktop;  // frozen snapshot in desktop physical pixels
  std::optional<xerahs::capture::PhysicalPoint> m_hoverPoint;
  std::unique_ptr<xerahs::capture::RegionSession> m_session;
  std::vector<std::unique_ptr<RegionOverlay>> m_overlays;
  std::optional<xerahs::capture::PhysicalPoint> m_keyboardCursor;
  bool m_keyboardSelecting = false;
  std::optional<QImage> m_captured;
  std::vector<QMetaObject::Connection> m_screenConnections;
};

}  // namespace xerahs::app
