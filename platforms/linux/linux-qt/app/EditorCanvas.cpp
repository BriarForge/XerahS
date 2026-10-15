#include "EditorCanvas.h"
#include "RectanglePropertiesDialog.h"
#include "SelectionRotationDialog.h"
#include "AnnotationColorDialog.h"

#include <QColorSpace>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointer>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::editor;

namespace {
QImage displayCopy(const QImage &source) {
  return !source.colorSpace().isValid() || source.colorSpace() == QColorSpace(QColorSpace::SRgb) ? source
      : source.convertedToColorSpace(QColorSpace::SRgb);
}
QTransform rotation(const RectangleAnnotation &r) {
  const QPointF centre{r.left() / 2 + r.right() / 2, r.top() / 2 + r.bottom() / 2};
  QTransform transform;
  transform.translate(centre.x(), centre.y());
  transform.rotate(std::fmod(r.style.rotationDegrees, 360.0));
  transform.translate(-centre.x(), -centre.y());
  return transform;
}

QPen outlinePen(QColor color, Qt::PenStyle style = Qt::SolidLine) {
  QPen pen(color, 2, style);
  pen.setCosmetic(true);  // ES-019: fixed-width selection at every zoom
  return pen;
}
Qt::CursorShape resizeCursor(RectangleHandle handle, double rotation) {
  double base = 0;
  if (handle == RectangleHandle::Top || handle == RectangleHandle::Bottom) base = 90;
  else if (handle == RectangleHandle::TopLeft || handle == RectangleHandle::BottomRight) base = 45;
  else if (handle == RectangleHandle::TopRight || handle == RectangleHandle::BottomLeft) base = -45;
  double angle = std::fmod(std::fmod(rotation, 180.0) + base, 180.0); if (angle < 0) angle += 180;
  const std::array<Qt::CursorShape, 4> cursors{{Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor}};
  return cursors[int(std::floor((angle + 22.5) / 45)) % 4];
}
}  // namespace

EditorCanvas::EditorCanvas(EditorSession &session, const QImage &image, QWidget *parent, const QImage &displaySource)
    : QAbstractScrollArea(parent), m_session(&session), m_image(image),
      m_displaySource(displaySource.isNull() ? displayCopy(image) : displaySource), m_view(image.size()) {
  setObjectName(QStringLiteral("editorCanvas"));
  setFocusPolicy(Qt::StrongFocus);
  viewport()->setMouseTracking(true);
  setAccessibleName(QStringLiteral("Image canvas"));
  setAccessibleDescription(toolInstructions());
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
  setTool(active ? Tool::Crop : Tool::Rectangle);
}

QString EditorCanvas::toolInstructions() const {
  QString text;
  switch (m_tool) {
    case Tool::Select:
      text = "Select: click an object; Control-click adds or removes it. Drag selected objects to move. Blank canvas clears selection."; break;
    case Tool::Rectangle:
      text = "Rectangle stays active: drag on blank canvas to draw. Click an existing object to select or move it. Out-of-canvas bounds are allowed; export clips to the image."; break;
    case Tool::Crop:
      text = "Crop stays active: drag to choose the retained image region."; break;
  }
  return text + " Create Rectangle offers keyboard entry of bounds with captured creation defaults. Drag a selected rectangle's square handles to resize; the opposite handle stays fixed. Rectangle Properties offers numeric bounds and style controls. "
      "Escape cancels a gesture. Arrows move one image pixel; Shift+arrows move ten. Alt+arrows move the bottom-right corner one pixel; Alt+Shift+arrows move it ten. R rotates clockwise 90 degrees; Shift+R rotates counterclockwise. Held transform keys repeat. Delete removes selected objects. Home/End bring to front/send to back; Page Up/Down bring forward/send backward. Control+wheel zooms; Space+drag pans.";
}

void EditorCanvas::setTool(Tool tool) {
  if (!isEnabled() || m_annotationDialogPending) { emit toolChanged(); return; }
  cancelGesture();
  m_tool = tool;
  setAccessibleDescription(toolInstructions());
  viewport()->setCursor(m_tool == Tool::Crop ? Qt::CrossCursor : Qt::ArrowCursor);
  emit toolChanged();
}

