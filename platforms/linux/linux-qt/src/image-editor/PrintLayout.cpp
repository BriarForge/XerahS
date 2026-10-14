#include "PrintLayout.h"
#include <algorithm>
#include <cmath>
namespace xerahs::editor {
quint32 printPixelOnWhite(quint32 argb) {
  const auto a = argb >> 24;
  const auto channel = [a](quint32 c) { return (c * a + 255 * (255 - a) + 127) / 255; };
  return 0xFF000000u | (channel((argb >> 16) & 255) << 16) | (channel((argb >> 8) & 255) << 8) | channel(argb & 255);
}
std::optional<QRectF> printImageRect(int width, int height, const QRectF &page, double percent) {
  if (width <= 0 || height <= 0 || !std::isfinite(percent) || percent < 1 || percent > 100 ||
      !std::isfinite(page.x()) || !std::isfinite(page.y()) || !std::isfinite(page.width()) ||
      !std::isfinite(page.height()) || page.width() <= 0 || page.height() <= 0) return {};
  const double factor = std::min(page.width() / width, page.height() / height) * (percent / 100);
  const double w = width * factor, h = height * factor;
  const QRectF result(page.x() + (page.width() - w) / 2, page.y() + (page.height() - h) / 2, w, h);
  if (!std::isfinite(result.x()) || !std::isfinite(result.y()) || !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) return {};
  return result;
}
}
