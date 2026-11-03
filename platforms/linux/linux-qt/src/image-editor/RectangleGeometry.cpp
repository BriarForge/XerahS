#include "image-editor/RectangleGeometry.h"
#include <algorithm>
#include <cmath>
namespace xerahs::editor {
QString rectanglePropertiesDiagnostic(const RectangleProperties &p) {
  if (!std::isfinite(p.start.x) || !std::isfinite(p.start.y) || !std::isfinite(p.end.x) || !std::isfinite(p.end.y) ||
      !std::isfinite(p.end.x - p.start.x) || !std::isfinite(p.end.y - p.start.y) ||
      p.end.x <= p.start.x || p.end.y <= p.start.y) return "annotation-geometry-invalid";
  if (!std::isfinite(p.style.strokeWidth) || p.style.strokeWidth <= 0 || p.style.strokeWidth > 10000 ||
      !std::isfinite(p.style.rotationDegrees) || !std::isfinite(p.style.opacity) || p.style.opacity < 0 || p.style.opacity > 1)
    return "annotation-style-invalid";
  // EA-005: finite, bounded border/corner/shadow fields (the strictest, Rectangle, reading).
  if (!validDecoration(shape::rectangle, p.style)) return "annotation-style-invalid";
  // Stroke-grown rotated export bounds must also remain finite. Valid scalar
  // fields alone do not prevent overflowing derived geometry (ES-025).
  double angle = std::fmod(p.style.rotationDegrees, 360.0); if (angle < 0) angle += 360;
  const double half = p.style.strokeWidth / 2;
  if (angle == 0 || angle == 180) {
    return std::isfinite(p.start.x - half) && std::isfinite(p.end.x + half) &&
        std::isfinite(p.start.y - half) && std::isfinite(p.end.y + half) ? QString() : QStringLiteral("annotation-geometry-invalid");
  }
  if (angle == 90 || angle == 270) {
    const double cx = (p.start.x + p.end.x) / 2, cy = (p.start.y + p.end.y) / 2;
    const double hw = (p.end.x - p.start.x) / 2, hh = (p.end.y - p.start.y) / 2;
    return std::isfinite(cx - hh - half) && std::isfinite(cx + hh + half) &&
        std::isfinite(cy - hw - half) && std::isfinite(cy + hw + half) ? QString() : QStringLiteral("annotation-geometry-invalid");
  }
  const double radians = angle * (std::acos(-1.0) / 180), cosine = std::cos(radians), sine = std::sin(radians);
  const double cx = p.start.x / 2 + p.end.x / 2, cy = p.start.y / 2 + p.end.y / 2;
  for (double x : {p.start.x - half, p.end.x + half}) for (double y : {p.start.y - half, p.end.y + half}) {
    const double dx = x - cx, dy = y - cy;
    if (!std::isfinite(cx + dx * cosine - dy * sine) || !std::isfinite(cy + dx * sine + dy * cosine)) return "annotation-geometry-invalid";
  }
  return {};
}
namespace {
PointF mapFraction(const RectangleAnnotation &r, const PointF &f) {
  return {r.left() + f.x * (r.right() - r.left()), r.top() + f.y * (r.bottom() - r.top())};
}
bool pathType(const RectangleAnnotation &r) { return r.type() == shape::line || r.type() == shape::arrow; }
}  // namespace

bool lineHasCurve(const RectangleAnnotation &r) {
  return pathType(r) && r.content.points.size() == 3 && !(r.type() == shape::arrow && r.style.arrowStyle == ArrowStyle::Modern);
}
PointF lineStart(const RectangleAnnotation &r) { return mapFraction(r, r.content.points.at(0)); }
PointF lineEnd(const RectangleAnnotation &r) { return mapFraction(r, r.content.points.at(1)); }
std::optional<PointF> lineAnchor(const RectangleAnnotation &r) {
  if (!lineHasCurve(r)) return std::nullopt;
  return mapFraction(r, r.content.points[2]);
}
PointF quadraticControl(PointF s, PointF e, PointF a) { return {2 * a.x - 0.5 * (s.x + e.x), 2 * a.y - 0.5 * (s.y + e.y)}; }

bool setLineCurve(RectangleAnnotation &r, std::optional<PointF> anchor) {
  if (!pathType(r) || r.content.points.size() < 2 || r.content.points.size() > 3) return false;
  const PointF s = lineStart(r), e = lineEnd(r);
  if (anchor && (!std::isfinite(anchor->x) || !std::isfinite(anchor->y))) return false;
  double left = std::min(s.x, e.x), top = std::min(s.y, e.y), right = std::max(s.x, e.x), bottom = std::max(s.y, e.y);
  if (anchor) {
    // Exact extent of the quadratic: endpoints plus each axis extremum.
    const PointF c = quadraticControl(s, e, *anchor);
    const auto extremum = [](double p0, double p1, double p2) {
      const double denominator = p0 - 2 * p1 + p2;
      if (denominator == 0) return p0;
      const double t = std::clamp((p0 - p1) / denominator, 0.0, 1.0);
      return (1 - t) * (1 - t) * p0 + 2 * (1 - t) * t * p1 + t * t * p2;
    };
    const double ex = extremum(s.x, c.x, e.x), ey = extremum(s.y, c.y, e.y);
    left = std::min(left, ex); right = std::max(right, ex); top = std::min(top, ey); bottom = std::max(bottom, ey);
  }
  if (!(right - left >= 1)) right = left + 1;  // a straight horizontal or vertical line keeps a 1px box
  if (!(bottom - top >= 1)) bottom = top + 1;
  RectangleAnnotation next = r;
  if (r.style.rotationDegrees == 0) { next.start = {left, top}; next.end = {right, bottom}; }
  if (!(next.right() - next.left() > 0) || !(next.bottom() - next.top() > 0)) return false;
  const auto fraction = [&](PointF p) { return PointF{(p.x - next.left()) / (next.right() - next.left()), (p.y - next.top()) / (next.bottom() - next.top())}; };
  next.content.points = {fraction(s), fraction(e)};
  if (anchor) next.content.points.push_back(fraction(*anchor));
  for (const PointF &p : next.content.points)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > kMaxCurveFraction || std::abs(p.y) > kMaxCurveFraction) return false;
  r = std::move(next);
  return true;
}

