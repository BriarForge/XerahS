#pragma once
#include "image-editor/AnnotationDocument.h"
#include <optional>
namespace xerahs::editor {
struct CropRegion {
  qint64 left = 0, top = 0, width = 0, height = 0;
  bool operator==(const CropRegion &r) const { return left == r.left && top == r.top && width == r.width && height == r.height; }
};
bool validCropRegion(const CropRegion &, qint64 sourceWidth, qint64 sourceHeight);
// Same floor/ceil, reverse-drag and clipping rules as the canvas engine.
std::optional<CropRegion> normalizedCropRegion(qint64 sourceWidth, qint64 sourceHeight, PointF start, PointF end);
} // namespace xerahs::editor
