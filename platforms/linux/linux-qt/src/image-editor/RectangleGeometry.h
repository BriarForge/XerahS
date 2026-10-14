// Qt Core-only rectangle edits shared by native pointer previews and commits.
#pragma once
#include "image-editor/AnnotationDocument.h"
namespace xerahs::editor {
struct RectangleProperties { PointF start, end; RectangleStyle style; };
enum class RectangleHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
struct RectangleResizeResult { std::optional<RectangleProperties> properties; QString diagnostic; };
QString rectanglePropertiesDiagnostic(const RectangleProperties &);
// Delta is in source-image coordinates. Edges constrain to their local axis;
// the opposite displayed handle stays fixed at every rotation. Crossing or
// collapsing it is rejected, rather than flipping the rectangle implicitly.
RectangleResizeResult resizeRectangleGeometry(const RectangleAnnotation &, RectangleHandle, PointF delta);
} // namespace xerahs::editor
