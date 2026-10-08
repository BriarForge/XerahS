#include "CaptureController.h"

#include "CaptureActions.h"
#include "PortalScreenshot.h"
#include "RegionOverlay.h"

#include "actions/PostCapturePipeline.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QUuid>

#include <cmath>
#include <cstring>

namespace xerahs::app {

using namespace xerahs::capture;

namespace {

// FILENAME-GENERATION-001 default capture pattern.
const QString kCapturePattern = QStringLiteral("%y%mo%dT%h%mi_%ra{10}");

// Pending clarification (ROOT-ESCALATE-001): no settings store exists yet, so
// region capture runs this fixed workflow instead of a configured one.
const QStringList kDefaultActions = {QStringLiteral("save"), QStringLiteral("image-clipboard")};

}  // namespace

CaptureController::CaptureController(QString saveDirectory, QObject *parent)
    : QObject(parent), m_saveDirectory(std::move(saveDirectory)) {}

CaptureController::~CaptureController() { closeOverlays(); }

QString CaptureController::instructions() {
  return QStringLiteral(
      "Region capture. Drag with the pointer, or press Space to start and finish a selection at the keyboard "
      "cursor. Arrow keys move by 1 pixel, Shift+Arrow by 10. With a selection, Alt+Arrow resizes. Enter "
      "captures. Escape cancels.");
}

void CaptureController::captureRegion() {
  if (m_busy) return;
  m_busy = true;
  emit busyChanged(true);
  m_portal = new PortalScreenshot(this);
  connect(m_portal, &PortalScreenshot::finished, this,
          [this](PortalScreenshot::Outcome outcome, const QImage &image, const QString &detail) {
            m_portal->deleteLater();
            onSnapshot(static_cast<int>(outcome), image, detail);
          });
  m_portal->request();
}

void CaptureController::onSnapshot(int outcome, const QImage &image, const QString &detail) {
  const auto result = static_cast<PortalScreenshot::Outcome>(outcome);
  if (result == PortalScreenshot::Outcome::Failed) {
    emit notify(QStringLiteral("Region capture failed"),
                QStringLiteral("%1: %2").arg(diagnostic::captureSourceFailed, detail), false);
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
  m_desktop = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
  m_session->permission(Permission::Authorized);

  for (QScreen *screen : screens) {
    const Display *display = findDisplay(topology, screen->name());
    const PhysicalRect &p = display->physical;
    QImage crop = m_desktop.copy(QRect(int(p.left), int(p.top), int(p.width()), int(p.height())));
    m_overlays.push_back(std::make_unique<RegionOverlay>(*this, screen, *display, std::move(crop)));
  }
  // RC-003: a topology change during selection cancels.
  const auto changed = [this] {
    if (m_session) {
      m_session->topologyChanged();
      afterInput();
    }
  };
  m_screenConnections.push_back(connect(qGuiApp, &QGuiApplication::screenAdded, this, changed));
  m_screenConnections.push_back(connect(qGuiApp, &QGuiApplication::screenRemoved, this, changed));
  for (QScreen *screen : screens) {
    m_screenConnections.push_back(connect(screen, &QScreen::geometryChanged, this, changed));
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
      m_session->keyEscape();
      afterInput();
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
  if (m_session && m_session->state() == SessionState::Confirming) acquire();
  repaint();
}

void CaptureController::repaint() {
  for (auto &overlay : m_overlays) overlay->update();
}

void CaptureController::acquire() {
  const std::optional<PhysicalRect> rect = m_session->selection();
  if (!rect) {
    m_session->captureFailed();
    return;
  }
  // RC-012: composite from the frozen snapshot; the overlay never appears in
  // these pixels (RC-013).
  const CompositeResult result = composite(*rect, m_session->topology(), [this](const Display &, const PhysicalRect &s) {
    const QImage region = m_desktop.copy(QRect(int(s.left), int(s.top), int(s.width()), int(s.height())));
    if (region.width() != s.width() || region.height() != s.height()) return std::optional<RgbaImage>();
    RgbaImage image{s.width(), s.height(), QByteArray(static_cast<qsizetype>(s.width() * s.height() * 4), '\0')};
    for (int y = 0; y < region.height(); ++y) {
      std::memcpy(image.pixels.data() + qsizetype(y) * s.width() * 4, region.constScanLine(y),
                  static_cast<std::size_t>(s.width() * 4));
    }
    return std::optional<RgbaImage>(std::move(image));
  });
  if (!result.image) {
    m_session->captureFailed();
    return;
  }
  m_captured = QImage(reinterpret_cast<const uchar *>(result.image->pixels.constData()), int(result.image->width),
                      int(result.image->height), int(result.image->width * 4), QImage::Format_RGBA8888_Premultiplied)
                   .convertToFormat(QImage::Format_ARGB32);
  m_session->captureSucceeded();
}

void CaptureController::onOutcome(const SessionOutcome &outcome) {
  // Published from inside a RegionSession call; finish on the next turn.
  QTimer::singleShot(0, this, [this, outcome] {
    if (outcome.state == SessionState::Completed && outcome.rectangle && m_captured) {
      runPipeline(*outcome.rectangle);
    } else if (outcome.diagnostic) {
      const QString title = outcome.state == SessionState::Failed ? QStringLiteral("Region capture failed")
                                                                  : QStringLiteral("Region capture cancelled");
      emit notify(title, *outcome.diagnostic, false);
    }
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
