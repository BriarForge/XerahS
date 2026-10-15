// EDITOR-SESSION-001 session: rectangle tool, selection, atomic history, and
// dirty state (ES-003 to ES-008, ES-011, ES-012, ES-024, ES-026, ES-027).
#pragma once

#include "image-editor/AnnotationDocument.h"
#include "image-editor/AnnotationTransfer.h"
#include "image-editor/RectangleGeometry.h"
#include "image-editor/CropRegion.h"

#include <QSet>

#include <optional>
#include <vector>

namespace xerahs::editor {

class EditorSession {
public:
  // ES-002: canvas coordinates are source pixels; `document` carries source
  // identity and any annotations reopened from a sidecar.
  enum class InitialPersistence { Saved, Unsaved };
  explicit EditorSession(AnnotationDocument document, InitialPersistence persistence = InitialPersistence::Saved);

  const AnnotationDocument &document() const { return m_history[m_position].document; }
  const QSet<QUuid> &selection() const { return m_selection; }
  const RectangleStyle &toolStyle() const { return m_tool; }
  const QString &editDiagnostic() const { return m_editDiagnostic; }

  // ESET-006: changing creation defaults never edits selection or history.
  bool setCreationStyle(const RectangleStyle &style);

  // ES-003, ES-024: returns the new ID, or nullopt for a zero-area drag.
  std::optional<QUuid> createRectangle(PointF start, PointF end);
  // Owned creation draft: normalize reverse bounds, retain its frozen style,
  // and reject intervening document/selection changes before one insertion.
  std::optional<QUuid> createRectangle(const RectangleProperties &, quint64 expectedState,
      const QSet<QUuid> &expectedSelection);
  // ES-004, ES-026: selection is not a document mutation. Unknown IDs are ignored.
  void select(const QList<QUuid> &ids);
  // Native object lists freeze their displayed history state before dispatch.
  bool select(const QList<QUuid> &ids, quint64 expectedState);
  // Each returns whether one history operation was committed (ES-005).
  bool moveSelection(double dx, double dy);
  // A pointer gesture owns its starting state and exact selection. A stale
  // release must never translate an intervening edit or another selection.
  bool moveSelection(double dx, double dy, quint64 expectedState, const QSet<QUuid> &expectedSelection);
  bool setStrokeWidth(double width);
  bool setStrokeColor(Argb color);
  bool setFillColor(Argb color);
  enum class ColorTarget { Stroke, Fill };
  struct StyleEditResult { bool accepted = false; bool documentChanged = false; bool defaultsChanged = false; QString diagnostic; };
  using ColorEditResult = StyleEditResult;
  // Freeze both the document/selection and the creation defaults across native
  // pickers. Failure leaves both unchanged; an empty selection edits only defaults.
  ColorEditResult editColor(ColorTarget, Argb, quint64 expectedState, const QSet<QUuid> &expectedSelection,
      const RectangleStyle &expectedDefaults);
  bool canEditColor() const;
  bool canEditStyle() const;
  StyleEditResult editStrokeWidth(double, quint64 expectedState, const QSet<QUuid> &expectedSelection,
      const RectangleStyle &expectedDefaults);
  bool setOpacity(double opacity);
  // ES-004: keyboard and pointer parity operations, each one history operation.
  // Resizing moves the bottom-right corner; a result without positive area is rejected.
  bool resizeSelection(double dw, double dh);
  bool resizeSelection(double dw, double dh, quint64 expectedState, const QSet<QUuid> &expectedSelection);
  // Rotation is clockwise degrees about the bounds centre, normalized to [0, 360).
  bool rotateSelection(double degrees);
  bool rotateSelection(double degrees, quint64 expectedState, const QSet<QUuid> &expectedSelection);
  // Native rotation is available only for visible, unlocked selections with
  // at least one represented rectangle. Compatible unknown objects stay inert.
  bool canRotateSelection() const;
  enum class Order { Forward, Backward, Front, Back };
  bool reorderSelection(Order order);
  bool reorderSelection(Order, quint64 expectedState, const QSet<QUuid> &expectedSelection);
  // Ordering opaque compatible objects changes only their position; it never
  // interprets their payload. Hidden or locked members protect the whole group.
  bool canOrderSelection() const { return !m_selection.isEmpty() && selectionEditable(); }
  bool deleteSelection();
  bool deleteSelection(quint64 expectedState, const QSet<QUuid> &expectedSelection);
  bool canDeleteSelection() const { return !m_selection.isEmpty() && selectionEditable(); }
  bool selectionEditable() const;
  // Borrowed until the next document mutation; callers freeze a copy for UI.
  const RectangleAnnotation *selectedRectangle() const;
  // Absolute selected-object edits never change creation defaults. The state
  // and exact single selection must still match the user's frozen snapshot.
  bool editRectangleProperties(const QUuid &, const RectangleProperties &, quint64 expectedState);
  bool resizeRectangle(const QUuid &, RectangleHandle, PointF delta, quint64 expectedState);
  struct TransferEditResult { bool changed = false; QString diagnostic; QList<QUuid> insertedIds; };
  TransferEditResult duplicateSelection(double offset = 10);
  // Caller owns this session exclusively. The identity factory is a pure,
  // caller-thread value provider; null/colliding IDs or a stale state reject
  // the complete insertion before this operation mutates history.
  TransferEditResult insertAnnotations(const std::vector<Annotation> &, quint64 expectedState,
      double offset = 10, const std::function<QUuid()> &newId = QUuid::createUuid);
  TransferEditResult cutAnnotations(quint64 expectedState, const QSet<QUuid> &expectedSelection);

