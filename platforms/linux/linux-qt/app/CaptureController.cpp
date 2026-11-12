#include "CaptureController.h"
#include "OwnedWork.h"
#include "capture/CompositorErrors.h"

#include "CaptureActions.h"
#include "PortalScreenshot.h"
#include "RegionOverlay.h"
#include "WindowDiscovery.h"

#include "actions/PostCapturePipeline.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <cmath>
#include <atomic>
#include <algorithm>
#include <mutex>

namespace xerahs::app {

using namespace xerahs::capture;

struct CaptureComposition {
  std::atomic<bool> cancelled{false};
  std::atomic<int> progress{0};
  std::mutex mutex;
  bool ready = false;
  ComposedSnapshot result;
};

namespace {

// Fallback for embedded callers without a workflow host. The application
// installs its persisted default resolver before exposing capture commands.
const QStringList kDefaultActions = {QStringLiteral("save"), QStringLiteral("image-clipboard")};

}  // namespace

CaptureController::CaptureController(QString saveDirectory, QObject *parent, ScreenshotFactory factory, SnapshotComposer composer, CaptureActionPorts actionPorts, PipelineJournalStorage journal)
    : QObject(parent), m_saveDirectory(std::move(saveDirectory)), m_screenshotFactory(std::move(factory)), m_composer(std::move(composer)), m_actionPorts(std::move(actionPorts)), m_journal(std::move(journal)) {
  m_compositionTimer = new QTimer(this); m_compositionTimer->setInterval(10);
  connect(m_compositionTimer, &QTimer::timeout, this, &CaptureController::pollComposition);
  // QScreen has no devicePixelRatioChanged signal on the Qt 6.4 baseline.
  // Compare the approved topology while consent/selection/composition is active.
  m_topologyTimer = new QTimer(this); m_topologyTimer->setInterval(25);
  connect(m_topologyTimer, &QTimer::timeout, this, [this] {
    if (!m_busy || m_pipeline || (m_session && isTerminal(m_session->state()))) return;
    if (!topologyMatches()) topologyChanged();
  });
}

CaptureController::~CaptureController() {
  m_topologyTimer->stop();
  m_compositionTimer->stop();
  if (m_compositorWork) m_compositorWork->cancel();
  if (m_pipeline) { disconnect(m_pipeline, nullptr, this, nullptr); m_pipeline->cancel(); }
  if (m_composition) { m_composition->cancelled.store(true); m_composition.reset(); }
  if (m_portal) {
    auto *portal = m_portal.data(); disconnect(portal, nullptr, this, nullptr);
    portal->setParent(nullptr); portal->cancel(); portal->retire(); m_portal = nullptr;
  }
  closeOverlays();
}

QString CaptureController::instructions() {
  return tr("Frozen region capture. Drag with the pointer, or press Space to start and finish a selection at the keyboard "
      "cursor. Arrow keys move by 1 physical pixel, Shift+Arrow by 10. With a selection, Alt+Arrow resizes. Enter "
      "captures. Drag a selected corner handle to resize. Escape cancels.");
}

void CaptureController::captureRegion() {
  if (m_workflowResolver) { beginConfiguredCapture(false); return; }
  if (m_busy) return;
  m_workflowSnapshot = {};
  m_captureOutput = {};
  m_source = QStringLiteral("region");
  beginCapture(kDefaultActions);
}
void CaptureController::captureLastRegion() {
  if (m_busy) return;
  if (m_workflowResolver) {
    auto choice = m_workflowResolver();
    if (!choice.workflow) { emit notify("Capture workflow unavailable", choice.diagnostic, false); return; }
    choice.workflow->capture = QStringLiteral("last-region"); captureWorkflow(*choice.workflow); return;
  }
  m_workflowSnapshot = {}; m_captureOutput = {}; m_source = QStringLiteral("last-region");
  beginCapture(kDefaultActions);
}
void CaptureController::captureRegionWithActions() {
  if (m_workflowResolver) { beginConfiguredCapture(true); return; }
  if (m_busy) return;
  m_workflowSnapshot = {};
  m_captureOutput = {};
  m_source = QStringLiteral("region");
  auto selected = kDefaultActions; selected.prepend(QStringLiteral("after-capture-window"));
  beginCapture(std::move(selected));
}
void CaptureController::beginConfiguredCapture(bool chooseActions) {
  if (m_busy) return;
  auto choice = m_workflowResolver();
  if (!choice.workflow) { emit notify("Capture workflow unavailable", "Open Capture workflows to review or reload the saved configuration. " + choice.diagnostic, false); return; }
  choice.workflow->chooseActions |= chooseActions; captureWorkflow(*choice.workflow);
}
std::function<void()> CaptureController::prepareWorkflowCapture(const xerahs::workflows::CaptureWorkflow &workflow,
    const QString &requestId, const QString &origin, xerahs::workflows::WorkflowExecution::Writer writer) {
  if (m_busy || !xerahs::workflows::workflowAvailability(workflow, CaptureActions::selectableActions()).isEmpty()) return {};
  auto effective = workflow;
  if (effective.output.directory.isEmpty()) effective.output.directory = m_saveDirectory;
  auto execution = std::make_shared<xerahs::workflows::WorkflowExecution>(requestId, origin, effective, std::move(writer));
  if (!execution->admit()) {
    emit notify("Capture not started", "Private workflow execution storage is unavailable or full. No capture was admitted. Review storage ownership, permissions and retained records before retrying.", false);
    return {};
  } // No capture/ACK without durable admission.
  return [guard = QPointer<CaptureController>(this), effective, execution] {
    if (!guard || guard->busy()) return; // RAII records not-started on release.
    if (!execution->start()) { guard->presentCaptureObservation(execution->recordedObservation()); return; }
    guard->m_execution = execution;
    guard->captureWorkflow(effective);
  };
}
void CaptureController::presentCaptureObservation(const QJsonObject &observation) {
  const auto text = xerahs::actions::pipelineObservationText(observation);
  qInfo().noquote() << "Workflow terminal" << text;
  const QPointer<CaptureController> guard(this);
  emit pipelineActivityChanged(text);
  QString body = text;
  if (observation.value("state") == "outcome-unconfirmed") body += QStringLiteral("\nworkflow-storage-unavailable: The execution outcome could not be confirmed.");
  if (!observation.value("diagnostic").toString().isEmpty()) body += QLatin1Char('\n') + observation.value("diagnostic").toString();
  if (!observation.value("detail").toString().isEmpty()) body += QLatin1Char('\n') + observation.value("detail").toString();
  if (guard) emit notify(QStringLiteral("Capture needs review"), body, false);
}
void CaptureController::recordCaptureOutcome(const QString &state, const QString &diagnostic) {
  if (m_execution) {
    m_execution->finish(state, diagnostic);
    m_earlyObservation = m_execution->recordedObservation();
  }
}
void CaptureController::captureWorkflow(const xerahs::workflows::CaptureWorkflow &workflow) {
  if (m_busy) return;
  const auto diagnostic = xerahs::workflows::workflowAvailability(workflow, CaptureActions::selectableActions());
  if (!diagnostic.isEmpty()) { emit notify("Capture workflow unavailable", "Open Capture workflows to review the selected actions. " + diagnostic, false); return; }
  auto effective = workflow;
  if (effective.output.directory.isEmpty()) effective.output.directory = m_saveDirectory;
  m_captureOutput = effective.output;
  m_source = effective.capture;
  m_workflowSnapshot = xerahs::workflows::workflowSnapshot(effective);
  beginCapture(xerahs::workflows::workflowActions(workflow));
}
void CaptureController::setActionSelectionParent(QWidget *parent) { m_selectionParent = parent; }
void CaptureController::selectRecordingRegion(std::function<void(QRectF)> selected) {
  if (m_busy) return;
  m_regionSelected = std::move(selected);
  m_source = QStringLiteral("region"); m_workflowSnapshot = {}; m_captureOutput = {};
  beginCapture({});
}
void CaptureController::beginCapture(QStringList selected) {
  if (m_busy) return;
  m_regionSettings = m_regionSettingsResolver ? m_regionSettingsResolver() : SessionSettings{};
  m_captureLastRegionWriter = m_lastRegionWriter; m_lastRegionDiagnostic.clear();
  m_selectedActions = std::move(selected);
  ++m_generation;
  m_pendingPipelineCancellation = false;
  m_busy = true;
  // Freeze identity, bounds, per-screen DPR and rotation BEFORE any screenshot.
  m_displays = m_topologyReader ? m_topologyReader() : screenTopology();
  m_topology = displayMappings(m_displays);
  m_lastRectangle.reset();
  if (m_source == QStringLiteral("last-region")) {
    const auto previous = m_lastRegionReader ? m_lastRegionReader() : std::nullopt;
    if (previous) m_lastRectangle = validateLastRegion(*previous, m_topology);
    if (!m_lastRectangle) { failCapture(diagnostic::lastRegionInvalid); return; }
  }
  if (m_topology.empty()) { failCapture(diagnostic::captureTopologyMismatch); return; }
  // Watch before consent, so a late portal image cannot bless a changed layout.
  const auto changed = [this] { topologyChanged(); };
  m_screenConnections.push_back(connect(qGuiApp, &QGuiApplication::screenAdded, this, changed));
  m_screenConnections.push_back(connect(qGuiApp, &QGuiApplication::screenRemoved, this, changed));
  for (const auto &entry : m_displays) if (auto *screen = entry.screen.data()) {
    m_screenConnections.push_back(connect(screen, &QScreen::geometryChanged, this, changed));
    m_screenConnections.push_back(connect(screen, &QScreen::orientationChanged, this, changed));
    m_screenConnections.push_back(connect(screen, &QScreen::physicalDotsPerInchChanged, this, changed));
    m_screenConnections.push_back(connect(screen, &QScreen::logicalDotsPerInchChanged, this, changed));
  }
  m_topologyTimer->start();
  if (m_regionSettings.includeCursor) { beginCompositorCapture(); return; }
  m_portal = m_screenshotFactory ? m_screenshotFactory(this) : new PortalScreenshot(this);
  if (!m_portal) {
    const QPointer<CaptureController> guard(this);
    emit busyChanged(true);
    if (!guard) return;
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed); if (!guard) return;
    if (!m_execution) emit notify(sourceTitle("capture failed"), diagnostic::captureSourceFailed, false);
    if (guard) finish();
    return;
  }
  auto *portal = m_portal.data();
  connect(portal, &PortalScreenshot::finished, this,
          [this, portal](PortalScreenshot::Outcome outcome, const QImage &image, const QString &detail) {
            // Queued old-request callbacks cannot finish a newer capture.
            if (!m_busy || m_portal != portal) return;
            m_portal = nullptr; portal->setParent(nullptr); portal->retire();
            onSnapshot(static_cast<int>(outcome), image, detail);
          });
  const QPointer<CaptureController> guard(this);
  emit busyChanged(true);
  // Availability listeners may synchronously cancel before acquisition starts.
  if (guard && m_busy && m_portal == portal) portal->request();
}

