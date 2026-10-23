#include "CaptureController.h"

#include "CaptureActions.h"
#include "PortalScreenshot.h"
#include "RegionOverlay.h"

#include "actions/PostCapturePipeline.h"

#include <QColorSpace>
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
}

CaptureController::~CaptureController() {
  m_compositionTimer->stop();
  if (m_pipeline) { disconnect(m_pipeline, nullptr, this, nullptr); m_pipeline->cancel(); }
  if (m_composition) { m_composition->cancelled.store(true); m_composition.reset(); }
  if (m_portal) {
    auto *portal = m_portal.data(); disconnect(portal, nullptr, this, nullptr);
    portal->setParent(nullptr); portal->cancel(); portal->retire(); m_portal = nullptr;
  }
  closeOverlays();
}

QString CaptureController::instructions() {
  return QStringLiteral(
      "Region capture. Drag with the pointer, or press Space to start and finish a selection at the keyboard "
      "cursor. Arrow keys move by 1 pixel, Shift+Arrow by 10. With a selection, Alt+Arrow resizes. Enter "
      "captures. Escape cancels.");
}

void CaptureController::captureRegion() {
  if (m_workflowResolver) { beginConfiguredCapture(false); return; }
  if (m_busy) return;
  m_workflowSnapshot = {};
  m_captureOutput = {};
  m_source = QStringLiteral("region");
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
    if (!execution->start()) { emit guard->notify("Capture not started", "Workflow execution storage is unavailable. No capture was started.", false); return; }
    guard->m_execution = execution;
    guard->captureWorkflow(effective);
  };
}
void CaptureController::recordCaptureOutcome(const QString &state, const QString &diagnostic) {
  if (m_execution && !m_execution->finish(state, diagnostic))
    emit notify("Execution result not recorded", "Workflow storage could not confirm the outcome. Check capture result history before retrying.", false);
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
void CaptureController::beginCapture(QStringList selected) {
  if (m_busy) return;
  m_selectedActions = std::move(selected);
  ++m_generation;
  m_pendingPipelineCancellation = false;
  m_busy = true;
  m_portal = m_screenshotFactory ? m_screenshotFactory(this) : new PortalScreenshot(this);
  if (!m_portal) {
    const QPointer<CaptureController> guard(this);
    emit busyChanged(true);
    if (!guard) return;
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed); if (!guard) return;
    emit notify(sourceTitle("capture failed"), diagnostic::captureSourceFailed, false); if (guard) finish(); return;
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

void CaptureController::onSnapshot(int outcome, const QImage &image, const QString &detail) {
  const QPointer<CaptureController> guard(this);
  const auto result = static_cast<PortalScreenshot::Outcome>(outcome);
  if (result == PortalScreenshot::Outcome::Cancelled) { recordCaptureOutcome("cancelled"); if (guard) finish(); return; }
  if (result == PortalScreenshot::Outcome::Failed) {
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed);
    if (!guard) return;
    emit notify(sourceTitle("capture failed"),
                detail.isEmpty() ? diagnostic::captureSourceFailed : detail, false);
    if (guard) finish();
    return;
  }

  // RC-003: snapshot the topology for this session. The portal image covers
  // the whole layout; its pixels define desktop physical space.
  Topology topology;
  const QList<QScreen *> screens = QGuiApplication::screens();
  QRect bounds;
  for (QScreen *screen : screens) bounds = bounds.united(screen->geometry());
  const double scale = image.isNull() || bounds.width() <= 0 ? 1.0 : double(image.width()) / bounds.width();
  for (QScreen *screen : screens) {
    const QRect g = screen->geometry();
    const auto physical = [&](int logical, int origin) {
      return static_cast<qint64>(std::llround((logical - origin) * scale));
    };
    topology.push_back(Display{screen->name(),
                               {physical(g.left(), bounds.left()), physical(g.top(), bounds.top()),
                                physical(g.left() + g.width(), bounds.left()), physical(g.top() + g.height(), bounds.top())},
                               {double(g.left()), double(g.top()), double(g.left() + g.width()), double(g.top() + g.height())},
                               scale});
  }

  if (m_source != QStringLiteral("region")) {
    if (result == PortalScreenshot::Outcome::Denied) {
      recordCaptureOutcome("permission-denied", diagnostic::capturePermissionDenied); if (!guard) return;
      emit notify(sourceTitle("capture denied"), diagnostic::capturePermissionDenied, false);
      if (guard) finish();
      return;
    }
    m_desktop = image;
    captureWholeSource(topology);
    return;
  }

  m_session = std::make_unique<RegionSession>(topology, SessionSettings{}, std::vector<SnapTarget>{},
                                              [this](const SessionOutcome &o) { onOutcome(o); });
  m_session->start();
  if (result == PortalScreenshot::Outcome::Denied) {
    m_session->permission(Permission::Denied);  // RC-002: no overlay, no re-prompt
    return;
  }
  // Pending clarification (ROOT-ESCALATE-001): with one portal image for the
  // whole layout, the engine assumes one uniform scale. A snapshot that does
  // not match the layout is a capture-source failure, not a guess.
  if (std::abs(image.height() - bounds.height() * scale) > 2.0) {
    m_session.reset();
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed);
    if (!guard) return;
    emit notify(sourceTitle("capture failed"),
                QStringLiteral("%1: snapshot does not match the display layout").arg(diagnostic::captureSourceFailed),
                false);
    if (guard) finish();
    return;
  }
  m_desktop = image; // immutable source; pixel preparation happens on the worker
  m_session->permission(Permission::Authorized);

  for (QScreen *screen : screens) {
    const Display *display = findDisplay(topology, screen->name());
    const PhysicalRect &p = display->physical;
    const QRect source(int(p.left), int(p.top), int(p.width()), int(p.height()));
    m_overlays.push_back(std::make_unique<RegionOverlay>(*this, screen, *display, m_desktop, source));
  }
  // RC-003: a topology change during selection cancels.
  const auto changed = [this] {
    if (m_session) {
      if (m_composition) m_composition->cancelled.store(true);
      m_session->topologyChanged();
      afterInput();
    }
  };
  m_screenConnections.push_back(connect(qGuiApp, &QGuiApplication::screenAdded, this, changed));
  m_screenConnections.push_back(connect(qGuiApp, &QGuiApplication::screenRemoved, this, changed));
  for (QScreen *screen : screens) {
    m_screenConnections.push_back(connect(screen, &QScreen::geometryChanged, this, changed));
    m_screenConnections.push_back(connect(screen, &QScreen::orientationChanged, this, changed));
    m_screenConnections.push_back(connect(screen, &QScreen::physicalDotsPerInchChanged, this, changed));
    m_screenConnections.push_back(connect(screen, &QScreen::logicalDotsPerInchChanged, this, changed));
  }

  for (auto &overlay : m_overlays) overlay->showFullScreen();
  m_session->ready();
  if (!m_overlays.empty()) {
    m_overlays.front()->activateWindow();
    m_overlays.front()->setFocus();
  }
}

