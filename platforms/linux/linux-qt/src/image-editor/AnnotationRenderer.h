// EDITOR-SESSION-001 authoritative export renderer (ES-009, ES-010, ES-025).
// Native previews may be accelerated; this path defines the exported pixels.
#pragma once

#include "image-editor/AnnotationDocument.h"

#include <optional>
#include <vector>
#include <functional>

namespace xerahs::editor {

// Non-premultiplied 8-bit sRGB, row-major.
struct ArgbImage {
  qint64 width = 0;
  qint64 height = 0;
  std::vector<Argb> pixels;

  Argb at(qint64 x, qint64 y) const { return pixels[static_cast<std::size_t>(y * width + x)]; }
};

ArgbImage solidImage(qint64 width, qint64 height, Argb color);

enum class RenderError {
  InvalidSource,
  InvalidGeometry,
  Cancelled,
};

struct RenderResult {
  std::optional<ArgbImage> image;
  std::optional<RenderError> error;
};

// Composites the visible rectangles of `annotations` in order over `source`.
// Unsupported annotation types are skipped by the exporter.
struct RenderControl {
  std::function<bool()> cancelled;
  std::function<void(int)> progress;
};

RenderResult render(const ArgbImage &source, const std::vector<Annotation> &annotations,
                    const RenderControl &control = {});

}  // namespace xerahs::editor
