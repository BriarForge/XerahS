// EDITOR-SESSION-001 session: rectangle tool, selection, atomic history, and
// dirty state (ES-003 to ES-008, ES-011, ES-012, ES-024, ES-026, ES-027).
#pragma once

#include "image-editor/AnnotationDocument.h"

#include <QSet>

#include <optional>
#include <vector>

namespace xerahs::editor {

class EditorSession {
public:
  // ES-002: canvas coordinates are source pixels; `document` carries source
  // identity and any annotations reopened from a sidecar.
  explicit EditorSession(AnnotationDocument document);

  const AnnotationDocument &document() const { return m_history[m_position].document; }
  const QSet<QUuid> &selection() const { return m_selection; }
  const RectangleStyle &toolStyle() const { return m_tool; }

  // ES-003, ES-024: returns the new ID, or nullopt for a zero-area drag.
  std::optional<QUuid> createRectangle(PointF start, PointF end);
  // ES-004, ES-026: selection is not a document mutation. Unknown IDs are ignored.
  void select(const QList<QUuid> &ids);
  // Each returns whether one history operation was committed (ES-005).
  bool moveSelection(double dx, double dy);
  bool setStrokeWidth(double width);
  bool setStrokeColor(Argb color);
  bool setFillColor(Argb color);
  bool setOpacity(double opacity);
  bool deleteSelection();

  bool undo();
  bool redo();
  int undoCount() const { return static_cast<int>(m_position); }
  int redoCount() const { return static_cast<int>(m_history.size() - 1 - m_position); }

  // ES-027.
  bool dirty() const { return m_history[m_position].stateId != m_checkpoint; }
  void markSaved() { m_checkpoint = m_history[m_position].stateId; }

  // ES-011, ES-012: a save is clean only when the raster and the sidecar both
  // reached persistence. Returns the diagnostics of the failed artifacts.
  QStringList recordSave(bool rasterSaved, bool sidecarSaved);

private:
  struct HistoryState {
    quint64 stateId;
    AnnotationDocument document;
    QSet<QUuid> selection;  // ES-006: selection restored with the state
  };

  // Commits `document` as one operation after the current position, clearing
  // the redo branch (ES-006).
  void commit(AnnotationDocument document);
  template <typename Change>
  bool changeSelected(Change change);

  std::vector<HistoryState> m_history;
  std::size_t m_position = 0;
  quint64 m_nextStateId = 1;
  quint64 m_checkpoint = 0;
  QSet<QUuid> m_selection;
  RectangleStyle m_tool;
};

}  // namespace xerahs::editor