QString CaptureController::sourceTitle(const QString &suffix) const {
  const auto label = xerahs::workflows::captureSourceLabel(m_source);
  return (label.isEmpty() ? QStringLiteral("Capture") : label) + QStringLiteral(" ") + suffix;
}

// CORE-014: full screen takes the whole portal image; monitor takes the
// display under the pointer, falling back to the primary display when the
// compositor does not report a pointer position. No overlay is shown.
void CaptureController::captureWholeSource(const Topology &topology) {
  const QPointer<CaptureController> guard(this);
  const auto fail = [&](const QString &detail) {
    recordCaptureOutcome("failed", diagnostic::captureSourceFailed); if (!guard) return;
    emit notify(sourceTitle("capture failed"), detail, false);
    if (guard) finish();
  };
  QRect source = m_desktop.rect();
  if (m_source == QStringLiteral("monitor")) {
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const Display *display = screen ? findDisplay(topology, screen->name()) : nullptr;
    if (!display) { fail(diagnostic::captureSourceFailed); return; }
    const PhysicalRect &p = display->physical;
    source = QRect(int(p.left), int(p.top), int(p.width()), int(p.height())).intersected(m_desktop.rect());
  }
  if (m_desktop.isNull() || source.isEmpty()) { fail(diagnostic::captureSourceFailed); return; }
  QImage pixels = m_desktop.copy(source).convertToFormat(QImage::Format_ARGB32);
  if (pixels.isNull()) { fail(diagnostic::captureSourceFailed); return; }
  pixels.setColorSpace(m_desktop.colorSpace());
  m_captured = std::move(pixels);
  runPipeline();
}

void CaptureController::pointerDown(PhysicalPoint point) {
  if (!m_session) return;
  m_keyboardCursor.reset();
  m_keyboardSelecting = false;
  m_session->pointerDown(point);
  afterInput();
}

void CaptureController::pointerMove(PhysicalPoint point) {
  if (!m_session || m_session->state() != SessionState::Selecting || m_keyboardSelecting) return;
  m_session->pointerMove(point);
  afterInput();
}

void CaptureController::pointerUp(PhysicalPoint point) {
  if (!m_session || m_keyboardSelecting) return;
  m_session->pointerUp(point);
  afterInput();
}

