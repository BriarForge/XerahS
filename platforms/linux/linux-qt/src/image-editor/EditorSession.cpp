#include "image-editor/EditorSession.h"

#include <algorithm>
#include <cmath>

namespace xerahs::editor {

EditorSession::EditorSession(AnnotationDocument document) {
  m_history.push_back(HistoryState{0, std::move(document), {}});
  m_checkpoint = 0;  // a freshly opened session is clean
}

void EditorSession::commit(AnnotationDocument document) {
  m_history.resize(m_position + 1);  // ES-006: a new mutation clears redo
  m_history.push_back(HistoryState{m_nextStateId++, std::move(document), m_selection});
  ++m_position;
}

std::optional<QUuid> EditorSession::createRectangle(PointF start, PointF end) {
  RectangleAnnotation rectangle;
  rectangle.id = QUuid::createUuid();
  rectangle.start = {std::min(start.x, end.x), std::min(start.y, end.y)};
  rectangle.end = {std::max(start.x, end.x), std::max(start.y, end.y)};
  if (!std::isfinite(rectangle.start.x) || !std::isfinite(rectangle.start.y) || !std::isfinite(rectangle.end.x) ||
      !std::isfinite(rectangle.end.y)) {
    return std::nullopt;
  }
  // ES-003: zero area (zero width or zero height) is rejected.
  if (rectangle.end.x - rectangle.start.x <= 0 || rectangle.end.y - rectangle.start.y <= 0) return std::nullopt;
  rectangle.style = m_tool;  // snapshot of the tool settings
  AnnotationDocument next = document();
  next.annotations.push_back(rectangle);  // ES-024: highest z-order
  m_selection = {rectangle.id};          // ES-024: the only selected object
  commit(std::move(next));
  return rectangle.id;
}

void EditorSession::select(const QList<QUuid> &ids) {
  QSet<QUuid> selection;
  for (const Annotation &annotation : document().annotations) {
    if (ids.contains(annotationId(annotation))) selection.insert(annotationId(annotation));
  }
  m_selection = selection;
}

template <typename Change>
bool EditorSession::changeSelected(Change change) {
  if (m_selection.isEmpty()) return false;
  AnnotationDocument next = document();
  bool changed = false;
  for (Annotation &annotation : next.annotations) {
    auto *rectangle = std::get_if<RectangleAnnotation>(&annotation);
    if (rectangle && m_selection.contains(rectangle->id)) changed = change(*rectangle) || changed;
  }
  // Pending clarification (ROOT-ESCALATE-001): ES-005 makes every
  // user-visible mutation one operation; a command that leaves every selected
  // object unchanged is not a mutation and records no history.
  if (!changed) return false;
  commit(std::move(next));
  return true;
}

bool EditorSession::moveSelection(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
  return changeSelected([&](RectangleAnnotation &r) {
    if (dx == 0 && dy == 0) return false;
    r.start = {r.start.x + dx, r.start.y + dy};
    r.end = {r.end.x + dx, r.end.y + dy};
    return true;
  });
}

bool EditorSession::setStrokeWidth(double width) {
  if (!std::isfinite(width) || width <= 0 || width > 10000) return false;
  m_tool.strokeWidth = width;  // ES-026: style changes update the tool default
  return changeSelected([&](RectangleAnnotation &r) {
    if (r.style.strokeWidth == width) return false;
    r.style.strokeWidth = width;
    return true;
  });
}

bool EditorSession::setStrokeColor(Argb color) {
  m_tool.strokeColor = color;
  return changeSelected([&](RectangleAnnotation &r) {
    if (r.style.strokeColor == color) return false;
    r.style.strokeColor = color;
    return true;
  });
}

bool EditorSession::setFillColor(Argb color) {
  m_tool.fillColor = color;
  return changeSelected([&](RectangleAnnotation &r) {
    if (r.style.fillColor == color) return false;
    r.style.fillColor = color;
    return true;
  });
}

bool EditorSession::setOpacity(double opacity) {
  if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) return false;
  m_tool.opacity = opacity;
  return changeSelected([&](RectangleAnnotation &r) {
    if (r.style.opacity == opacity) return false;
    r.style.opacity = opacity;
    return true;
  });
}

bool EditorSession::deleteSelection() {
  if (m_selection.isEmpty()) return false;
  AnnotationDocument next = document();
  const auto removed = std::remove_if(next.annotations.begin(), next.annotations.end(), [&](const Annotation &a) {
    return m_selection.contains(annotationId(a));
  });
  if (removed == next.annotations.end()) return false;
  next.annotations.erase(removed, next.annotations.end());
  m_selection.clear();
  commit(std::move(next));
  return true;
}

bool EditorSession::undo() {
  if (m_position == 0) return false;
  // ES-006: restore the complete state before the most recent operation,
  // including the selection that state had.
  m_selection = m_history[m_position - 1].selection;
  --m_position;
  return true;
}

bool EditorSession::redo() {
  if (m_position + 1 >= m_history.size()) return false;
  ++m_position;
  m_selection = m_history[m_position].selection;
  return true;
}

QStringList EditorSession::recordSave(bool rasterSaved, bool sidecarSaved) {
  QStringList failures;
  if (!rasterSaved) failures.append(diagnostic::rasterSaveFailed);
  if (!sidecarSaved) failures.append(diagnostic::sidecarSaveFailed);
  if (failures.isEmpty()) markSaved();  // ES-012: otherwise stay dirty
  return failures;
}

}  // namespace xerahs::editor
