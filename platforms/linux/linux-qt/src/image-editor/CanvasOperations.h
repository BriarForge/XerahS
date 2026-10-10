// EC-007, EC-009..EC-011, EC-017: immutable, atomic canvas edits. Encoding
// and native UI are injected by callers; pixel/annotation decisions live here.
#pragma once

#include "image-editor/AnnotationRenderer.h"

#include <functional>

namespace xerahs::editor {

struct CanvasTransform {
  double xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;
  PointF map(PointF boundaryPoint) const;
  // Pixel indices refer to pixel centres, unlike half-open annotation bounds.
  PointF mapPixel(PointF pixelIndex) const;
};

enum class CanvasAction { Crop, ResizeCanvas, ResizeImage, RotateClockwise, RotateCounterClockwise, Rotate180,
                          FlipHorizontal, FlipVertical, AutoCrop, RotateCustom, Flatten, ClearAnnotations, ClearImageAndAnnotations };
enum class CanvasAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
enum class Interpolation { Nearest, Bilinear };
enum class CropBorder { Transparent, TopLeftColor, Color };

struct AutoCropPolicy {
  CropBorder border = CropBorder::Transparent;
  int alphaThreshold = 0;  // alpha <= threshold is always background
  int tolerance = 0;       // maximum absolute RGBA channel distance, inclusive
  Argb color = 0xFFFFFFFF;
  bool includeAnnotations = true;
};

struct CanvasOperation {
  CanvasAction action = CanvasAction::RotateClockwise;
  qint64 width = 0, height = 0;
  PointF start, end;  // Crop: any drag direction, normalized/clipped to source
  CanvasAnchor anchor = CanvasAnchor::Center;
  Argb fill = 0;     // added canvas area, non-premultiplied #AARRGGBB
  Interpolation interpolation = Interpolation::Nearest;
  bool lockAspect = true;
  AutoCropPolicy autoCrop;
  double rotationDegrees = 0;  // positive is clockwise, about canvas centre
  bool expandCanvas = true;
};

struct CanvasControl {
  std::function<bool()> cancelled;
  std::function<void(int)> progress;  // 0..100, called on the executing thread
};

struct CanvasResult {
  std::optional<AnnotationDocument> document;
  std::optional<ArgbImage> image;
  // A document-only edit (e.g. clearing annotations) keeps its source PNG
  // and has no replacement image to encode.
  std::optional<QString> error;
  CanvasTransform transform;
  bool changed = false;
  bool pixelsResampled = false;
};

// A failure returns no partial document/image. Unknown annotation geometry
// is preserved by refusing edits that cannot transform it safely.
CanvasResult applyCanvasOperation(const AnnotationDocument &document, const ArgbImage &source,
                                  const CanvasOperation &operation, const CanvasControl &control = {});

}  // namespace xerahs::editor