void CaptureController::beginCompositorCapture() {
  if (m_displays.size() != 1 || m_displays.front().outputName.isEmpty()) {
    onSnapshot(int(PortalScreenshot::Outcome::Failed), {}, compositorCaptureFailureMessage(kCompositorLayoutUnproven)); return;
  }
  const auto display = m_displays.front();
  const auto generation = m_generation;
  auto capture = m_compositorCapture;
  if (!capture) capture = &CompositorFrameCapture::capture;
  m_compositorPending = true;
  const QPointer<CaptureController> guard(this);
  emit busyChanged(true);
  if (!guard || !m_busy || !m_compositorPending || m_generation != generation) return;
  m_compositorWork = OwnedWork::run<CompositorFrame>(this,
      [capture = std::move(capture)](const OwnedWork::Context &context) {
        CompositorFrameRequest request; request.includeCursor = true;
        request.cancelled = [context] { return context.cancelled(); };
        try { return capture(request); }
        catch (...) { CompositorFrame failed; failed.diagnostic = kCompositorCaptureFailed; return failed; }
      }, [this, generation, display](CompositorFrame frame) {
        if (!m_busy || !m_compositorPending || m_generation != generation) return;
        m_compositorPending = false; m_compositorWork.reset();
        if (frame.status == CompositorFrame::Status::Cancelled) {
          onSnapshot(int(PortalScreenshot::Outcome::Cancelled), {}, {}); return;
        }
        if (frame.status != CompositorFrame::Status::Captured) {
          onSnapshot(int(PortalScreenshot::Outcome::Failed), {}, compositorCaptureFailureMessage(frame.diagnostic)); return;
        }
        const auto &mapping = display.mapping;
        if (!frame.singleOutputBufferOriginProven || !frame.cursorPaintRequested || !frame.cursorPaintedIntoFrame ||
            frame.outputName != display.outputName || frame.transform != 0 ||
            frame.scale != mapping.scale || frame.logicalX != mapping.logical.left || frame.logicalY != mapping.logical.top ||
            frame.bufferWidth != mapping.physical.width() || frame.bufferHeight != mapping.physical.height() ||
            frame.image.width() != mapping.physical.width() || frame.image.height() != mapping.physical.height()) {
          failCapture(diagnostic::captureTopologyMismatch, compositorCaptureFailureMessage(kCompositorSourceMismatch)); return;
        }
        onSnapshot(int(PortalScreenshot::Outcome::Captured), frame.image, {});
      });
}