RectangleMoveResult moveRectangleGeometry(const RectangleProperties &original, PointF delta) {
  if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) return {{}, "annotation-geometry-invalid"};
  RectangleProperties next = original;
  if (delta.x != 0 || delta.y != 0) {
    next.start = {original.start.x + delta.x, original.start.y + delta.y};
    next.end = {original.end.x + delta.x, original.end.y + delta.y};
  }
  if (const auto invalid = rectanglePropertiesDiagnostic(next); !invalid.isEmpty()) return {{}, invalid};
  return {next, {}};
}
RectangleRotationResult rotateRectangleGeometry(const RectangleProperties &original, double degrees) {
  if (!std::isfinite(degrees)) return {{}, "annotation-geometry-invalid"};
  auto next = original;
  next.style.rotationDegrees = std::fmod(std::fmod(original.style.rotationDegrees, 360.0) + std::fmod(degrees, 360.0), 360.0);
  if (next.style.rotationDegrees < 0) next.style.rotationDegrees += 360.0;
  if (const auto invalid = rectanglePropertiesDiagnostic(next); !invalid.isEmpty()) return {{}, invalid};
  return {next, {}};
}
RectangleResizeResult resizeRectangleGeometry(const RectangleAnnotation &r, RectangleHandle handle, PointF delta) {
  const RectangleProperties original{r.start, r.end, r.style};
  if (const auto invalid = rectanglePropertiesDiagnostic(original); !invalid.isEmpty()) return {{}, invalid};
  if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) return {{}, "annotation-geometry-invalid"};
  int horizontal = 0, vertical = 0;
  switch (handle) {
    case RectangleHandle::TopLeft: horizontal = -1; vertical = -1; break;
    case RectangleHandle::Top: vertical = -1; break;
    case RectangleHandle::TopRight: horizontal = 1; vertical = -1; break;
    case RectangleHandle::Right: horizontal = 1; break;
    case RectangleHandle::BottomRight: horizontal = 1; vertical = 1; break;
    case RectangleHandle::Bottom: vertical = 1; break;
    case RectangleHandle::BottomLeft: horizontal = -1; vertical = 1; break;
    case RectangleHandle::Left: horizontal = -1; break;
    default: return {{}, "annotation-handle-invalid"};
  }
  if (delta.x == 0 && delta.y == 0) return {original, {}}; // preserve exact original fields
  double angle = std::fmod(r.style.rotationDegrees, 360.0); if (angle < 0) angle += 360;
  double cosine = 0, sine = 0;
  if (angle == 0) cosine = 1;
  else if (angle == 90) sine = 1;
  else if (angle == 180) cosine = -1;
  else if (angle == 270) sine = -1;
  else { const double radians = angle * (std::acos(-1.0) / 180); cosine = std::cos(radians); sine = std::sin(radians); }
  const double dx = horizontal ? delta.x * cosine + delta.y * sine : 0;
  const double dy = vertical ? -delta.x * sine + delta.y * cosine : 0;
  if (dx == 0 && dy == 0) return {original, {}};
  const double width = r.right() - r.left(), height = r.bottom() - r.top();
  const double newWidth = width + horizontal * dx, newHeight = height + vertical * dy;
  if (!std::isfinite(newWidth) || !std::isfinite(newHeight) || newWidth <= 0 || newHeight <= 0)
    return {{}, "annotation-geometry-invalid"};
  const double cx = r.left() / 2 + r.right() / 2 + (dx / 2 * cosine - dy / 2 * sine);
  const double cy = r.top() / 2 + r.bottom() / 2 + (dx / 2 * sine + dy / 2 * cosine);
  RectangleProperties next{{cx - newWidth / 2, cy - newHeight / 2}, {cx + newWidth / 2, cy + newHeight / 2}, r.style};
  if (const auto invalid = rectanglePropertiesDiagnostic(next); !invalid.isEmpty()) return {{}, invalid};
  return {next, {}};
}
} // namespace xerahs::editor
