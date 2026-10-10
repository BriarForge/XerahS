// Native canvas input/preview over the shared document and viewport models.
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/EditorViewport.h"

#include <QAbstractScrollArea>
#include <QImage>

namespace xerahs::app {

class EditorCanvas final : public QAbstractScrollArea {
  Q_OBJECT
public:
  EditorCanvas(xerahs::editor::EditorSession &session, const QImage &image, QWidget *parent = nullptr);

  const xerahs::editor::EditorViewport &viewState() const { return m_view; }
  void zoomIn();
  void zoomOut();
  void resetZoom();
  void zoomToFit();
  void setCropTool(bool active);
  void sourceChanged();

signals:
  void changed();
  void viewportChanged(double zoom);
  void cropRequested(QPointF start, QPointF end);

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
  void syncViewport();
  void cancelGesture();

  xerahs::editor::EditorSession &m_session;
  const QImage &m_image;
  xerahs::editor::EditorViewport m_view;
  std::optional<std::pair<QPointF, QPointF>> m_drag;
  bool m_moving = false;
  bool m_cropTool = false;
  QPointF m_moveStart, m_moveLast;
  bool m_spaceHeld = false;
  bool m_panning = false;
  QPointF m_panLast, m_panOriginalOffset;
};

}  // namespace xerahs::app