void CaptureController::onSnapshot(int outcome, const QImage &image, const QString &detail) {
  const QPointer<CaptureController> guard(this);
  const auto result = static_cast<PortalScreenshot::Outcome>(outcome);
  if (result == PortalScreenshot::Outcome::Cancelled) { recordCaptureOutcome("cancelled"); if (guard) finish(); return; }
  if (result == PortalScreenshot::Outcome::Failed) {
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed);
    if (!guard) return;
    if (!m_execution) emit notify(sourceTitle("capture failed"),
                detail.isEmpty() ? diagnostic::captureSourceFailed : detail, false);
    if (guard) finish();
    return;
  }

  if (result == PortalScreenshot::Outcome::Captured && !topologyMatches()) { topologyChanged(); return; }
  const auto &topology = m_topology;
  if (result != PortalScreenshot::Outcome::Denied) {
    const auto bounds = boundingBox(topology);
    if (image.isNull() || image.width() != bounds.width() || image.height() != bounds.height()) {
      failCapture(diagnostic::captureTopologyMismatch); return;
    }
  }

  if (m_source != QStringLiteral("region") && m_source != QStringLiteral("last-region")) {
    if (result == PortalScreenshot::Outcome::Denied) {
      recordCaptureOutcome("permission-denied", diagnostic::capturePermissionDenied); if (!guard) return;
      if (!m_execution) emit notify(sourceTitle("capture denied"), diagnostic::capturePermissionDenied, false);
      if (guard) emit capturePermissionDenied();
      if (guard) finish();
      return;
    }
    m_desktop = image;
    captureWholeSource(topology);
    return;
  }

  const auto settings = m_regionSettings;
  const auto targets = settings.snapping && result != PortalScreenshot::Outcome::Denied
      ? windowDiscovery()->targets(topology, QCoreApplication::applicationPid()) : std::vector<SnapTarget>{};
  m_session = std::make_unique<RegionSession>(topology, settings, targets,
                                              [this](const SessionOutcome &o) { onOutcome(o); });
  m_session->start();
  if (result == PortalScreenshot::Outcome::Denied) {
    m_session->permission(Permission::Denied);  // RC-002: no overlay, no re-prompt
    return;
  }
  m_desktop = image; // single immutable decoded session snapshot
  m_session->permission(Permission::Authorized);
  if (m_lastRectangle) {
    const auto rect = *m_lastRectangle;
    m_session->ready();
    m_session->pointerDown({rect.left, rect.top});
    m_session->pointerUp({rect.right, rect.bottom});
    m_session->confirm();
    afterInput();
    return;
  }
  const auto bounds = boundingBox(topology);
  for (const auto &entry : m_displays) {
    const auto &display = entry.mapping;
    const auto &p = display.physical;
    const QRect source(int(p.left - bounds.left), int(p.top - bounds.top), int(p.width()), int(p.height()));
    m_overlays.push_back(std::make_unique<RegionOverlay>(*this, entry.screen, display, m_desktop, source));
  }

  for (auto &overlay : m_overlays) overlay->showFullScreen();
  m_session->ready();
  if (!m_overlays.empty()) {
    m_overlays.front()->activateWindow();
    m_overlays.front()->setFocus();
  }
}