bool EditorCanvas::canEditRectangle() const {
  return m_session->selectedRectangle() && m_session->selectionEditable();
}
bool EditorCanvas::selectAnnotations(const QList<QUuid> &ids, quint64 state, quint64 generation) {
  if (!isEnabled() || interactionActive()) return false;
  const QPointer<EditorCanvas> owner(this);
  if (generation != m_documentGeneration) { emit editRejected("annotation-edit-stale"); return false; }
  const bool changed = m_session->select(ids, state);
  viewport()->update();
  if (!m_session->editDiagnostic().isEmpty()) { emit editRejected(m_session->editDiagnostic()); if (!owner) return false; }
  if (changed) emit this->changed();
  return changed;
}
bool EditorCanvas::editRectangleProperties(QWidget *parent) {
  return rectangleDialog(parent, false);
}
bool EditorCanvas::createRectangle(QWidget *parent) {
  return rectangleDialog(parent, true);
}
bool EditorCanvas::rotateSelection(QWidget *parent) {
  if (!isEnabled() || interactionActive() || !canRotateSelection()) return false;
  AnnotationDocument document; QSet<QUuid> selection;
  try { document = m_session->document(); selection = m_session->selection(); }
  catch (const std::bad_alloc &) { emit editRejected("annotation-edit-failed"); return false; }
  const auto state = m_session->stateId(), generation = m_documentGeneration;
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  const auto choice = chooseSelectionRotation(document, selection, parent); if (!owner) return false;
  QString diagnostic = choice.diagnostic; bool changed = false;
  if (choice.degrees) {
    if (generation != m_documentGeneration) diagnostic = "annotation-edit-stale";
    else { changed = m_session->rotateSelection(*choice.degrees, state, selection); diagnostic = m_session->editDiagnostic(); }
  }
  viewport()->update(); if (!diagnostic.isEmpty()) { emit editRejected(diagnostic); if (!owner) return false; }
  return changed;
}
bool EditorCanvas::rectangleDialog(QWidget *parent, bool creation) {
  if (!isEnabled() || interactionActive() || (!creation && !canEditRectangle())) return false;
  const auto snapshot = creation ? RectangleAnnotation{} : *m_session->selectedRectangle();
  const auto style = m_session->toolStyle(); const auto state = m_session->stateId(), generation = m_documentGeneration;
  const auto selection = m_session->selection(); const QSize imageSize = m_displaySource.size();
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  const auto properties = creation ? chooseRectangleCreation(style, imageSize, parent) : chooseRectangleProperties(snapshot, parent);
  if (!owner || !properties) return false;
  if (generation != m_documentGeneration) { emit editRejected("annotation-edit-stale"); return false; }
  const bool changed = creation ? bool(m_session->createRectangle(*properties, state, selection))
      : m_session->editRectangleProperties(snapshot.id, *properties, state);
  viewport()->update();
  if (!m_session->editDiagnostic().isEmpty()) { emit editRejected(m_session->editDiagnostic()); if (!owner) return false; }
  return changed;
}

