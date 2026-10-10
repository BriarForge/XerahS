#include "EditorCanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::editor;

namespace {
QTransform rotation(const RectangleAnnotation &r) {
  const QPointF centre{r.left() / 2 + r.right() / 2, r.top() / 2 + r.bottom() / 2};
  QTransform transform;
  transform.translate(centre.x(), centre.y());
  transform.rotate(r.style.rotationDegrees);
  transform.translate(-centre.x(), -centre.y());
  return transform;
}

QPen outlinePen(QColor color, Qt::PenStyle style = Qt::SolidLine) {
  QPen pen(color, 2, style);
  pen.setCosmetic(true);  // ES-019: fixed-width selection at every zoom
  return pen;
}
}  // namespace

EditorCanvas::EditorCanvas(EditorSession &session, const QImage &image, QWidget *parent)
    : QAbstractScrollArea(parent), m_session(session), m_image(image), m_view(image.size()) {
  setObjectName(QStringLiteral("editorCanvas"));
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(QStringLiteral("Image canvas"));
  setAccessibleDescription(QStringLiteral(
      "Drag to draw a rectangle. Arrow keys move selected objects. Control and wheel zoom; "
      "Space and drag or the middle mouse button pan. With no selection, arrow keys pan."));
  horizontalScrollBar()->setAccessibleName(QStringLiteral("Horizontal image position"));
  verticalScrollBar()->setAccessibleName(QStringLiteral("Vertical image position"));
  // Persistent scrollbars keep viewport dimensions stable while zooming and
  // provide keyboard/screen-reader access to pan at both zoom limits.
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_view.setViewSize(viewport()->size());
  m_view.fit();
  syncViewport();
}

void EditorCanvas::syncViewport() {
  const QPointF lo = m_view.minimumOffset(), hi = m_view.maximumOffset();
  const QSignalBlocker horizontal(horizontalScrollBar()), vertical(verticalScrollBar());
  horizontalScrollBar()->setRange(int(std::floor(-hi.x())), int(std::ceil(-lo.x())));
  verticalScrollBar()->setRange(int(std::floor(-hi.y())), int(std::ceil(-lo.y())));
  horizontalScrollBar()->setPageStep(viewport()->width());
  verticalScrollBar()->setPageStep(viewport()->height());
  horizontalScrollBar()->setValue(int(std::round(-m_view.offset().x())));
  verticalScrollBar()->setValue(int(std::round(-m_view.offset().y())));
  viewport()->update();
  emit viewportChanged(m_view.zoom());
}

void EditorCanvas::zoomIn() {
  cancelGesture();
  m_view.zoomBy(1.25, QPointF(viewport()->rect().center()));
  syncViewport();
}
void EditorCanvas::zoomOut() {
  cancelGesture();
  m_view.zoomBy(1 / 1.25, QPointF(viewport()->rect().center()));
  syncViewport();
}
void EditorCanvas::resetZoom() {
  cancelGesture();
  m_view.reset();
  syncViewport();
}
void EditorCanvas::zoomToFit() {
  cancelGesture();
  m_view.fit();
  syncViewport();
}

void EditorCanvas::setCropTool(bool active) {
  cancelGesture();
  m_cropTool = active;
  viewport()->setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
}

void EditorCanvas::sourceChanged() {
  cancelGesture();
  m_view.setImageSize(m_image.size());
  syncViewport();
}

void EditorCanvas::resizeEvent(QResizeEvent *event) {
  QAbstractScrollArea::resizeEvent(event);
  m_view.setViewSize(viewport()->size());
  syncViewport();
}

void EditorCanvas::scrollContentsBy(int, int) {
  cancelGesture();
  m_view.setOffset({-double(horizontalScrollBar()->value()), -double(verticalScrollBar()->value())});
  syncViewport();
}

