// EDITOR-ANNOTATIONS-001 painting for every box-bounded shape type. The
// canvas preview and the exporter share this code so what the user sees is
// what is saved. Rectangle export keeps the exact ES-025 renderer.
#pragma once

#include "image-editor/AnnotationDocument.h"

#include <QImage>
#include <QTransform>

class QPainter;

namespace xerahs::editor {

// ES-024: rotation about the bounds centre.
QTransform shapeRotation(const RectangleAnnotation &shape);

// Defaults used when a type-specific value is 0.
double shapeFontSize(const RectangleAnnotation &shape);
double shapeStrength(const RectangleAnnotation &shape);

// Paints `shape` in document coordinates on a painter whose transform maps
// document pixels. Region types (blur, pixelate) sample `source`, the
// immutable source pixels, never earlier annotations (EA-011).
// Asset types look up their PNG in `assets`; a missing or undecodable asset
// paints a visible placeholder rather than failing the export.
void paintShape(QPainter &painter, const RectangleAnnotation &shape, const QImage &source,
                const EmbeddedImages *assets = nullptr);

// EA-010: decodes an embedded PNG within the ES-028 limits, or a null image.
QImage decodeAsset(const QByteArray &png);

// EA-004: turns a creation gesture (pointer samples in document pixels) into
// bounds and content. Path types keep their ordered points as fractions of
// bounds padded to at least one pixel per axis, so straight horizontal or
// vertical lines are valid. Freehand drops samples closer than 1 pixel to the
// previous kept sample (EA-007 tolerance). Step and Text accept a click and
// use a default size centred on (step) or starting at (text) the point.
struct ShapeGeometry {
  PointF start, end;
  std::vector<PointF> points;
};
std::optional<ShapeGeometry> shapeGeometry(const QString &type, const std::vector<PointF> &gesture);

// EA-009: the next step number is one past the highest existing step.
qint64 nextStepValue(const AnnotationDocument &document);

}  // namespace xerahs::editor