void CaptureController::failCapture(const QString &code, const QString &message) {
  const QPointer<CaptureController> guard(this);
  recordCaptureOutcome("failed", code);
  if (!guard) return;
  if (!m_execution) emit notify(sourceTitle("capture failed"), message.isEmpty() ? code : message, false);
  if (guard) finish();
}

bool CaptureController::topologyMatches() const {
  const auto displays = m_topologyReader ? m_topologyReader() : screenTopology();
  if (m_regionSettings.includeCursor && (displays.size() != 1 || m_displays.size() != 1 ||
      displays.front().outputName != m_displays.front().outputName)) return false;
  const auto current = displayMappings(displays);
  if (current.size() != m_topology.size()) return false;
  for (const auto &display : m_topology) {
    const auto *now = findDisplay(current, display.id);
    if (!now || now->physical != display.physical || now->scale != display.scale || now->rotation != display.rotation ||
        now->logical.left != display.logical.left || now->logical.top != display.logical.top ||
        now->logical.right != display.logical.right || now->logical.bottom != display.logical.bottom ||
        now->colorDescription != display.colorDescription || now->captureAvailable != display.captureAvailable) return false;
  }
  return true;
}

void CaptureController::topologyChanged() {
  if (!m_busy || m_pipeline || (m_session && isTerminal(m_session->state()))) return;
  if (m_composition) m_composition->cancelled.store(true);
  if (m_compositorWork) { m_compositorWork->cancel(); m_compositorWork.reset(); }
  m_compositorPending = false;
  if (m_session) { m_session->topologyChanged(); afterInput(); return; }
  // Pending consent: detach before cancelling to ignore its terminal callback.
  if (m_portal) {
    auto *portal = m_portal.data(); disconnect(portal, nullptr, this, nullptr);
    m_portal = nullptr; portal->setParent(nullptr); portal->cancel(); portal->retire();
  }
  failCapture(diagnostic::displayTopologyChanged);
}

QString CaptureController::sourceTitle(const QString &suffix) const {
  const auto label = xerahs::workflows::captureSourceLabel(m_source);
  return (label.isEmpty() ? QStringLiteral("Capture") : label) + QStringLiteral(" ") + suffix;
}

void CaptureController::setWindowDiscovery(std::unique_ptr<WindowDiscovery> discovery) { m_windows = std::move(discovery); }
WindowDiscovery *CaptureController::windowDiscovery() {
  if (!m_windows) m_windows = nativeWindowDiscovery();
  return m_windows.get();
}

// CORE-014: full screen takes the whole portal image; monitor takes the
// display under the pointer, falling back to the primary display when the
// compositor does not report a pointer position. Active window copies the
// focused window's rectangle. No overlay is shown, and a missing window is
// not replaced with the full screen.
void CaptureController::captureWholeSource(const Topology &topology) {
  const QPointer<CaptureController> guard(this);
  const auto fail = [&](const QString &detail) {
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed); if (!guard) return;
    if (m_execution) m_earlyObservation.insert("detail", detail);
    else emit notify(sourceTitle("capture failed"), detail, false);
    if (guard) finish();
  };
  QRect source = m_desktop.rect();
  if (m_source == QStringLiteral("monitor")) {
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const Display *display = screen ? findDisplay(topology, screenIdentity(screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber())) : nullptr;
    if (!display) { fail(diagnostic::captureSourceFailed); return; }
    const PhysicalRect &p = display->physical;
    source = QRect(int(p.left - boundingBox(topology).left), int(p.top - boundingBox(topology).top), int(p.width()), int(p.height())).intersected(m_desktop.rect());
  } else if (m_source == QStringLiteral("active-window")) {
    const auto window = windowDiscovery()->activeWindow(topology, QCoreApplication::applicationPid());
    if (!window) { fail(QStringLiteral("No active window to capture")); return; }
    const PhysicalRect &p = window->physical;
    source = QRect(int(p.left - boundingBox(topology).left), int(p.top - boundingBox(topology).top), int(p.width()), int(p.height())).intersected(m_desktop.rect());
    if (source.isEmpty()) { fail(QStringLiteral("No active window to capture")); return; }
  }
  if (m_desktop.isNull() || source.isEmpty()) { fail(diagnostic::captureSourceFailed); return; }
  QImage pixels = m_desktop.copy(source).convertToFormat(QImage::Format_ARGB32);
  if (pixels.isNull()) { fail(diagnostic::captureSourceFailed); return; }
  pixels.setColorSpace(m_desktop.colorSpace());
  m_captured = std::move(pixels);
  runPipeline();
}

