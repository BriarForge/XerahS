#include "CaptureController.h"

#include "CaptureActions.h"
#include "PortalScreenshot.h"
#include "RegionOverlay.h"

#include "actions/PostCapturePipeline.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QUuid>

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

// FILENAME-GENERATION-001 default capture pattern.
const QString kCapturePattern = QStringLiteral("%y%mo%dT%h%mi_%ra{10}");

// Pending clarification (ROOT-ESCALATE-001): no settings store exists yet, so
// region capture runs this fixed workflow instead of a configured one.
const QStringList kDefaultActions = {QStringLiteral("save"), QStringLiteral("image-clipboard")};

}  // namespace

CaptureController::CaptureController(QString saveDirectory, QObject *parent, ScreenshotFactory factory, SnapshotComposer composer)
    : QObject(parent), m_saveDirectory(std::move(saveDirectory)), m_screenshotFactory(std::move(factory)), m_composer(std::move(composer)) {
  m_compositionTimer = new QTimer(this); m_compositionTimer->setInterval(10);
  connect(m_compositionTimer, &QTimer::timeout, this, &CaptureController::pollComposition);
}

CaptureController::~CaptureController() {
  m_compositionTimer->stop();
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
  if (m_busy) return;
  ++m_generation;
  m_busy = true;
  m_portal = m_screenshotFactory ? m_screenshotFactory(this) : new PortalScreenshot(this);
  if (!m_portal) {
    emit busyChanged(true);
    emit notify(QStringLiteral("Region capture failed"), diagnostic::captureSourceFailed, false); finish(); return;
  }
  auto *portal = m_portal.data();
  connect(portal, &PortalScreenshot::finished, this,
          [this, portal](PortalScreenshot::Outcome outcome, const QImage &image, const QString &detail) {
            // Queued old-request callbacks cannot finish a newer capture.
            if (!m_busy || m_portal != portal) return;
            m_portal = nullptr; portal->setParent(nullptr); portal->retire();
            onSnapshot(static_cast<int>(outcome), image, detail);
          });
  emit busyChanged(true);
  // Availability listeners may synchronously cancel before acquisition starts.
  if (m_busy && m_portal == portal) portal->request();
}

void CaptureController::onSnapshot(int outcome, const QImage &image, const QString &detail) {
  const auto result = static_cast<PortalScreenshot::Outcome>(outcome);
  if (result == PortalScreenshot::Outcome::Cancelled) { finish(); return; }
  if (result == PortalScreenshot::Outcome::Failed) {
    emit notify(QStringLiteral("Region capture failed"),
                detail.isEmpty() ? diagnostic::captureSourceFailed : detail, false);
    finish();
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
    emit notify(QStringLiteral("Region capture failed"),
                QStringLiteral("%1: snapshot does not match the display layout").arg(diagnostic::captureSourceFailed),
                false);
    finish();
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
  if (m_composition) m_composition->cancelled.store(true);
  if (m_portal) { m_portal->cancel(); return; }
  if (!m_session) return;
  m_session->cancel();
  afterInput();
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
      runPipeline(*outcome.rectangle);
    } else if (outcome.diagnostic) {
      const QString title = outcome.state == SessionState::Failed ? QStringLiteral("Region capture failed")
                                                                  : QStringLiteral("Region capture cancelled");
      emit notify(title, *outcome.diagnostic, false);
    }
    if (!guard) return;
    // RC-008: a plain cancel shows nothing and starts no actions.
    closeOverlays();
    finish();
  });
}

void CaptureController::runPipeline(const PhysicalRect &rect) {
  // Runs while the overlay still has focus, so the clipboard has a valid
  // Wayland input serial.
  CaptureActions actions(*m_captured, m_saveDirectory, kCapturePattern);
  xerahs::actions::CancellationToken token;
  const xerahs::actions::PipelineResult result =
      xerahs::actions::runPipeline(QUuid::createUuid().toString(QUuid::WithoutBraces), kDefaultActions, {}, actions, token);

  QStringList done;
  QStringList failed;
  for (const auto &action : result.orderedResults) {
    if (action.state == xerahs::actions::ActionState::Succeeded) {
      if (action.action == QStringLiteral("save") && actions.savedPath()) {
        done.append(QStringLiteral("Saved %1").arg(QFileInfo(*actions.savedPath()).fileName()));
      } else if (action.action == QStringLiteral("image-clipboard")) {
        done.append(QStringLiteral("copied to clipboard"));
      }
    } else {
      failed.append(QStringLiteral("%1 %2 (%3)")
                        .arg(action.action, xerahs::actions::actionStateName(action.state),
                             action.diagnostic.value_or(QString())));
    }
  }
  const QString size = QStringLiteral("%1 × %2").arg(rect.width()).arg(rect.height());
  QString body = done.join(QStringLiteral(", "));
  if (!failed.isEmpty()) body += (body.isEmpty() ? QString() : QStringLiteral("; ")) + failed.join(QStringLiteral("; "));
  emit notify(QStringLiteral("Region captured · %1").arg(size), body, failed.isEmpty());
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
  for (const auto &connection : m_screenConnections) disconnect(connection);
  m_screenConnections.clear();
  m_session.reset();
  m_desktop = QImage();
  m_captured.reset();
  m_keyboardCursor.reset();
  m_keyboardSelecting = false;
  m_busy = false;
  emit busyChanged(false);
}

}  // namespace xerahs::app
