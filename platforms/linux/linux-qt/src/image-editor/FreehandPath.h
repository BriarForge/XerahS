// EA-007 freehand path simplification. Qt Core only; no product policy lives
// outside this file. All values are in document (source-pixel) units and are
// applied once, at gesture commit, never while rendering.
//
// Linux implementation choice pending cross-platform agreement (stored data is
// unaffected: points remain ordered fractions of the shape bounds).
#pragma once

#include "image-editor/AnnotationDocument.h"

#include <vector>

namespace xerahs::editor {

// Maximum perpendicular distance a dropped sample may lie from the simplified
// path (Ramer-Douglas-Peucker epsilon), in source pixels.
inline constexpr double kFreehandToleranceDocumentPixels = 0.5;
// A vertex whose direction change exceeds this angle is an intentional corner
// and is retained regardless of the tolerance.
inline constexpr double kFreehandCornerDegrees = 30.0;
// Corner turning angles are measured between chords to the nearest samples at
// least this far (document pixels) before and after the vertex, so sub-pixel
// jitter on a dense path is not mistaken for a corner.
inline constexpr double kFreehandCornerReachPixels = 4.0;

// Returns the simplified ordered path. Preserves order, the first and last
// samples, every detected corner and all samples needed to stay within the
// tolerance of the raw path. Consecutive duplicate samples are dropped. Input
// must be finite (callers validate); fewer than 3 samples are returned as is
// minus duplicates.
std::vector<PointF> simplifyFreehand(const std::vector<PointF> &raw);

}  // namespace xerahs::editor
