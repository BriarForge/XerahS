#include "image-editor/AnnotationTransform.h"
#include "image-editor/ShapePainter.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace xerahs::editor {
namespace {

constexpr double kTolerance = 1e-9;       // shear / orthogonality, relative
constexpr double kScaleTolerance = 1e-12; // "same scale" and "scale of one"

bool finite(double v) { return std::isfinite(v); }
bool finite(PointF p) { return finite(p.x) && finite(p.y); }

double normalize(double angle) {
  angle = std::fmod(angle, 360.0);
  if (angle < 0) angle += 360;
  if (!(angle < 360)) angle = 0;
  return angle + 0.0;  // never -0
}

// Signed angular distance folded into [0, 180].
double angularDistance(double a, double b) {
  const double d = std::abs(normalize(a) - normalize(b));
  return d > 180 ? 360 - d : d;
}

// Exact values on the axes so quarter turns cannot drift.
double directionDegrees(double x, double y) {
  if (y == 0) return x >= 0 ? 0 : 180;
  if (x == 0) return y > 0 ? 90 : 270;
  return normalize(std::atan2(y, x) * (180 / std::acos(-1.0)));
}

std::pair<double, double> cosSin(double degrees) {  // degrees already in [0,360)
  if (std::fmod(degrees, 90.0) == 0) {
    switch (int(degrees)) {
      case 0: return {1, 0};
      case 90: return {0, 1};
      case 180: return {-1, 0};
      default: return {0, -1};
    }
  }
  const double radians = degrees * (std::acos(-1.0) / 180);
  return {std::cos(radians), std::sin(radians)};
}

}  // namespace

bool validShapeGeometry(const RectangleAnnotation &r) {
  return finite(r.start) && finite(r.end) && r.right() > r.left() && r.bottom() > r.top() &&
         finite(r.right() - r.left()) && finite(r.bottom() - r.top()) && finite(r.style.rotationDegrees) &&
         finite(r.style.strokeWidth) && r.style.strokeWidth > 0 && finite(r.style.opacity) && r.style.opacity >= 0 &&
         r.style.opacity <= 1;
}

