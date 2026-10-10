#include "image-editor/EditorViewport.h"
#include "image-editor/AnnotationDocument.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace xerahs::editor {
namespace {

bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
bool valid(QSizeF size) {
  return std::isfinite(size.width()) && std::isfinite(size.height()) && size.width() > 0 && size.height() > 0;
}

double visibleExtent(double image, double view) { return std::min({32.0, image, view / 2}); }

}  // namespace

EditorViewport::EditorViewport(QSizeF imageSize) : m_imageSize(imageSize) {
  if (!valid(imageSize) || imageSize.width() > kMaxDimension || imageSize.height() > kMaxDimension ||
      imageSize.width() * imageSize.height() > kMaxPixels)
    throw std::invalid_argument("invalid viewport image dimensions");
}

QPointF EditorViewport::toImage(QPointF point) const { return (point - m_offset) / m_zoom; }
QPointF EditorViewport::toView(QPointF point) const { return point * m_zoom + m_offset; }

QPointF EditorViewport::minimumOffset() const {
  return {visibleExtent(m_imageSize.width() * m_zoom, m_viewSize.width()) - m_imageSize.width() * m_zoom,
          visibleExtent(m_imageSize.height() * m_zoom, m_viewSize.height()) - m_imageSize.height() * m_zoom};
}

QPointF EditorViewport::maximumOffset() const {
  return {m_viewSize.width() - visibleExtent(m_imageSize.width() * m_zoom, m_viewSize.width()),
          m_viewSize.height() - visibleExtent(m_imageSize.height() * m_zoom, m_viewSize.height())};
}

void EditorViewport::clampOffset() {
  const QPointF lo = minimumOffset(), hi = maximumOffset();
  m_offset = {std::clamp(m_offset.x(), lo.x(), hi.x()), std::clamp(m_offset.y(), lo.y(), hi.y())};
}

bool EditorViewport::setViewSize(QSizeF size) {
  if (!valid(size) || size == m_viewSize) return false;
  m_viewSize = size;
  clampOffset();
  return true;
}

bool EditorViewport::setImageSize(QSizeF size) {
  if (!valid(size) || size.width() > kMaxDimension || size.height() > kMaxDimension ||
      size.width() * size.height() > kMaxPixels || size == m_imageSize) return false;
  m_imageSize = size;
  clampOffset();
  return true;
}

bool EditorViewport::zoomAt(double scale, QPointF anchor) {
  if (!std::isfinite(scale) || scale <= 0 || !finite(anchor)) return false;
  scale = std::clamp(scale, minimumZoom, maximumZoom);
  if (scale == m_zoom) return false;
  const QPointF logical = toImage(anchor);
  const QPointF offset = anchor - logical * scale;
  if (!finite(offset)) return false;
  m_zoom = scale;
  m_offset = offset;
  clampOffset();
  return true;
}

bool EditorViewport::zoomBy(double factor, QPointF anchor) {
  if (!std::isfinite(factor) || factor <= 0) return false;
  return zoomAt(m_zoom * factor, anchor);
}

bool EditorViewport::fit() {
  const double scale = std::clamp(std::min(m_viewSize.width() / m_imageSize.width(),
                                         m_viewSize.height() / m_imageSize.height()), minimumZoom, maximumZoom);
  const QPointF offset{(m_viewSize.width() - m_imageSize.width() * scale) / 2,
                       (m_viewSize.height() - m_imageSize.height() * scale) / 2};
  if (scale == m_zoom && offset == m_offset) return false;
  m_zoom = scale;
  m_offset = offset;
  return true;
}

bool EditorViewport::reset() { return zoomAt(1, {m_viewSize.width() / 2, m_viewSize.height() / 2}); }

bool EditorViewport::pan(QPointF delta) {
  if (!finite(delta)) return false;
  return setOffset(m_offset + delta);
}

bool EditorViewport::setOffset(QPointF offset) {
  if (!finite(offset)) return false;
  const QPointF prior = m_offset;
  m_offset = offset;
  clampOffset();
  return prior != m_offset;
}

}  // namespace xerahs::editor
