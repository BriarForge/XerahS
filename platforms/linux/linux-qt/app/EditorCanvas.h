// Native canvas input/preview over the shared document and viewport models.
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/EditorViewport.h"

#include <QAbstractScrollArea>
#include <QImage>
#include <QHash>
#include <array>

namespace xerahs::app {

class EditorCanvas final : public QAbstractScrollArea {
  Q_OBJECT
public:
  EditorCanvas(xerahs::editor::EditorSession &session, const QImage &image, QWidget *parent = nullptr,
               const QImage &displaySource = {});

  const xerahs::editor::EditorViewport &viewState() const { return m_view; }
  void zoomIn();
  void zoomOut();
  void resetZoom();
  void zoomToFit();
  // EA-001: creation tools after Crop map to document shape types.
  enum class Tool { Select, Rectangle, Crop, Ellipse, Line, Arrow, Freehand, Text, Step, Highlight, Blur, Pixelate };
  static QString shapeType(Tool tool);
  Tool tool() const { return m_tool; }
  void setTool(Tool);
  QString toolInstructions() const;
  void setCropTool(bool active);
  void sourceChanged(const QImage &preparedDisplay = {});
  bool interactionActive() const { return m_drag.has_value() || m_move.has_value() || m_panning || m_resize.has_value() || m_annotationDialogPending; }
  bool canEditRectangle() const;
  bool canRotateSelection() const { return m_session->canRotateSelection(); }
  bool rotateSelection(QWidget *parent);
  bool canEditColor() const { return m_session->canEditColor(); }
  bool editColor(xerahs::editor::EditorSession::ColorTarget, QWidget *parent);
  bool canEditStyle() const { return m_session->canEditStyle(); }
  double creationStrokeWidth() const { return m_session->toolStyle().strokeWidth; }
  bool changeStrokeWidth(bool increase);
  bool canOrderSelection() const { return m_session->canOrderSelection(); }
  bool reorderSelection(xerahs::editor::EditorSession::Order);
  bool canDeleteSelection() const { return m_session->canDeleteSelection(); }
  bool deleteSelection();
  // Capture uses the same owned native dialog and Core edit as the main editor.
  bool editRectangleProperties(QWidget *parent);
  bool createRectangle(QWidget *parent);
  // Borrowed only until mutation; native list models copy their displayed rows.
  const xerahs::editor::AnnotationDocument &annotationDocument() const { return m_session->document(); }
  const QSet<QUuid> &annotationSelection() const { return m_session->selection(); }
  quint64 annotationState() const { return m_session->stateId(); }
  quint64 documentGeneration() const { return m_documentGeneration; }
  bool selectAnnotations(const QList<QUuid> &, quint64 expectedState, quint64 expectedGeneration);
  // Adopt an already prepared session without rebuilding actions or controls.
  void resetDocument(xerahs::editor::EditorSession &session, const QImage &displaySource);

signals:
  void changed();
  void viewportChanged(double zoom);
  void cropRequested(QPointF start, QPointF end);
  void toolChanged();
  void editRejected(QString diagnostic);

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  bool viewportEvent(QEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  void focusInEvent(QFocusEvent *) override;
  void focusOutEvent(QFocusEvent *) override;
  void scrollContentsBy(int, int) override;

private:
  enum class SelectionTransform { Move, Resize, Rotate };
  bool transformSelection(SelectionTransform, double x, double y = 0);
  bool rectangleDialog(QWidget *parent, bool creation);
  std::optional<QUuid> topmostAt(QPointF imagePoint) const;
  std::optional<xerahs::editor::RectangleHandle> handleAt(QPointF viewPoint) const;
  std::array<std::pair<xerahs::editor::RectangleHandle, QPointF>, 8> handles(const xerahs::editor::RectangleAnnotation &) const;
  void syncViewport();
  void cancelGesture();
  bool movePreviewCurrent() const;
  void updateMovePreview(QPointF imagePoint);
  std::vector<xerahs::editor::PointF> creationGesture(QPointF end) const;
  bool createFromGesture(const std::vector<xerahs::editor::PointF> &gesture);

  xerahs::editor::EditorSession *m_session;
  quint64 m_documentGeneration = 0;
  const QImage &m_image;
  QImage m_displaySource;
  xerahs::editor::EditorViewport m_view;
  std::optional<std::pair<QPointF, QPointF>> m_drag;
  std::vector<xerahs::editor::PointF> m_path;  // freehand samples of the live drag
  struct CreationSnapshot {
    xerahs::editor::RectangleStyle style;
    quint64 state;
    QSet<QUuid> selection;
  };
  std::optional<CreationSnapshot> m_creation;
  struct ResizeGesture {
    xerahs::editor::RectangleAnnotation original;
    xerahs::editor::RectangleHandle handle;
    quint64 state;
    QPointF start, last;
    std::optional<xerahs::editor::RectangleProperties> preview;
  };
  std::optional<ResizeGesture> m_resize;
  struct MoveGesture {
    quint64 state, generation;
    QSet<QUuid> selection;
    QHash<QUuid, xerahs::editor::RectangleProperties> originals;
    QPointF start, last;
    std::optional<QHash<QUuid, xerahs::editor::RectangleProperties>> preview;
  };
  std::optional<MoveGesture> m_move;
  bool m_annotationDialogPending = false;
  bool m_initialFit = true;
  Tool m_tool = Tool::Rectangle;
  bool m_spaceHeld = false;
  bool m_panning = false;
  QPointF m_panLast, m_panOriginalOffset;
};

}  // namespace xerahs::app
