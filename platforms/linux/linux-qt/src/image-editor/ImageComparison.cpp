#include "image-editor/ImageComparison.h"
#include <algorithm>
#include <new>

namespace xerahs::editor {
namespace {
std::optional<QString> check(ImageExtent extent) {
  if (extent.width < 1 || extent.height < 1)
    return diagnostic::documentInvalid;
  if (extent.width > kMaxDimension || extent.height > kMaxDimension || extent.width * extent.height > kMaxPixels)
    return diagnostic::documentTooLarge;
  return std::nullopt;
}
} // namespace
ComparisonLayoutResult comparisonLayout(ImageExtent first, ImageExtent second, ComparisonAlignment alignment) {
  if (auto error = check(first))
    return {std::nullopt, error};
  if (auto error = check(second))
    return {std::nullopt, error};
  if (alignment != ComparisonAlignment::TopLeft && alignment != ComparisonAlignment::Center)
    return {std::nullopt, diagnostic::documentInvalid};
  ComparisonLayout layout{
      {std::max(first.width, second.width), std::max(first.height, second.height)}, first, second, {}, {}};
  if (auto error = check(layout.canvas))
    return {std::nullopt, error};
  if (alignment == ComparisonAlignment::Center) {
    layout.firstOffset = {double((layout.canvas.width - first.width) / 2),
                          double((layout.canvas.height - first.height) / 2)};
    layout.secondOffset = {double((layout.canvas.width - second.width) / 2),
                           double((layout.canvas.height - second.height) / 2)};
  }
  return {layout, std::nullopt};
}
std::optional<qint64> comparisonSplitColumn(const ComparisonLayout &layout, int reveal) {
  if (check(layout.canvas) || reveal < 0 || reveal > 1000)
    return std::nullopt;
  return layout.canvas.width * reveal / 1000;
}
ComparisonResult composeComparison(const ArgbImage &first, const ArgbImage &second, ComparisonAlignment alignment,
                                   int reveal, const RenderControl &control) {
  const auto fail = [](const QString &error) { return ComparisonResult{std::nullopt, std::nullopt, error}; };
  try {
    const auto layout = comparisonLayout({first.width, first.height}, {second.width, second.height}, alignment);
    if (layout.error)
      return fail(*layout.error);
    const auto split = comparisonSplitColumn(*layout.layout, reveal);
    if (!split || first.pixels.size() != std::size_t(first.width * first.height) ||
        second.pixels.size() != std::size_t(second.width * second.height))
      return fail(diagnostic::documentInvalid);
    const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
    if (cancelled())
      return fail(QStringLiteral("canvas-cancelled"));
    const auto &geometry = *layout.layout;
    ArgbImage output = solidImage(geometry.canvas.width, geometry.canvas.height, 0);
    for (qint64 y = 0; y < output.height; ++y) {
      if (cancelled())
        return fail(QStringLiteral("canvas-cancelled"));
      for (qint64 x = 0; x < output.width; ++x) {
        const bool left = x < *split;
        const ArgbImage &image = left ? first : second;
        const PointF offset = left ? geometry.firstOffset : geometry.secondOffset;
        const qint64 sx = x - qint64(offset.x), sy = y - qint64(offset.y);
        if (sx >= 0 && sy >= 0 && sx < image.width && sy < image.height)
          output.pixels[std::size_t(y * output.width + x)] = image.at(sx, sy);
      }
      if (control.progress)
        control.progress(int((y + 1) * 100 / output.height));
    }
    if (cancelled())
      return fail(QStringLiteral("canvas-cancelled"));
    return {layout.layout, std::move(output), std::nullopt};
  } catch (const std::bad_alloc &) {
    return fail(diagnostic::documentTooLarge);
  }
}
RenderResult renderComparisonSource(const ArgbImage &source, const std::vector<Annotation> &annotations,
                                    const RenderControl &control) {
  if (control.cancelled && control.cancelled())
    return {std::nullopt, RenderError::Cancelled, {}};
  for (const auto &annotation : annotations) {
    if (control.cancelled && control.cancelled())
      return {std::nullopt, RenderError::Cancelled, {}};
    if (const auto *unknown = std::get_if<UnsupportedAnnotation>(&annotation))
      if (unknown->raw.value(u"visible").toBool(true))
        return {std::nullopt, RenderError::UnsupportedAnnotation, {}};
  }
  return render(source, annotations, control);
}
} // namespace xerahs::editor