ShapeTransformResult transformShape(const RectangleAnnotation &r, const CanvasTransform &t) {
  const auto fail = [](const QString &code) { ShapeTransformResult f; f.error = code; return f; };
  const QString shear = QStringLiteral("canvas-annotation-shear-unsupported");
  const QString unsupported = QStringLiteral("canvas-annotation-transform-unsupported");
  if (!validShapeGeometry(r)) return fail(diagnostic::documentInvalid);
  for (double v : {t.xx, t.xy, t.yx, t.yy, t.dx, t.dy}) if (!finite(v)) return fail(diagnostic::documentInvalid);
  const double det = t.xx * t.yy - t.xy * t.yx;
  if (!finite(det) || det == 0) return fail(diagnostic::documentInvalid);
  const bool diagonal = t.xy == 0 && t.yx == 0;

  // The object's local axes in world space after the map: B = A * R(theta).
  const double theta = normalize(r.style.rotationDegrees);
  const auto [c, s] = cosSin(theta);
  const PointF b1{t.xx * c + t.xy * s, t.yx * c + t.yy * s};
  const PointF b2{-t.xx * s + t.xy * c, -t.yx * s + t.yy * c};
  double l1 = std::hypot(b1.x, b1.y), l2 = std::hypot(b2.x, b2.y);
  if (!finite(l1) || !finite(l2) || !(l1 > 0) || !(l2 > 0)) return fail(diagnostic::documentInvalid);
  // A per-axis scale of an object off a quarter turn would shear its box. No
  // shear is stored, so (as in the reference app) the local width and height
  // scale by the world x and y factors and the rotation is kept.
  const bool sheared = std::abs(b1.x * b2.x + b1.y * b2.y) > kTolerance * l1 * l2;
  if (sheared && !(diagonal && det > 0)) return fail(shear);
  if (sheared) { l1 = std::abs(t.xx); l2 = std::abs(t.yy); }
  if (std::abs(l1 - 1) <= kScaleTolerance) l1 = 1;
  if (std::abs(l2 - 1) <= kScaleTolerance) l2 = 1;
  if (std::abs(l1 - l2) <= kScaleTolerance * std::max(l1, l2)) l2 = l1;

  double newAngle = theta;
  bool mirrorX = false, mirrorY = false;
  if (det > 0) {
    double phi;
    if (sheared) {
      phi = 0;
    } else if (diagonal) {
      phi = t.xx < 0 ? 180 : 0;
    } else {
      const double a1 = std::hypot(t.xx, t.yx), a2 = std::hypot(t.xy, t.yy);
      if (std::abs(t.xx * t.xy + t.yx * t.yy) > kTolerance * a1 * a2 ||
          std::abs(a1 - a2) > kScaleTolerance * std::max(a1, a2)) return fail(unsupported);
      phi = finite(t.angleDegrees) ? t.angleDegrees : directionDegrees(t.xx, t.yx);
    }
    newAngle = normalize(theta + phi);
  } else {
    if (!diagonal) return fail(unsupported);  // canvas operations only produce axis flips
    // Reflection across a vertical (xx<0) or horizontal line. Mirroring the
    // local x axis with angle A equals mirroring local y with angle A+180.
    const double viaX = normalize(t.xx < 0 ? -theta : 180 - theta), viaY = normalize(viaX + 180);
    if (angularDistance(viaX, theta) <= angularDistance(viaY, theta)) { newAngle = viaX; mirrorX = true; }
    else { newAngle = viaY; mirrorY = true; }
  }

  RectangleAnnotation moved = r;
  const PointF centre{r.left() / 2 + r.right() / 2, r.top() / 2 + r.bottom() / 2};
  const PointF mapped = t.map(centre);
  const double halfWidth = (r.right() - r.left()) / 2 * l1, halfHeight = (r.bottom() - r.top()) / 2 * l2;
  moved.start = {mapped.x - halfWidth, mapped.y - halfHeight};
  moved.end = {mapped.x + halfWidth, mapped.y + halfHeight};
  moved.style.rotationDegrees = newAngle;
  // World scale factors drive stroke width and font size like the reference
  // app: stroke *= min(sx, sy), font size *= sy. Rotations and flips leave 1.
  const double worldX = diagonal ? std::abs(t.xx) : l1, worldY = diagonal ? std::abs(t.yy) : l1;
  const double strokeScale = std::min(worldX, worldY);
  if (strokeScale != 1) moved.style.strokeWidth *= strokeScale;
  if (worldY != 1 && (isTextShape(r.type()))) {
    // Step markers with no stored size follow their box height instead.
    if (r.content.fontSize > 0) moved.content.fontSize = r.content.fontSize * worldY;
    else if (r.type() != shape::step) moved.content.fontSize = shapeFontSize(r) * worldY;
  }
  if (mirrorX) for (PointF &p : moved.content.points) p.x = 1 - p.x;
  if (mirrorY) for (PointF &p : moved.content.points) p.y = 1 - p.y;
  if (!validShapeGeometry(moved)) return fail(diagnostic::documentInvalid);
  ShapeTransformResult result;
  result.shape = std::move(moved);
  result.mirrorLocalX = mirrorX;
  result.mirrorLocalY = mirrorY;
  return result;
}

std::optional<QByteArray> mirrorAssetPng(const QByteArray &png, bool horizontal, bool vertical) {
  const QImage decoded = decodeAsset(png);
  if (decoded.isNull()) return std::nullopt;
  QImage mirrored = decoded.mirrored(horizontal, vertical);
  if (mirrored.isNull()) return std::nullopt;
  mirrored.setColorSpace(decoded.colorSpace());
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !mirrored.save(&buffer, "PNG")) return std::nullopt;
  return bytes;
}

}  // namespace xerahs::editor
