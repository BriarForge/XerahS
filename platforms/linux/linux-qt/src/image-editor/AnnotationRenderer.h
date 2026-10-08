// EDITOR-SESSION-001 authoritative export renderer (ES-009, ES-010, ES-025).
// Native previews may be accelerated; this path defines the exported pixels.
#pragma once

#include "image-editor/AnnotationDocument.h"

#include <optional>
#include <vector>

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
  // Pending clarification (ROOT-ESCALATE-001): ES-025 allows rotations that are
  // not multiples of 90 degrees within 1 per channel through exact polygon
  // coverage. This renderer implements the multiple-of-90 reference only.
  UnsupportedRotation,
};

struct RenderResult {
  std::optional<ArgbImage> image;
  std::optional<RenderError> error;
};

// Composites the visible rectangles of `annotations` in order over `source`.
// Unsupported annotation types are skipped by the exporter.
RenderResult render(const ArgbImage &source, const std::vector<Annotation> &annotations);

}  // namespace xerahs::editor