void EditorCanvas::paintEvent(QPaintEvent *) {
  QPainter p(viewport());
  p.fillRect(viewport()->rect(), palette().dark());
  p.translate(m_view.offset());
  p.scale(m_view.zoom(), m_view.zoom());
  // Checkerboard exposes source alpha without changing source pixels.
  QPixmap tile(16, 16);
  tile.fill(Qt::white);
  { QPainter checker(&tile); checker.fillRect(0, 0, 8, 8, Qt::lightGray); checker.fillRect(8, 8, 8, 8, Qt::lightGray); }
  p.fillRect(QRectF(QPointF(0, 0), m_image.size()), QBrush(tile));
  p.drawImage(0, 0, m_image);
  p.setRenderHint(QPainter::Antialiasing);
  for (const Annotation &annotation : m_session.document().annotations) {
    const auto *r = std::get_if<RectangleAnnotation>(&annotation);
    if (!r || !r->visible) continue;
    const QRectF box(QPointF(r->left(), r->top()), QPointF(r->right(), r->bottom()));
    p.save();
    if (m_moving && m_session.selection().contains(r->id)) p.translate(m_moveLast - m_moveStart);
    p.setWorldTransform(rotation(*r), true);
    p.setOpacity(r->style.opacity);
    p.setBrush(QColor::fromRgba(r->style.fillColor));
    p.setPen(QPen(QColor::fromRgba(r->style.strokeColor), r->style.strokeWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.drawRect(box);
    p.restore();
    if (m_session.selection().contains(r->id)) {
      const double gap = r->style.strokeWidth / 2 + 3 / m_view.zoom();
      const QRectF outline = box.adjusted(-gap, -gap, gap, gap);
      p.save();
      if (m_moving) p.translate(m_moveLast - m_moveStart);
      p.setWorldTransform(rotation(*r), true);
      p.setBrush(Qt::NoBrush);
      p.setPen(outlinePen(Qt::white));
      p.drawRect(outline);
      p.setPen(outlinePen(palette().highlight().color(), Qt::DashLine));
      p.drawRect(outline);
      p.restore();
    }
  }
  if (m_drag) {
    p.setBrush(Qt::NoBrush);
    p.setPen(outlinePen(palette().highlight().color(), Qt::DashLine));
    p.drawRect(QRectF(m_drag->first, m_drag->second).normalized());
  }
  if (hasFocus()) {
    p.resetTransform();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(outlinePen(palette().highlight().color()));
    p.setBrush(Qt::NoBrush);
    p.drawRect(viewport()->rect().adjusted(1, 1, -1, -1));
  }
}

void EditorCanvas::mousePressEvent(QMouseEvent *event) {
  setFocus();
  if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
    cancelGesture();
    m_panning = true;
    m_panLast = event->position();
    m_panOriginalOffset = m_view.offset();
    viewport()->setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (event->button() != Qt::LeftButton) return;
  const QPointF at = m_view.toImage(event->position());
  if (m_cropTool) {
    m_drag = std::make_pair(at, at);
    viewport()->update();
    return;
  }
  if (const auto hit = topmostAt(at)) {
    const bool additive = event->modifiers() & Qt::ControlModifier;
    QList<QUuid> ids;
    if (additive) {
      for (const QUuid &id : m_session.selection()) ids << id;
      if (!ids.removeOne(*hit)) ids << *hit;
    } else if (!m_session.selection().contains(*hit)) {
      ids << *hit;
    } else {
      for (const QUuid &id : m_session.selection()) ids << id;
    }
    m_session.select(ids);
    m_moving = m_session.selection().contains(*hit);
    m_moveStart = m_moveLast = at;
  } else {
    if (!(event->modifiers() & Qt::ControlModifier)) m_session.select({});
    m_drag = std::make_pair(at, at);
  }
  viewport()->update();
  emit changed();
}

void EditorCanvas::mouseMoveEvent(QMouseEvent *event) {
  if (m_panning) {
    m_view.pan(event->position() - m_panLast);
    m_panLast = event->position();
    syncViewport();
  } else if (m_drag) {
    m_drag->second = m_view.toImage(event->position());
    viewport()->update();
  } else if (m_moving) {
    m_moveLast = m_view.toImage(event->position());
    viewport()->update();
  }
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent *event) {
  if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
    m_view.pan(event->position() - m_panLast);
    m_panning = false;
    viewport()->setCursor(m_spaceHeld ? Qt::OpenHandCursor : (m_cropTool ? Qt::CrossCursor : Qt::ArrowCursor));
    syncViewport();
    return;
  }
  if (event->button() != Qt::LeftButton) return;
  const QPointF at = m_view.toImage(event->position());
  if (m_drag) {
    const QPointF start = m_drag->first;
    m_drag.reset();
    if (m_cropTool) emit cropRequested(start, at);
    else m_session.createRectangle({start.x(), start.y()}, {at.x(), at.y()});
  } else if (m_moving) {
    m_moving = false;
    const QPointF delta = at - m_moveStart;
    m_session.moveSelection(delta.x(), delta.y());
  }
  viewport()->update();
  emit changed();
}

void EditorCanvas::cancelGesture() {
  m_drag.reset();
  m_moving = false;
  if (m_panning) m_view.setOffset(m_panOriginalOffset);
  m_panning = false;
  viewport()->setCursor(m_spaceHeld ? Qt::OpenHandCursor : (m_cropTool ? Qt::CrossCursor : Qt::ArrowCursor));
  viewport()->update();
}

void EditorCanvas::wheelEvent(QWheelEvent *event) {
  cancelGesture();
  if (event->modifiers() & Qt::ControlModifier) {
    const double steps = event->pixelDelta().isNull() ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 120.0;
    m_view.zoomBy(std::pow(1.25, std::clamp(steps, -8.0, 8.0)), event->position());
  } else {
    QPointF delta = event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 4 : QPointF(event->pixelDelta());
    if (event->modifiers() & Qt::ShiftModifier) delta = {delta.y(), delta.x()};
    m_view.pan(delta);
  }
  syncViewport();
  event->accept();
}

bool EditorCanvas::viewportEvent(QEvent *event) {
  if (event->type() == QEvent::NativeGesture) {
    auto *gesture = static_cast<QNativeGestureEvent *>(event);
    if (gesture->gestureType() == Qt::ZoomNativeGesture || gesture->gestureType() == Qt::PanNativeGesture) {
      cancelGesture();
      if (gesture->gestureType() == Qt::ZoomNativeGesture && std::isfinite(gesture->value()))
        m_view.zoomBy(std::exp(std::clamp(gesture->value(), -2.0, 2.0)), gesture->position());
      else if (gesture->gestureType() == Qt::PanNativeGesture) m_view.pan(gesture->delta());
      syncViewport();
      event->accept();
      return true;
    }
  }
  return QAbstractScrollArea::viewportEvent(event);
}

void EditorCanvas::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space) {
    m_spaceHeld = true;
    viewport()->setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    return;
  }
  if (event->key() == Qt::Key_Escape && (m_drag || m_moving || m_panning)) {
    cancelGesture();
    syncViewport();
    return;
  }
  const double step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
  if (m_session.selection().isEmpty()) {
    switch (event->key()) {
      case Qt::Key_Left: m_view.pan({25 * step, 0}); break;
      case Qt::Key_Right: m_view.pan({-25 * step, 0}); break;
      case Qt::Key_Up: m_view.pan({0, 25 * step}); break;
      case Qt::Key_Down: m_view.pan({0, -25 * step}); break;
      default: QAbstractScrollArea::keyPressEvent(event); return;
    }
    syncViewport();
    return;
  }
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_Left: m_session.resizeSelection(-step, 0); break;
      case Qt::Key_Right: m_session.resizeSelection(step, 0); break;
      case Qt::Key_Up: m_session.resizeSelection(0, -step); break;
      case Qt::Key_Down: m_session.resizeSelection(0, step); break;
      default: QAbstractScrollArea::keyPressEvent(event); return;
    }
  } else {
    const bool toEdge = event->modifiers() & Qt::ControlModifier;
    switch (event->key()) {
      case Qt::Key_R: m_session.rotateSelection((event->modifiers() & Qt::ShiftModifier) ? -90 : 90); break;
      case Qt::Key_BracketRight:
        m_session.reorderSelection(toEdge ? EditorSession::Order::Front : EditorSession::Order::Forward); break;
      case Qt::Key_BracketLeft:
        m_session.reorderSelection(toEdge ? EditorSession::Order::Back : EditorSession::Order::Backward); break;
      case Qt::Key_Left: m_session.moveSelection(-step, 0); break;
      case Qt::Key_Right: m_session.moveSelection(step, 0); break;
      case Qt::Key_Up: m_session.moveSelection(0, -step); break;
      case Qt::Key_Down: m_session.moveSelection(0, step); break;
      case Qt::Key_Delete:
      case Qt::Key_Backspace: m_session.deleteSelection(); break;
      default: QAbstractScrollArea::keyPressEvent(event); return;
    }
  }
  viewport()->update();
  emit changed();
}