bool CaptureController::beginCornerResize(Corner corner, PhysicalPoint point) {
  if (!m_session || m_session->state() != SessionState::Selected || !m_session->selection()) return false;
  const auto rect = *m_session->selection();
  const bool left = corner == Corner::TopLeft || corner == Corner::BottomLeft;
  const bool top = corner == Corner::TopLeft || corner == Corner::TopRight;
  m_resizeCorner = corner;
  m_resizeOffset = {point.x - (left ? rect.left : rect.right), point.y - (top ? rect.top : rect.bottom)};
  m_keyboardCursor.reset(); m_keyboardSelecting = false;
  return true;
}

void CaptureController::pointerDown(PhysicalPoint point) {
  if (!m_session) return;
  m_resizeCorner.reset();
  m_keyboardCursor.reset();
  m_keyboardSelecting = false;
  m_session->pointerDown(point);
  afterInput();
}

void CaptureController::pointerMove(PhysicalPoint point) {
  if (!m_session || m_keyboardSelecting) return;
  m_hoverPoint = point;
  if (m_resizeCorner) {
    if (m_session->resizeCorner(*m_resizeCorner, {point.x - m_resizeOffset.x, point.y - m_resizeOffset.y})) announce();
  } else m_session->pointerMove(point);
  afterInput();
}

void CaptureController::pointerUp(PhysicalPoint point) {
  if (!m_session || m_keyboardSelecting) return;
  if (m_resizeCorner) {
    const auto corner = *m_resizeCorner; m_resizeCorner.reset();
    if (m_session->resizeCorner(corner, {point.x - m_resizeOffset.x, point.y - m_resizeOffset.y})) announce();
  } else m_session->pointerUp(point);
  afterInput();
}

bool CaptureController::acquireExternal(std::function<void()> onCancel) {
  if (m_busy) return false;
  ++m_generation; m_busy = true; m_external = true; m_externalCancel = std::move(onCancel);
  emit busyChanged(true);
  return true;
}
void CaptureController::releaseExternal() {
  if (!m_external) return;
  m_external = false; m_externalCancel = {};
  finish();
}
bool CaptureController::submitExternalImage(const QImage &image, const QString &source) {
  if (!m_external || image.isNull()) { releaseExternal(); return false; }
  m_external = false; m_externalCancel = {};
  QStringList actions = kDefaultActions;
  m_captureOutput = {}; m_workflowSnapshot = {}; m_source = source;
  if (m_workflowResolver) {
    auto choice = m_workflowResolver();
    if (choice.workflow) {
      if (choice.workflow->output.directory.isEmpty()) choice.workflow->output.directory = m_saveDirectory;
      m_captureOutput = choice.workflow->output;
      m_workflowSnapshot = xerahs::workflows::workflowSnapshot(*choice.workflow);
      actions = xerahs::workflows::workflowActions(*choice.workflow);
    }
  }
  m_selectedActions = actions; m_pendingPipelineCancellation = false; m_execution.reset();
  m_captured = image;
  runPipeline();
  return true;
}

void CaptureController::cancel() {
  if (m_external) { if (auto hook = m_externalCancel) hook(); return; }
  if (m_pipeline) { m_pipeline->cancel(); return; }
  if (m_session && m_session->state() == SessionState::Completed) m_pendingPipelineCancellation = true;
  if (m_composition) m_composition->cancelled.store(true);
  if (m_compositorPending) {
    m_compositorPending = false;
    if (m_compositorWork) { m_compositorWork->cancel(); m_compositorWork.reset(); }
    onSnapshot(int(PortalScreenshot::Outcome::Cancelled), {}, {}); return;
  }
  if (m_portal) { m_portal->cancel(); return; }
  if (!m_session) return;
  m_session->cancel();
  afterInput();
}

bool CaptureController::discardRetainedResult(const CapturePipelineReceipt &expected) {
  if (m_busy || !expected || m_lastPipelineResult != expected) return false;
  m_lastPipelineResult.reset(); emit retainedResultChanged(); return true;
}