bool EditorCanvas::deleteSelection() {
  if (!isEnabled() || interactionActive() || !canDeleteSelection()) return false;
  const auto state = m_session->stateId(), generation = m_documentGeneration; const auto selection = m_session->selection();
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  bool changed = false; QString diagnostic;
  if (generation != m_documentGeneration) diagnostic = "annotation-edit-stale";
  else { changed = m_session->deleteSelection(state, selection); diagnostic = m_session->editDiagnostic(); }
  viewport()->update(); if (!diagnostic.isEmpty()) { emit editRejected(diagnostic); if (!owner) return false; }
  return changed;
}
bool EditorCanvas::transformSelection(SelectionTransform transform, double x, double y) {
  if (!isEnabled() || interactionActive()) return false;
  const auto state = m_session->stateId(), generation = m_documentGeneration;
  QSet<QUuid> selection;
  try { selection = m_session->selection(); }
  catch (const std::bad_alloc &) { emit editRejected("annotation-edit-failed"); return false; }
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  bool changed = false; QString diagnostic;
  if (generation != m_documentGeneration || !isEnabled()) diagnostic = "annotation-edit-stale";
  else {
    switch (transform) {
      case SelectionTransform::Move: changed = m_session->moveSelection(x, y, state, selection); break;
      case SelectionTransform::Resize: changed = m_session->resizeSelection(x, y, state, selection); break;
      case SelectionTransform::Rotate: changed = m_session->rotateSelection(x, state, selection); break;
    }
    diagnostic = m_session->editDiagnostic();
  }
  viewport()->update(); if (!diagnostic.isEmpty()) { emit editRejected(diagnostic); if (!owner) return false; }
  return changed;
}
bool EditorCanvas::reorderSelection(EditorSession::Order order) {
  if (!isEnabled() || interactionActive() || !canOrderSelection()) return false;
  const auto state = m_session->stateId(), generation = m_documentGeneration; const auto selection = m_session->selection();
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  bool changed = false; QString diagnostic;
  if (generation != m_documentGeneration) diagnostic = "annotation-edit-stale";
  else { changed = m_session->reorderSelection(order, state, selection); diagnostic = m_session->editDiagnostic(); }
  viewport()->update(); if (!diagnostic.isEmpty()) { emit editRejected(diagnostic); if (!owner) return false; }
  return changed;
}
bool EditorCanvas::changeStrokeWidth(bool increase) {
  if (!isEnabled() || interactionActive() || !canEditStyle()) return false;
  const auto state = m_session->stateId(), generation = m_documentGeneration; const auto selection = m_session->selection();
  const auto defaults = m_session->toolStyle();
  const double width = increase ? std::min(10000.0, defaults.strokeWidth + 1) : std::max(1.0, defaults.strokeWidth - 1);
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  QString diagnostic; bool changed = false;
  if (generation != m_documentGeneration) diagnostic = "annotation-edit-stale";
  else { const auto edit = m_session->editStrokeWidth(width, state, selection, defaults); changed = edit.documentChanged; diagnostic = edit.diagnostic; }
  viewport()->update(); if (!diagnostic.isEmpty()) { emit editRejected(diagnostic); if (!owner) return false; }
  return changed;
}
bool EditorCanvas::editColor(EditorSession::ColorTarget target, QWidget *parent) {
  if (!isEnabled() || interactionActive() || !canEditColor()) return false;
  const auto state = m_session->stateId(), generation = m_documentGeneration; const auto selection = m_session->selection();
  const auto defaults = m_session->toolStyle(); const auto initial = target == EditorSession::ColorTarget::Stroke ? defaults.strokeColor : defaults.fillColor;
  const QString scope = selection.isEmpty() ? "This colour changes only the default for new rectangles in this capture. Current objects and history stay unchanged."
      : "Selected rectangles change in one undo step. This colour also becomes the default for new rectangles in this capture. Unsupported objects stay unchanged.";
  const QPointer<EditorCanvas> owner(this); m_annotationDialogPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_annotationDialogPending = false; emit owner->changed(); } });
  emit changed(); if (!owner) return false;
  const auto choice = chooseAnnotationColor(target, initial, scope, parent); if (!owner) return false;
  QString diagnostic = choice.diagnostic; bool changed = false;
  if (choice.color) {
    if (generation != m_documentGeneration) diagnostic = "annotation-edit-stale";
    else { const auto edit = m_session->editColor(target, *choice.color, state, selection, defaults); changed = edit.documentChanged; diagnostic = edit.diagnostic; }
  }
  viewport()->update(); if (!diagnostic.isEmpty()) { emit editRejected(diagnostic); if (!owner) return false; }
  return changed;
}

