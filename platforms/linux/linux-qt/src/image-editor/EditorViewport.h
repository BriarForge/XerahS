// EC-005/EC-006: viewport state never belongs to the document or its history.
#pragma once

#include <QPointF>
#include <QSizeF>

namespace xerahs::editor {

class EditorViewport {
public:
  static constexpr double minimumZoom = 1.0 / 64;
  static constexpr double maximumZoom = 64;

  explicit EditorViewport(QSizeF imageSize);

  double zoom() const { return m_zoom; }
  QPointF offset() const { return m_offset; }
  QSizeF viewSize() const { return m_viewSize; }
  QPointF toImage(QPointF viewPoint) const;
  QPointF toView(QPointF imagePoint) const;

  bool setViewSize(QSizeF size);
  bool zoomAt(double scale, QPointF anchor);
  bool zoomBy(double factor, QPointF anchor);
  bool fit();
  bool reset();
  bool pan(QPointF delta);
  bool setOffset(QPointF offset);

  // Bounds keep at least a 32px strip (or the smaller image/view extent)
  // accessible on each axis, including at both zoom limits.
  QPointF minimumOffset() const;
  QPointF maximumOffset() const;

private:
  void clampOffset();

  QSizeF m_imageSize;
  QSizeF m_viewSize{1, 1};
  double m_zoom = 1;
  QPointF m_offset;
};

}  // namespace xerahs::editor