bool CaptureController::key(int key, Qt::KeyboardModifiers modifiers, const Display &display) {
  if (!m_session) return false;
  const SessionState state = m_session->state();
  const bool precision = modifiers & Qt::ShiftModifier;
  const qint64 step = precision ? kPrecisionStep : kKeyboardStep;
  qint64 dx = 0;
  qint64 dy = 0;
  switch (key) {
    case Qt::Key_Escape:
      cancel();
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      m_session->keyEnter();
      afterInput();
      return true;
    case Qt::Key_Space: {
      if (!m_keyboardCursor) {
        const PhysicalRect &p = display.physical;
        m_keyboardCursor = PhysicalPoint{(p.left + p.right) / 2, (p.top + p.bottom) / 2};
      }
      if (state == SessionState::Idle || state == SessionState::Selected) {
        m_session->pointerDown(*m_keyboardCursor);
        m_keyboardSelecting = true;
      } else if (state == SessionState::Selecting && m_keyboardSelecting) {
        m_keyboardSelecting = false;
        m_session->pointerUp(*m_keyboardCursor);
      }
      afterInput();
      return true;
    }
    case Qt::Key_Left: dx = -1; break;
    case Qt::Key_Right: dx = 1; break;
    case Qt::Key_Up: dy = -1; break;
    case Qt::Key_Down: dy = 1; break;
    default: return false;
  }

  if (state == SessionState::Selected) {
    KeyboardStep adjust;
    adjust.precision = precision;
    if (modifiers & Qt::AltModifier) {
      adjust.action = KeyboardStep::Action::Resize;
      adjust.edge = dx != 0 ? Edge::Right : Edge::Bottom;
      adjust.delta = dx != 0 ? dx : dy;
    } else {
      adjust.dx = dx;
      adjust.dy = dy;
    }
    if (m_session->keyboardAdjust(adjust)) announce();
    afterInput();
    return true;
  }

  // Idle or keyboard selecting: move the keyboard cursor (RC-019).
  if (!m_keyboardCursor) {
    const PhysicalRect &p = display.physical;
    m_keyboardCursor = PhysicalPoint{(p.left + p.right) / 2, (p.top + p.bottom) / 2};
  }
  m_keyboardCursor = clampPhysical(m_session->topology(),
                                   PhysicalPoint{m_keyboardCursor->x + dx * step, m_keyboardCursor->y + dy * step});
  if (state == SessionState::Selecting && m_keyboardSelecting) {
    m_session->pointerMove(*m_keyboardCursor);
    announce();
  }
  afterInput();
  return true;
}

std::optional<PhysicalRect> CaptureController::selection() const {
  return m_session ? m_session->selection() : std::nullopt;
}

std::optional<PhysicalRect> CaptureController::snapPreview() const {
  return m_session && m_hoverPoint ? m_session->snapPreviewAt(*m_hoverPoint) : std::nullopt;
}

bool CaptureController::selectionSettled() const {
  return m_session && m_session->state() == SessionState::Selected;
}

std::optional<PhysicalPoint> CaptureController::keyboardCursor() const { return m_keyboardCursor; }

QScreen *CaptureController::hintScreen() const { return QGuiApplication::primaryScreen(); }
std::optional<int> CaptureController::compositionProgress() const {
  return m_composition ? std::optional<int>(m_reportedProgress < 0 ? 0 : m_reportedProgress) : std::nullopt;
}

void CaptureController::announce() {
  const auto rect = selection();
  if (!rect) return;
  const QString text = QStringLiteral("Selection %1 by %2 pixels at %3, %4")
                           .arg(rect->width())
                           .arg(rect->height())
                           .arg(rect->left)
                           .arg(rect->top);
  for (auto &overlay : m_overlays) overlay->announce(text);
}

void CaptureController::afterInput() {
  const QPointer<CaptureController> guard(this);
  if (m_session && m_session->state() == SessionState::Confirming) acquire();
  if (guard) repaint();
}

void CaptureController::repaint() {
  for (auto &overlay : m_overlays) overlay->update();
}