void EditorCanvas::keyReleaseEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    m_spaceHeld = false;
    viewport()->setCursor(m_panning ? Qt::ClosedHandCursor : (m_cropTool ? Qt::CrossCursor : Qt::ArrowCursor));
    return;
  }
  QAbstractScrollArea::keyReleaseEvent(event);
}

void EditorCanvas::focusInEvent(QFocusEvent *event) { QAbstractScrollArea::focusInEvent(event); viewport()->update(); }
void EditorCanvas::focusOutEvent(QFocusEvent *event) {
  m_spaceHeld = false;
  cancelGesture();
  syncViewport();
  QAbstractScrollArea::focusOutEvent(event);
}

std::optional<QUuid> EditorCanvas::topmostAt(QPointF at) const {
  const auto &list = m_session.document().annotations;
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    const auto *r = std::get_if<RectangleAnnotation>(&*it);
    if (!r || !r->visible) continue;
    const double slop = std::max(6 / m_view.zoom(), r->style.strokeWidth / 2);
    const QRectF box(QPointF(r->left(), r->top()), QPointF(r->right(), r->bottom()));
    if (box.adjusted(-slop, -slop, slop, slop).contains(rotation(*r).inverted().map(at))) return r->id;
  }
  return std::nullopt;
}

}  // namespace xerahs::app
