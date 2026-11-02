// EC-009, EC-011: the one routine that carries an annotation through a canvas
// affine map (translation, quarter/half/custom rotation, flip, resize). Every
// box-bounded tool uses it, so coordinates, rotation, mirrored path/tail
// geometry and directional assets can never diverge between operations.
#pragma once

#include "image-editor/CanvasOperations.h"

#include <QByteArray>

#include <optional>

namespace xerahs::editor {

struct ShapeTransformResult {
  std::optional<RectangleAnnotation> shape;  // empty on failure
  QString error;  // stable code: document-invalid, canvas-annotation-shear-unsupported, ...
  // A reflection is absorbed in the object's own frame (the document has no
  // mirror field): path points and balloon tails are already mirrored in
  // `shape`; embedded asset pixels must be mirrored by the caller.
  bool mirrorLocalX = false;
  bool mirrorLocalY = false;
};

// Finite, positive-area bounds with valid style (shared with canvas edits).
bool validShapeGeometry(const RectangleAnnotation &);

// Maps the shape through `transform` without distortion. A transform that
// would shear the object (non-uniform scale of a non-quarter-rotated shape)
// or give a rendered stroke different widths per axis fails with
// canvas-annotation-shear-unsupported; the caller must then abandon the
// whole operation. Rotations and flips by exact quarter turns are bit exact
// for exact input. Box shapes keep their bounds centre-mapped; a reflection
// mirrors the object's own points (axis chosen to keep the rotation closest
// to the original so text stays readable) and never turns text upside down.
ShapeTransformResult transformShape(const RectangleAnnotation &, const CanvasTransform &);

// Mirrors an embedded PNG's pixels (all channels, colour space preserved).
// Returns nullopt when the PNG cannot be decoded within limits or encoded.
std::optional<QByteArray> mirrorAssetPng(const QByteArray &png, bool horizontal, bool vertical);

}  // namespace xerahs::editor