std::array<std::pair<RectangleHandle, QPointF>, 8> EditorCanvas::handles(const RectangleAnnotation &r) const {
  const double gap = r.style.strokeWidth / 2 + 3 / m_view.zoom();
  const double left = r.left() - gap, right = r.right() + gap, top = r.top() - gap, bottom = r.bottom() + gap;
  const double cx = r.left() / 2 + r.right() / 2, cy = r.top() / 2 + r.bottom() / 2;
  const auto point = [&](double x, double y) { return m_view.toView(rotation(r).map(QPointF(x, y))); };
  return {{{RectangleHandle::TopLeft, point(left, top)}, {RectangleHandle::Top, point(cx, top)},
      {RectangleHandle::TopRight, point(right, top)}, {RectangleHandle::Right, point(right, cy)},
      {RectangleHandle::BottomRight, point(right, bottom)}, {RectangleHandle::Bottom, point(cx, bottom)},
      {RectangleHandle::BottomLeft, point(left, bottom)}, {RectangleHandle::Left, point(left, cy)}}};
}
std::optional<RectangleHandle> EditorCanvas::handleAt(QPointF viewPoint) const {
  if (m_tool == Tool::Crop || !canEditRectangle()) return {};
  const auto *r = m_session->selectedRectangle();
  if (!rectanglePropertiesDiagnostic({r->start, r->end, r->style}).isEmpty()) return {};
  std::optional<RectangleHandle> nearest; double distance = 73;
  for (const auto &[handle, at] : handles(*r)) {
    const auto delta = viewPoint - at; const double squared = delta.x() * delta.x() + delta.y() * delta.y();
    if (std::abs(delta.x()) <= 6 && std::abs(delta.y()) <= 6 && squared < distance) { nearest = handle; distance = squared; }
  }
  return nearest;
}

void EditorCanvas::resetDocument(EditorSession &session, const QImage &displaySource) {
  cancelGesture();
  m_session = &session;
  ++m_documentGeneration;
  m_displaySource = displaySource;
  m_tool = Tool::Rectangle;
  m_initialFit = false;
  setAccessibleDescription(toolInstructions());
  m_spaceHeld = false;
  viewport()->setCursor(Qt::ArrowCursor);
  m_view = EditorViewport(m_image.size());
  m_view.setViewSize(viewport()->size());
  m_view.fit();
  syncViewport();
  emit toolChanged();
}

void EditorCanvas::sourceChanged(const QImage &preparedDisplay) {
  cancelGesture();
  m_displaySource = preparedDisplay.isNull() ? displayCopy(m_image) : preparedDisplay;
  m_view.setImageSize(m_image.size());
  syncViewport();
}

void EditorCanvas::resizeEvent(QResizeEvent *event) {
  QAbstractScrollArea::resizeEvent(event);
  m_view.setViewSize(viewport()->size());
  // Constructor geometry precedes the parent layout. Fit once to the actual
  // initial canvas rather than retaining the tiny pre-layout viewport's zoom.
  if (m_initialFit) { m_initialFit = false; m_view.fit(); }
  syncViewport();
}

void EditorCanvas::scrollContentsBy(int, int) {
  cancelGesture();
  m_view.setOffset({-double(horizontalScrollBar()->value()), -double(verticalScrollBar()->value())});
  syncViewport();
}

