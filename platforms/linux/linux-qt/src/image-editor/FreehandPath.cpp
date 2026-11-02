#include "image-editor/FreehandPath.h"

#include <algorithm>
#include <cmath>

namespace xerahs::editor {
namespace {

double distance(const PointF &a, const PointF &b) { return std::hypot(a.x - b.x, a.y - b.y); }

double segmentDistance(const PointF &p, const PointF &a, const PointF &b) {
  const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
  if (len2 <= 0) return distance(p, a);
  const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
  return distance(p, {a.x + t * dx, a.y + t * dy});
}

void rdp(const std::vector<PointF> &pts, std::size_t first, std::size_t last, std::vector<bool> &keep) {
  if (last <= first + 1) return;
  double worst = 0;
  std::size_t index = first;
  for (std::size_t i = first + 1; i < last; ++i) {
    const double d = segmentDistance(pts[i], pts[first], pts[last]);
    if (d > worst) { worst = d; index = i; }
  }
  if (worst <= kFreehandToleranceDocumentPixels) return;
  keep[index] = true;
  rdp(pts, first, index, keep);
  rdp(pts, index, last, keep);
}

// Turning angle in degrees at pts[i], measured to the nearest samples at least
// kFreehandCornerReachPixels away on each side (or the path end).
double turnDegrees(const std::vector<PointF> &pts, std::size_t i) {
  std::size_t before = i, after = i;
  while (before > 0 && distance(pts[before], pts[i]) < kFreehandCornerReachPixels) --before;
  while (after + 1 < pts.size() && distance(pts[after], pts[i]) < kFreehandCornerReachPixels) ++after;
  const double inLen = distance(pts[i], pts[before]), outLen = distance(pts[after], pts[i]);
  if (!(inLen > 0) || !(outLen > 0)) return 0;
  const double ix = (pts[i].x - pts[before].x) / inLen, iy = (pts[i].y - pts[before].y) / inLen;
  const double ox = (pts[after].x - pts[i].x) / outLen, oy = (pts[after].y - pts[i].y) / outLen;
  const double cosine = std::clamp(ix * ox + iy * oy, -1.0, 1.0);
  return std::acos(cosine) * 180.0 / 3.14159265358979323846;
}

}  // namespace

std::vector<PointF> simplifyFreehand(const std::vector<PointF> &raw) {
  std::vector<PointF> pts;
  for (const PointF &p : raw) if (pts.empty() || !(p == pts.back())) pts.push_back(p);
  if (pts.size() < 3) return pts;

  // Corner anchors: within each run of over-threshold vertices keep the sharpest.
  std::vector<bool> keep(pts.size(), false);
  keep.front() = keep.back() = true;
  std::size_t runBest = 0;
  double runAngle = 0;
  const auto flush = [&] { if (runAngle > 0) keep[runBest] = true; runAngle = 0; };
  for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
    const double angle = turnDegrees(pts, i);
    if (angle > kFreehandCornerDegrees) {
      if (angle > runAngle) { runAngle = angle; runBest = i; }
    } else {
      flush();
    }
  }
  flush();

  // RDP between consecutive anchors; every dropped sample is within tolerance
  // of the segment that replaces it.
  std::size_t anchor = 0;
  for (std::size_t i = 1; i < pts.size(); ++i) {
    if (!keep[i]) continue;
    rdp(pts, anchor, i, keep);
    anchor = i;
  }
  std::vector<PointF> out;
  for (std::size_t i = 0; i < pts.size(); ++i) if (keep[i]) out.push_back(pts[i]);
  return out;
}

}  // namespace xerahs::editor
