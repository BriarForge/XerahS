#include "image-editor/EditorSession.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace xerahs::editor {

EditorSession::EditorSession(AnnotationDocument document) {
  m_history.push_back(HistoryState{0, std::move(document), {}});
  m_checkpoint = 0;  // a freshly opened session is clean
}

void EditorSession::commit(AnnotationDocument document) {
  // Allocate before changing the redo branch or checkpoint identities.
  if (m_history.capacity() < m_position + 2) m_history.reserve(std::max(m_position + 2, m_history.capacity() * 2));
  HistoryState next{m_nextStateId, std::move(document), m_selection};
  m_history.resize(m_position + 1);  // ES-006: a new mutation clears redo
  m_history.push_back(std::move(next));
  ++m_nextStateId;
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

bool EditorSession::resizeSelection(double dw, double dh) {
  if (!std::isfinite(dw) || !std::isfinite(dh) || (dw == 0 && dh == 0)) return false;
  AnnotationDocument probe = document();
  for (Annotation &annotation : probe.annotations) {
    const auto *r = std::get_if<RectangleAnnotation>(&annotation);
    if (r && m_selection.contains(r->id) &&
        (r->end.x + dw - r->start.x <= 0 || r->end.y + dh - r->start.y <= 0)) {
      return false;  // ES-003: never produce a zero-area rectangle
    }
  }
  return changeSelected([&](RectangleAnnotation &r) {
    r.end = {r.end.x + dw, r.end.y + dh};
    return true;
  });
}

bool EditorSession::rotateSelection(double degrees) {
  if (!std::isfinite(degrees)) return false;
  return changeSelected([&](RectangleAnnotation &r) {
    double next = std::fmod(r.style.rotationDegrees + degrees, 360.0);
    if (next < 0) next += 360.0;
    if (next == r.style.rotationDegrees) return false;
    r.style.rotationDegrees = next;
    return true;
  });
}

bool EditorSession::reorderSelection(Order order) {
  AnnotationDocument next = document();
  auto &list = next.annotations;
  const auto selected = [&](const Annotation &a) { return m_selection.contains(annotationId(a)); };
  if (m_selection.isEmpty() || !std::any_of(list.begin(), list.end(), selected)) return false;
  const std::vector<Annotation> before = list;
  if (order == Order::Front) {
    std::stable_partition(list.begin(), list.end(), [&](const Annotation &a) { return !selected(a); });
  } else if (order == Order::Back) {
    std::stable_partition(list.begin(), list.end(), selected);
  } else if (order == Order::Forward) {
    for (std::size_t i = list.size(); i-- > 1;) {
      if (selected(list[i - 1]) && !selected(list[i])) std::swap(list[i - 1], list[i]);
    }
  } else {
    for (std::size_t i = 1; i < list.size(); ++i) {
      if (selected(list[i]) && !selected(list[i - 1])) std::swap(list[i - 1], list[i]);
    }
  }
  bool same = true;
  for (std::size_t i = 0; i < list.size(); ++i) same = same && annotationId(list[i]) == annotationId(before[i]);
  if (same) return false;
  commit(std::move(next));
  return true;
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

bool EditorSession::commitCanvas(AnnotationDocument next, quint64 expectedState) {
  if (expectedState != stateId()) return false;
  try {
    const QJsonObject serialized = serializeDocument(next);
    if (!parseDocument(serialized).document) return false;
    const auto pngDimension = [&](int offset) {
      const auto *p = reinterpret_cast<const unsigned char *>(next.sourceImagePng.constData() + offset);
      return (qint64(p[0]) << 24) | (qint64(p[1]) << 16) | (qint64(p[2]) << 8) | qint64(p[3]);
    };
    // Canvas edits persist a complete new source, rather than leaving an old
    // PNG attached to new dimensions. parseDocument checked the PNG header.
    if (pngDimension(16) != next.canvasWidth || pngDimension(20) != next.canvasHeight) return false;
    // Preserve the selection from immediately before this operation, then keep
    // only IDs whose objects survived the retained-rectangle policy.
    QSet<QUuid> retained;
    for (const Annotation &a : next.annotations) {
      if (m_selection.contains(annotationId(a))) retained.insert(annotationId(a));
    }
    if (m_history.capacity() < m_position + 2) m_history.reserve(std::max(m_position + 2, m_history.capacity() * 2));
    m_history[m_position].selection = m_selection;
    m_selection = std::move(retained);
    commit(std::move(next));
    return true;
  } catch (const std::bad_alloc &) { return false; }
}

QStringList EditorSession::recordSave(bool rasterSaved, bool sidecarSaved) {
  QStringList failures;
  if (!rasterSaved) failures.append(diagnostic::rasterSaveFailed);
  if (!sidecarSaved) failures.append(diagnostic::sidecarSaveFailed);
  if (failures.isEmpty()) markSaved();  // ES-012: otherwise stay dirty
  return failures;
}

}  // namespace xerahs::editor