void EditorCanvas::paintEvent(QPaintEvent *) {
  const bool moving = movePreviewCurrent();
  QPainter p(viewport());
  p.fillRect(viewport()->rect(), palette().dark());
  p.translate(m_view.offset());
  p.scale(m_view.zoom(), m_view.zoom());
  // Checkerboard exposes source alpha without changing source pixels.
  QPixmap tile(16, 16);
  tile.fill(Qt::white);
  { QPainter checker(&tile); checker.fillRect(0, 0, 8, 8, Qt::lightGray); checker.fillRect(8, 8, 8, 8, Qt::lightGray); }
  p.fillRect(QRectF(QPointF(0, 0), m_image.size()), QBrush(tile));
  p.drawImage(0, 0, m_displaySource);
  p.setRenderHint(QPainter::Antialiasing);
  for (const Annotation &annotation : m_session->document().annotations) {
    const auto *r = std::get_if<RectangleAnnotation>(&annotation);
    if (!r || !r->visible) continue;
    RectangleAnnotation displayed;
    if (m_resize && m_resize->preview && m_resize->state == m_session->stateId() &&
        m_session->selection() == QSet<QUuid>{m_resize->original.id} && r->id == m_resize->original.id) {
      displayed = *r; displayed.start = m_resize->preview->start; displayed.end = m_resize->preview->end; r = &displayed;
    } else if (moving) {
      const auto preview = m_move->preview->constFind(r->id);
      if (preview != m_move->preview->cend()) {
        displayed = *r; displayed.start = preview->start; displayed.end = preview->end; displayed.style = preview->style; r = &displayed;
      }
    }
    const QRectF box(QPointF(r->left(), r->top()), QPointF(r->right(), r->bottom()));
    p.save();
    p.setWorldTransform(rotation(*r), true);
    p.setOpacity(r->style.opacity);
    p.setBrush(QColor::fromRgba(r->style.fillColor));
    p.setPen(QPen(QColor::fromRgba(r->style.strokeColor), r->style.strokeWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.drawRect(box);
    p.restore();
    if (m_session->selection().contains(r->id)) {
      const double gap = r->style.strokeWidth / 2 + 3 / m_view.zoom();
      const QRectF outline = box.adjusted(-gap, -gap, gap, gap);
      p.save();
      p.setWorldTransform(rotation(*r), true);
      p.setBrush(Qt::NoBrush);
      p.setPen(outlinePen(Qt::white));
      p.drawRect(outline);
      p.setPen(outlinePen(palette().highlight().color(), Qt::DashLine));
      p.drawRect(outline);
      p.restore();
      if (m_tool != Tool::Crop && canEditRectangle() && rectanglePropertiesDiagnostic({r->start, r->end, r->style}).isEmpty()) {
        p.save(); p.resetTransform(); p.setOpacity(1); p.setPen(QPen(Qt::black, 1)); p.setBrush(Qt::white);
        for (const auto &[handle, at] : handles(*r)) {
          Q_UNUSED(handle);
          p.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
        }
        p.restore();
      }
    }
  }
  if ((m_resize && (!m_resize->preview || m_resize->state != m_session->stateId() || m_session->selection() != QSet<QUuid>{m_resize->original.id})) ||
      (m_move && !moving)) {
    p.save(); p.resetTransform(); const QPointF at = m_view.toView(m_move ? m_move->last : m_resize->last);
    for (const QPen &pen : {QPen(Qt::white, 4), QPen(Qt::black, 2)}) {
      p.setPen(pen); p.drawLine(at - QPointF(5, 5), at + QPointF(5, 5)); p.drawLine(at + QPointF(5, -5), at + QPointF(-5, 5));
    }
    p.restore();
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
  if (m_tool == Tool::Crop) {
    m_drag = std::make_pair(at, at);
    viewport()->update();
    return;
  }
  if (!(event->modifiers() & Qt::ControlModifier)) if (const auto handle = handleAt(event->position())) {
    const auto original = *m_session->selectedRectangle();
    m_resize = ResizeGesture{original, *handle, m_session->stateId(), at, at, RectangleProperties{original.start, original.end, original.style}};
    viewport()->setCursor(resizeCursor(*handle, original.style.rotationDegrees)); viewport()->update(); emit changed(); return;
  }
  if (const auto hit = topmostAt(at)) {
    const bool additive = event->modifiers() & Qt::ControlModifier;
    QList<QUuid> ids;
    if (additive) {
      for (const QUuid &id : m_session->selection()) ids << id;
      if (!ids.removeOne(*hit)) ids << *hit;
    } else if (!m_session->selection().contains(*hit)) {
      ids << *hit;
    } else {
      for (const QUuid &id : m_session->selection()) ids << id;
    }
    m_session->select(ids);
    if (m_session->selection().contains(*hit) && m_session->selectionEditable()) {
      try {
        MoveGesture draft{m_session->stateId(), m_documentGeneration, m_session->selection(), {}, at, at, {}};
        for (const auto &a : m_session->document().annotations) if (const auto *r = std::get_if<RectangleAnnotation>(&a); r && draft.selection.contains(r->id))
          draft.originals.insert(r->id, {r->start, r->end, r->style});
        m_move = std::move(draft);
        updateMovePreview(at);
      } catch (const std::bad_alloc &) {
        m_move.reset(); const QPointer<EditorCanvas> owner(this); emit editRejected("annotation-edit-failed"); if (!owner) return;
      }
    }
  } else {
    if (!(event->modifiers() & Qt::ControlModifier)) m_session->select({});
    if (m_tool == Tool::Rectangle) {
      m_drag = std::make_pair(at, at);
      m_creation = CreationSnapshot{m_session->toolStyle(), m_session->stateId(), m_session->selection()};
    }
  }
  viewport()->update();
  emit changed();
}

void EditorCanvas::mouseMoveEvent(QMouseEvent *event) {
  if (m_panning) {
    m_view.pan(event->position() - m_panLast);
    m_panLast = event->position();
    syncViewport();
  } else if (m_resize) {
    m_resize->last = m_view.toImage(event->position()); const auto delta = m_resize->last - m_resize->start;
    m_resize->preview = resizeRectangleGeometry(m_resize->original, m_resize->handle, {delta.x(), delta.y()}).properties;
    viewport()->update();
  } else if (m_drag) {
    m_drag->second = m_view.toImage(event->position());
    viewport()->update();
  } else if (m_move) {
    updateMovePreview(m_view.toImage(event->position()));
    viewport()->update();
  } else if (!m_spaceHeld) {
    const auto handle = handleAt(event->position());
    viewport()->setCursor(handle ? resizeCursor(*handle, m_session->selectedRectangle()->style.rotationDegrees)
        : (m_tool == Tool::Crop ? Qt::CrossCursor : Qt::ArrowCursor));
  }
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent *event) {
  if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
    m_view.pan(event->position() - m_panLast);
    m_panning = false;
    viewport()->setCursor(m_spaceHeld ? Qt::OpenHandCursor : (m_tool == Tool::Crop ? Qt::CrossCursor : Qt::ArrowCursor));
    syncViewport();
    return;
  }
  if (event->button() != Qt::LeftButton) return;
  const QPointF at = m_view.toImage(event->position());
  QString rejection;
  if (m_resize) {
    const auto gesture = *m_resize; m_resize.reset(); const auto delta = at - gesture.start;
    m_session->resizeRectangle(gesture.original.id, gesture.handle, {delta.x(), delta.y()}, gesture.state);
    rejection = m_session->editDiagnostic(); viewport()->setCursor(Qt::ArrowCursor);
  } else if (m_drag) {
    const QPointF start = m_drag->first;
    m_drag.reset();
    if (m_tool == Tool::Crop) { const QPointer<EditorCanvas> owner(this); emit cropRequested(start, at); if (!owner) return; }
    else {
      const auto creation = m_creation; m_creation.reset();
      if (creation) m_session->createRectangle({{start.x(), start.y()}, {at.x(), at.y()}, creation->style}, creation->state, creation->selection);
      rejection = m_session->editDiagnostic();
    }
  } else if (m_move) {
    const auto gesture = std::move(*m_move); m_move.reset();
    const QPointF delta = at - gesture.start;
    if (gesture.generation != m_documentGeneration) rejection = "annotation-edit-stale";
    else {
      m_session->moveSelection(delta.x(), delta.y(), gesture.state, gesture.selection);
      rejection = m_session->editDiagnostic();
    }
  }
  viewport()->update();
  if (!rejection.isEmpty()) {
    const QPointer<EditorCanvas> owner(this); emit editRejected(rejection); if (!owner) return;
  }
  emit changed();
}

void EditorCanvas::cancelGesture() {
  m_drag.reset();
  m_creation.reset();
  m_resize.reset();
  m_move.reset();
  if (m_panning) m_view.setOffset(m_panOriginalOffset);
  m_panning = false;
  viewport()->setCursor(m_spaceHeld ? Qt::OpenHandCursor : (m_tool == Tool::Crop ? Qt::CrossCursor : Qt::ArrowCursor));
  viewport()->update();
}

bool EditorCanvas::movePreviewCurrent() const {
  return m_move && m_move->preview && m_move->state == m_session->stateId() &&
      m_move->generation == m_documentGeneration && m_move->selection == m_session->selection();
}

void EditorCanvas::updateMovePreview(QPointF imagePoint) {
  m_move->last = imagePoint;
  m_move->preview.reset();
  if (m_move->state != m_session->stateId() || m_move->generation != m_documentGeneration || m_move->selection != m_session->selection()) return;
  const auto delta = imagePoint - m_move->start;
  try {
    QHash<QUuid, RectangleProperties> preview;
    for (auto it = m_move->originals.cbegin(); it != m_move->originals.cend(); ++it) {
      const auto prepared = moveRectangleGeometry(it.value(), {delta.x(), delta.y()});
      if (!prepared.properties) return;
      preview.insert(it.key(), *prepared.properties);
    }
    m_move->preview = std::move(preview);
  } catch (const std::bad_alloc &) { /* Keep committed geometry and show the rejection marker. */ }
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
    if (!m_panning) cancelGesture();
    m_spaceHeld = true;
    viewport()->setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    return;
  }
  if (event->key() == Qt::Key_Escape && (m_drag || m_move || m_panning || m_resize)) {
    cancelGesture();
    syncViewport();
    return;
  }
  if (interactionActive()) { event->accept(); return; }
  // Canvas-local navigation gestures leave native text/list navigation alone.
  // Legacy bracket gestures dispatch the same frozen operation.
  std::optional<EditorSession::Order> order;
  const auto modifiers = event->modifiers();
  if (modifiers == Qt::NoModifier && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
    event->accept(); if (!event->isAutoRepeat()) deleteSelection(); return;
  }
  if (modifiers == Qt::NoModifier) {
    switch (event->key()) {
      case Qt::Key_Home: order = EditorSession::Order::Front; break;
      case Qt::Key_End: order = EditorSession::Order::Back; break;
      case Qt::Key_PageUp: order = EditorSession::Order::Forward; break;
      case Qt::Key_PageDown: order = EditorSession::Order::Backward; break;
    }
  }
  if (!(modifiers & (Qt::AltModifier | Qt::MetaModifier))) {
    if (event->key() == Qt::Key_BracketRight) order = modifiers & Qt::ControlModifier ? EditorSession::Order::Front : EditorSession::Order::Forward;
    if (event->key() == Qt::Key_BracketLeft) order = modifiers & Qt::ControlModifier ? EditorSession::Order::Back : EditorSession::Order::Backward;
  }
  if (order) { event->accept(); if (!event->isAutoRepeat()) reorderSelection(*order); return; }
  const double step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
  if (m_session->selection().isEmpty()) {
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
  const bool resize = modifiers & Qt::AltModifier;
  if (!resize && event->key() == Qt::Key_R) {
    event->accept(); transformSelection(SelectionTransform::Rotate, modifiers & Qt::ShiftModifier ? -90 : 90); return;
  }
  double x = 0, y = 0;
  switch (event->key()) {
    case Qt::Key_Left: x = -step; break;
    case Qt::Key_Right: x = step; break;
    case Qt::Key_Up: y = -step; break;
    case Qt::Key_Down: y = step; break;
    default: QAbstractScrollArea::keyPressEvent(event); return;
  }
  // Each repeated event remains one edit, with a fresh frozen snapshot.
  event->accept(); transformSelection(resize ? SelectionTransform::Resize : SelectionTransform::Move, x, y);
}

void EditorCanvas::keyReleaseEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    m_spaceHeld = false;
    viewport()->setCursor(m_panning ? Qt::ClosedHandCursor : (m_tool == Tool::Crop ? Qt::CrossCursor : Qt::ArrowCursor));
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
  const auto &list = m_session->document().annotations;
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