  bool undo();
  bool redo();
  // Admission for owned native history preparation. The target identity also
  // fences a replaced Redo branch after an intervening edit and Undo.
  std::optional<quint64> historyTargetId(bool redo) const;
  bool restoreHistory(bool redo, quint64 expectedState, const QSet<QUuid> &expectedSelection,
                      quint64 expectedTarget);
  int undoCount() const { return static_cast<int>(m_position); }
  int redoCount() const { return static_cast<int>(m_history.size() - 1 - m_position); }
  quint64 stateId() const { return m_history[m_position].stateId; }
  const AnnotationDocument *historyTarget(bool redo) const {
    if (redo) return m_position + 1 < m_history.size() ? &m_history[m_position + 1].document : nullptr;
    return m_position ? &m_history[m_position - 1].document : nullptr;
  }

  // EC-017/ES-005: replace source and annotations only after preparation and
  // encoding succeeded, and only if the document being edited is still current.
  // Validation or a stale state leaves document, selection and history intact.
  bool commitCanvas(AnnotationDocument document, quint64 expectedState);
  // Private in-memory lineage, never serialized into a shared document. Capture
  // independently reproduces this region from its original pixels at handoff.
  std::optional<CropRegion> retainedSourceRegion() const { return m_history[m_position].sourceRegion; }
  bool commitCrop(AnnotationDocument, CropRegion relativeRegion, quint64 expectedState, const QSet<QUuid> &expectedSelection);

  // ES-027.
  bool dirty() const { return !m_checkpoint || m_history[m_position].stateId != *m_checkpoint; }
  void markSaved() { m_checkpoint = m_history[m_position].stateId; }

  // ES-011, ES-012: a save is clean only when the raster and the sidecar both
  // reached persistence. Returns the diagnostics of the failed artifacts.
  QStringList recordSave(bool rasterSaved, bool sidecarSaved);
  // EO-001/ES-027: acknowledge the exported history state, never a later edit.
  // An unreachable saved state stays dirty after branch truncation.
  QStringList recordSnapshotSave(quint64 state, bool rasterSaved, bool sidecarSaved);

private:
  struct HistoryState {
    quint64 stateId;
    AnnotationDocument document;
    QSet<QUuid> selection;  // ES-006: selection restored with the state
    std::optional<CropRegion> sourceRegion;
  };

  // Commits `document` as one operation after the current position, clearing
  // the redo branch (ES-006).
  void commit(AnnotationDocument document);
  void commitWithSelection(AnnotationDocument, QSet<QUuid> selection);
  template <typename Change>
  bool changeSelected(Change change);
  bool admitStyleEdit(quint64, const QSet<QUuid> &, const RectangleStyle &);

  std::vector<HistoryState> m_history;
  std::size_t m_position = 0;
  quint64 m_nextStateId = 1;
  std::optional<quint64> m_checkpoint = 0;
  QSet<QUuid> m_selection;
  RectangleStyle m_tool;
  QString m_editDiagnostic;
};

}  // namespace xerahs::editor