void CaptureController::acquire() {
  if (!topologyMatches()) { topologyChanged(); return; }
  if (m_composition) return; // repeated input never starts another acquisition
  const std::optional<PhysicalRect> rect = m_session->selection();
  if (!rect) {
    m_session->captureFailed();
    return;
  }
  if (m_regionSelected) { m_session->captureSucceeded(); return; }
  // RC-008/012: frozen source pixels, cancellation and progress are independent
  // of the GUI event loop. The worker owns its inputs, never the controller.
  try {
    auto job = std::make_shared<CaptureComposition>(); m_composition = job; m_reportedProgress = 0;
    const QPointer<CaptureController> guard(this);
    emit compositionProgressChanged(0);
    if (!guard) return;
    if (!m_session || m_session->state() != SessionState::Confirming || job->cancelled.load()) return;
    const auto composer = m_composer ? m_composer : SnapshotComposer(composeSnapshot);
    auto *worker = QThread::create([job, composer, desktop = m_desktop, rectangle = *rect, topology = m_session->topology()] {
      ComposedSnapshot result;
      try {
        result = composer(desktop, rectangle, topology, {[job] { return job->cancelled.load(); },
            [job](int percent) { job->progress.store(std::clamp(percent, 0, 100)); }});
      } catch (...) { result.diagnostic = diagnostic::captureSourceFailed; }
      std::lock_guard<std::mutex> lock(job->mutex);
      if (!job->cancelled.load()) job->result = std::move(result);
      job->ready = true;
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start(); m_compositionTimer->start();
  } catch (...) { m_composition.reset(); m_session->captureFailed(); }
}

void CaptureController::pollComposition() {
  const auto job = m_composition;
  if (!job || !m_busy || !m_session || m_session->state() != SessionState::Confirming) return;
  const int progress = job->progress.load();
  if (progress != m_reportedProgress) {
    const QPointer<CaptureController> guard(this);
    m_reportedProgress = progress; emit compositionProgressChanged(progress);
    if (!guard) return;
    if (m_composition != job || !m_session || m_session->state() != SessionState::Confirming) return;
    for (auto &overlay : m_overlays) overlay->announce(QStringLiteral("Preparing capture pixels, %1 percent. Escape cancels.").arg(progress));
    repaint();
  }
  ComposedSnapshot result;
  {
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!job->ready) return;
    result = std::move(job->result);
  }
  m_compositionTimer->stop(); m_composition.reset();
  if (!topologyMatches()) { topologyChanged(); return; }
  if (job->cancelled.load() || result.cancelled) m_session->cancel();
  else if (!result.diagnostic.isEmpty() || result.image.isNull()) m_session->captureFailed();
  else { m_captured = std::move(result.image); m_session->captureSucceeded(); }
}

void CaptureController::onOutcome(const SessionOutcome &outcome) {
  // Published from inside a RegionSession call; finish on the next turn.
  const auto generation = m_generation;
  QTimer::singleShot(0, this, [this, outcome, generation] {
    if (generation != m_generation || !m_busy) return;
    const QPointer<CaptureController> guard(this);
    if (m_regionSelected && outcome.state == SessionState::Completed && outcome.rectangle) {
      const auto rect = *outcome.rectangle;
      const auto display = m_session->topology().front();
      const QRectF logical(display.logical.left + (rect.left - display.physical.left) / display.scale,
                           display.logical.top + (rect.top - display.physical.top) / display.scale,
                           rect.width() / display.scale, rect.height() / display.scale);
      auto selected = std::move(m_regionSelected);
      closeOverlays(); finish();
      if (guard) selected(logical);
      return;
    }
    if (outcome.state == SessionState::Completed && outcome.rectangle && m_captured) {
      if (outcome.lastRegion && m_captureLastRegionWriter) rememberRegion(*outcome.lastRegion);
      else runPipeline();
      return; // cleanup follows the actual pipeline receipt
    }
    recordCaptureOutcome(outcome.diagnostic == diagnostic::capturePermissionDenied ? QStringLiteral("permission-denied") :
        outcome.state == SessionState::Cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"), outcome.diagnostic.value_or(QString{}));
    if (!guard) return;
    if (outcome.diagnostic) {
      const QString title = sourceTitle(outcome.state == SessionState::Failed ? QStringLiteral("capture failed") : QStringLiteral("capture cancelled"));
      if (!m_execution) emit notify(title, *outcome.diagnostic, false);
      if (guard && *outcome.diagnostic == diagnostic::capturePermissionDenied) emit capturePermissionDenied();
    }
    if (!guard) return;
    // RC-008: cancellation starts no post-capture actions.
    closeOverlays();
    finish();
  });
}

void CaptureController::rememberRegion(const LastRegion &region) {
  const auto generation = m_generation;
  const auto writer = m_captureLastRegionWriter;
  // Successful pixels already exist. Worker owns geometry and the writer, never
  // the controller or an image. Metadata failure cannot discard those pixels.
  OwnedWork::run<QString>(this, [writer, region](const OwnedWork::Context &) {
    try { return writer(region); }
    catch (...) { return QStringLiteral("last-region-storage-failed: Capture pixels were acquired, but last-region metadata could not be saved."); }
  }, [this, generation](QString diagnostic) {
    if (!m_busy || generation != m_generation) return;
    m_lastRegionDiagnostic = std::move(diagnostic);
    runPipeline();
  });
}

void CaptureController::runPipeline() {
  // The default path keeps overlay focus until clipboard dispatch. A chooser
  // releases overlays before taking focus; both retain the same frozen media.
  auto *pipeline = new CapturePipeline(*m_captured, m_captureOutput.directory.isEmpty() ? m_saveDirectory : m_captureOutput.directory,
      m_captureOutput.pattern, m_selectedActions, this, m_actionPorts, m_journal, m_selectionParent, m_workflowSnapshot, m_execution ? m_execution->record().id : QString{});
  if (m_execution && !m_execution->processing(pipeline->taskId())) {
    delete pipeline;
    const QPointer<CaptureController> guard(this);
    m_earlyObservation = m_execution->recordedObservation();
    auto detail = QStringLiteral("No post-capture action was started.");
    if (!m_lastRegionDiagnostic.isEmpty()) detail += QLatin1Char('\n') + m_lastRegionDiagnostic;
    m_earlyObservation.insert("detail", detail);
    if (guard) { closeOverlays(); finish(); } return;
  }
  connectPipeline(pipeline);
  const QPointer<CapturePipeline> pipelineGuard(pipeline);
  if (m_pendingPipelineCancellation) pipeline->cancel();
  if (pipelineGuard) pipelineGuard->start();
}
bool CaptureController::retryFailedActions(const CapturePipelineReceipt &expected) {
  if (m_busy || !expected || m_lastPipelineResult != expected || !expected->retryPlan().available()) return false;
  ++m_generation; m_busy = true; m_pendingPipelineCancellation = false;
  auto *pipeline = new CapturePipeline(expected, this, m_actionPorts, m_journal, m_selectionParent);
  connectPipeline(pipeline);
  const QPointer<CaptureController> guard(this); const QPointer<CapturePipeline> operation(pipeline);
  emit busyChanged(true); // cancel/destruction can arrive at first availability
  if (guard && operation) operation->start();
  return true;
}
void CaptureController::connectPipeline(CapturePipeline *pipeline) {
  m_pipeline = pipeline;
  connect(pipeline, &CapturePipeline::choosingActions, this, [this, pipeline] {
    if (m_pipeline != pipeline) return;
    const QPointer<CaptureController> guard(this); closeOverlays();
    if (guard) emit choosingActions();
  });
  connect(pipeline, &CapturePipeline::activityChanged, this, [this, pipeline](const QString &text) {
    if (m_pipeline != pipeline) return;
    m_pipelineActivity = text;
    const QPointer<CaptureController> guard(this);
    for (auto &overlay : m_overlays) { overlay->announce(text); if (!guard) return; }
    emit pipelineActivityChanged(text); if (guard) repaint();
  });
  connect(pipeline, &CapturePipeline::finished, this, [this, pipeline](const CapturePipelineReceipt &result) {
    if (m_pipeline != pipeline || !m_busy) return;
    m_pipeline = nullptr; pipeline->deleteLater(); m_lastPipelineResult = result;
    const QPointer<CaptureController> guard(this);
    const bool executionConfirmed = !m_execution || m_execution->complete(result->pipeline);
    emit retainedResultChanged(); if (!guard) return;
    publishPipeline(result, executionConfirmed);
    if (guard) { closeOverlays(); finish(); }
  });
}

void CaptureController::publishPipeline(const CapturePipelineReceipt &receipt, bool executionConfirmed) {
  const auto &result = receipt->pipeline;
  const auto observation = receipt->terminalObservation();
  const auto terminal = observation.value("state").toString();
  const QString size = QStringLiteral("%1 × %2").arg(receipt->workingMedia.width()).arg(receipt->workingMedia.height());
  // WFCAT-017: the body is the shared public observation (state + opaque output
  // references) that the log, progress and history surfaces carry verbatim.
  // Paths, clipboard/OCR payloads and remote error bodies stay out of notices;
  // only storage-review flags are appended.
  QString body = receipt->terminalText();
  if (!m_lastRegionDiagnostic.isEmpty()) body += "; Last-region storage needs review.";
  if (!receipt->diagnostic.isEmpty()) body += "; Capture result storage needs review.";
  if (!executionConfirmed) body += "; Workflow execution storage could not confirm the outcome. Check capture result history before retrying.";
  const auto title = result.state == xerahs::actions::PipelineState::Cancelled ? QStringLiteral("Capture actions cancelled · %1") :
      result.state == xerahs::actions::PipelineState::Interrupted ? QStringLiteral("Capture actions interrupted · %1") :
      terminal != "succeeded" ? QStringLiteral("Capture actions %2 · %1") :
      result.retry ? QStringLiteral("Capture actions retried · %1") : sourceTitle(QStringLiteral("captured · %1"));
  emit notify(title.arg(size, terminal), body, executionConfirmed && terminal == "succeeded" &&
      receipt->diagnostic.isEmpty() && m_lastRegionDiagnostic.isEmpty());
}

void CaptureController::closeOverlays() {
  for (auto &overlay : m_overlays) overlay->hide();
  m_overlays.clear();
}

void CaptureController::finish() {
  if (!m_busy) return;
  m_compositionTimer->stop();
  m_compositorPending = false;
  if (m_compositorWork) { m_compositorWork->cancel(); m_compositorWork.reset(); }
  if (m_composition) { m_composition->cancelled.store(true); m_composition.reset(); }
  m_reportedProgress = -1;
  m_pipelineActivity.clear();
  for (const auto &connection : m_screenConnections) disconnect(connection);
  m_screenConnections.clear();
  m_topologyTimer->stop();
  m_displays.clear(); m_topology.clear(); m_lastRectangle.reset();
  m_session.reset();
  m_regionSelected = {};
  m_hoverPoint.reset();
  m_desktop = QImage();
  m_captured.reset();
  m_selectedActions.clear();
  m_keyboardCursor.reset();
  m_keyboardSelecting = false;
  m_resizeCorner.reset();
  const auto earlyObservation = std::exchange(m_earlyObservation, {});
  m_execution.reset();
  m_captureLastRegionWriter = {}; m_lastRegionDiagnostic.clear();
  m_busy = false;
  const QPointer<CaptureController> guard(this);
  if (!earlyObservation.isEmpty()) presentCaptureObservation(earlyObservation);
  if (guard) emit busyChanged(false);
}

}  // namespace xerahs::app
