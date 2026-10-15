#include "image-editor/CropRegion.h"
#include <algorithm>
#include <cmath>
namespace xerahs::editor {
bool validCropRegion(const CropRegion &r, qint64 width, qint64 height) {
  return width > 0 && height > 0 && width <= kMaxDimension && height <= kMaxDimension && width * height <= kMaxPixels &&
      r.left >= 0 && r.top >= 0 && r.width > 0 && r.height > 0 && r.left < width && r.top < height &&
      r.width <= width - r.left && r.height <= height - r.top;
}
std::optional<CropRegion> normalizedCropRegion(qint64 width, qint64 height, PointF start, PointF end) {
  if (!validCropRegion({0, 0, width, height}, width, height) || !std::isfinite(start.x) || !std::isfinite(start.y) ||
      !std::isfinite(end.x) || !std::isfinite(end.y) || start.x == end.x || start.y == end.y) return {};
  const auto edge = [](double value, qint64 extent, bool upper) { value = std::clamp(value, 0.0, double(extent)); return qint64(upper ? std::ceil(value) : std::floor(value)); };
  const auto left = edge(std::min(start.x, end.x), width, false), top = edge(std::min(start.y, end.y), height, false);
  CropRegion r{left, top, edge(std::max(start.x, end.x), width, true) - left, edge(std::max(start.y, end.y), height, true) - top};
  return validCropRegion(r, width, height) ? std::optional<CropRegion>(r) : std::nullopt;
}
} // namespace xerahs::editor
