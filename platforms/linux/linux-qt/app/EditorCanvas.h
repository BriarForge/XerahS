// Native canvas input/preview over the shared document and viewport models.
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/EditorViewport.h"

#include <QAbstractScrollArea>
#include <QImage>
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
  enum class Tool { Select, Rectangle, Crop };
  Tool tool() const { return m_tool; }
  void setTool(Tool);
  QString toolInstructions() const;
  void setCropTool(bool active);
  void sourceChanged();
  bool interactionActive() const { return m_drag.has_value() || m_moving || m_panning || m_resize.has_value() || m_propertiesPending; }
  bool canEditRectangle() const;
  // Capture uses the same owned native dialog and Core edit as the main editor.
  bool editRectangleProperties(QWidget *parent);
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
  std::optional<QUuid> topmostAt(QPointF imagePoint) const;
  std::optional<xerahs::editor::RectangleHandle> handleAt(QPointF viewPoint) const;
  std::array<std::pair<xerahs::editor::RectangleHandle, QPointF>, 8> handles(const xerahs::editor::RectangleAnnotation &) const;
  void syncViewport();
  void cancelGesture();

  xerahs::editor::EditorSession *m_session;
  const QImage &m_image;
  QImage m_displaySource;
  xerahs::editor::EditorViewport m_view;
  std::optional<std::pair<QPointF, QPointF>> m_drag;
  struct ResizeGesture {
    xerahs::editor::RectangleAnnotation original;
    xerahs::editor::RectangleHandle handle;
    quint64 state;
    QPointF start, last;
    std::optional<xerahs::editor::RectangleProperties> preview;
  };
  std::optional<ResizeGesture> m_resize;
  bool m_propertiesPending = false;
  bool m_initialFit = true;
  bool m_moving = false;
  Tool m_tool = Tool::Rectangle;
  QPointF m_moveStart, m_moveLast;
  bool m_spaceHeld = false;
  bool m_panning = false;
  QPointF m_panLast, m_panOriginalOffset;
};

}  // namespace xerahs::app
