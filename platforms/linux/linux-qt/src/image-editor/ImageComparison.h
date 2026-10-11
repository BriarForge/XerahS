// EC-016/EC-017: immutable image comparison at native pixel sizes.
#pragma once
#include "image-editor/AnnotationRenderer.h"

namespace xerahs::editor {
enum class ComparisonAlignment { TopLeft, Center };
struct ImageExtent {
  qint64 width = 0, height = 0;
};
struct ComparisonLayout {
  ImageExtent canvas, first, second;
  PointF firstOffset, secondOffset;
};
struct ComparisonLayoutResult {
  std::optional<ComparisonLayout> layout;
  std::optional<QString> error;
};
// Union canvas, no pixel resizing. Centre padding puts odd extra pixels on
// the right/bottom. Resource limits apply to each input and the union canvas.
ComparisonLayoutResult comparisonLayout(ImageExtent first, ImageExtent second, ComparisonAlignment alignment);
// 0..1000: first image left of floor(width * reveal / 1000), second to its right.
std::optional<qint64> comparisonSplitColumn(const ComparisonLayout &layout, int reveal);
struct ComparisonResult {
  std::optional<ComparisonLayout> layout;
  std::optional<ArgbImage> image;
  std::optional<QString> error;
};
ComparisonResult composeComparison(const ArgbImage &first, const ArgbImage &second, ComparisonAlignment alignment,
                                   int reveal, const RenderControl &control = {});
// The current editor can be a comparison input without changing its source.
// Unknown visible objects prevent a faithful snapshot; hidden ones are retained.
RenderResult renderComparisonSource(const ArgbImage &source, const std::vector<Annotation> &annotations,
                                    const RenderControl &control = {});
} // namespace xerahs::editor