void CaptureController::cancel() {
  if (m_pipeline) { m_pipeline->cancel(); return; }
  if (m_session && m_session->state() == SessionState::Completed) m_pendingPipelineCancellation = true;
  if (m_composition) m_composition->cancelled.store(true);
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
  if (m_composition) return; // repeated input never starts another acquisition
  const std::optional<PhysicalRect> rect = m_session->selection();
  if (!rect) {
    m_session->captureFailed();
    return;
  }
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
    if (outcome.state == SessionState::Completed && outcome.rectangle && m_captured) {
      runPipeline();
      return; // cleanup follows the actual pipeline receipt
    }
    recordCaptureOutcome(outcome.diagnostic == diagnostic::capturePermissionDenied ? QStringLiteral("permission-denied") :
        outcome.state == SessionState::Cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"), outcome.diagnostic.value_or(QString{}));
    if (!guard) return;
    if (outcome.diagnostic) {
      const QString title = sourceTitle(outcome.state == SessionState::Failed ? QStringLiteral("capture failed") : QStringLiteral("capture cancelled"));
      emit notify(title, *outcome.diagnostic, false);
    }
    if (!guard) return;
    // RC-008: a plain cancel shows nothing and starts no actions.
    closeOverlays();
    finish();
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
    emit notify("Capture actions not started", "Workflow execution storage is unavailable. No post-capture action was started.", false);
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

  QStringList done;
  QStringList failed;
  for (const auto &action : result.orderedResults) {
    const auto label = CaptureActions::actionLabel(action.action);
    if (action.state == xerahs::actions::ActionState::Succeeded) {
      if (action.action == "save" || action.action == "save-as") {
        for (const auto &file : receipt->savedFiles()) if (file.artifact == action.action + ".file")
          done.append(QStringLiteral("Saved %1").arg(QFileInfo(file.path).fileName()));
      } else if (action.action == QStringLiteral("image-clipboard")) {
        done.append(QStringLiteral("copied to clipboard"));
      } else if (action.action == "file-clipboard") done.append(QStringLiteral("copied image file"));
      else if (action.action == "path-clipboard") done.append(QStringLiteral("copied file path"));
      else if (action.action == "reveal") done.append(QStringLiteral("file manager acknowledged the show request"));
      if (action.cancellationArrivedDuring) done.append(QStringLiteral("%1 finished after cancellation was requested").arg(label));
    } else {
      if (xerahs::actions::hasAcknowledgedPartialRaster(action.action, action.state, action.artifacts)) {
        for (const auto &file : receipt->savedFiles()) if (file.artifact == action.action + ".file")
          done.append(QStringLiteral("PNG saved as %1; editable companion failed and document is retained").arg(QFileInfo(file.path).fileName()));
      }
      auto description = label + QStringLiteral(" ") + xerahs::actions::actionStateName(action.state);
      if (action.diagnostic) description += QStringLiteral(" (%1)").arg(*action.diagnostic);
      failed.append(description);
    }
  }
  const QString size = QStringLiteral("%1 × %2").arg(receipt->workingMedia.width()).arg(receipt->workingMedia.height());
  QString body = done.join(QStringLiteral(", "));
  if (body.isEmpty() && failed.isEmpty() && !receipt->file) body = QStringLiteral("Captured image kept in memory.");
  if (!failed.isEmpty()) body += (body.isEmpty() ? QString() : QStringLiteral("; ")) + failed.join(QStringLiteral("; "));
  if (!receipt->diagnostic.isEmpty()) body += QStringLiteral("; ") + receipt->diagnostic;
  if (!executionConfirmed) body += QStringLiteral("; Workflow execution storage could not confirm the outcome. Completed outputs remain in capture result history; check before retrying.");
  const auto title = result.state == xerahs::actions::PipelineState::Cancelled ? QStringLiteral("Capture actions cancelled · %1") :
      result.state == xerahs::actions::PipelineState::Interrupted ? QStringLiteral("Capture actions interrupted · %1") :
      result.retry ? QStringLiteral("Capture actions retried · %1") : sourceTitle(QStringLiteral("captured · %1"));
  emit notify(title.arg(size), body, executionConfirmed && failed.isEmpty() && receipt->diagnostic.isEmpty());
}

void CaptureController::closeOverlays() {
  for (auto &overlay : m_overlays) overlay->hide();
  m_overlays.clear();
}

void CaptureController::finish() {
  if (!m_busy) return;
  m_compositionTimer->stop();
  if (m_composition) { m_composition->cancelled.store(true); m_composition.reset(); }
  m_reportedProgress = -1;
  m_pipelineActivity.clear();
  for (const auto &connection : m_screenConnections) disconnect(connection);
  m_screenConnections.clear();
  m_session.reset();
  m_desktop = QImage();
  m_captured.reset();
  m_selectedActions.clear();
  m_keyboardCursor.reset();
  m_keyboardSelecting = false;
  m_execution.reset();
  m_busy = false;
  emit busyChanged(false);
}

}  // namespace xerahs::app
