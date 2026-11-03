// Qt Core-only rectangle edits shared by native pointer previews and commits.
#pragma once
#include <optional>
#include <vector>
#include "image-editor/AnnotationDocument.h"
namespace xerahs::editor {
// `points`, when set, replaces a Line or Arrow's fractional path points (EA-006
// curve edit); every other edit leaves them unchanged.
struct RectangleProperties { PointF start, end; RectangleStyle style; std::optional<std::vector<PointF>> points = std::nullopt; };
enum class RectangleHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
struct RectangleResizeResult { std::optional<RectangleProperties> properties; QString diagnostic; };
struct RectangleMoveResult { std::optional<RectangleProperties> properties; QString diagnostic; };
struct RectangleRotationResult { std::optional<RectangleProperties> properties; QString diagnostic; };
QString rectanglePropertiesDiagnostic(const RectangleProperties &);

// EA-006: curved Line and Arrow geometry. The curve is a quadratic Bezier
// from the first to the second path point that passes through the anchor
// (points[2], a fraction of the bounds) at its middle. Modern arrows are
// straight and ignore a stored anchor.
bool lineHasCurve(const RectangleAnnotation &);
// Endpoints and anchor in document coordinates (start, end, anchor if curved).
PointF lineStart(const RectangleAnnotation &);
PointF lineEnd(const RectangleAnnotation &);
std::optional<PointF> lineAnchor(const RectangleAnnotation &);
// Quadratic control point that makes the curve pass through `anchor`.
PointF quadraticControl(PointF start, PointF end, PointF anchor);
// Sets (anchor in document coordinates) or removes (nullopt) the curve and, for an
// unrotated shape, resizes the bounds to enclose the endpoints and the curve
// exactly while keeping every point at its document position. A rotated shape
// keeps its bounds (the anchor may then lie outside them). Returns false and
// leaves `shape` unchanged when the result is not finite or in range.
bool setLineCurve(RectangleAnnotation &shape, std::optional<PointF> anchor);
// Translation preserves style and validates all derived geometry before a
// preview or an atomic selection edit can use the result.
RectangleMoveResult moveRectangleGeometry(const RectangleProperties &, PointF delta);
// Add clockwise degrees about each object's existing bounds centre; normalize
// without overflowing the angle sum and validate the resulting export geometry.
RectangleRotationResult rotateRectangleGeometry(const RectangleProperties &, double degrees);
// Delta is in source-image coordinates. Edges constrain to their local axis;
// the opposite displayed handle stays fixed at every rotation. Crossing or
// collapsing it is rejected, rather than flipping the rectangle implicitly.
RectangleResizeResult resizeRectangleGeometry(const RectangleAnnotation &, RectangleHandle, PointF delta);
} // namespace xerahs::editor
