// EDITOR-EFFECTS-001: EE-003, EE-006, EE-008..EE-010, EE-016, EE-018.
// Geometry algorithms ported from ShareX Team's GPLv3 ImageEditor references;
// each registration records its census referencePath.
#include "Effect.h"
#include "EffectSupport.h"
#include "image-editor/CanvasOperations.h"

#include <QPainter>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace xerahs::effects {
namespace {
using namespace support;
namespace ed = xerahs::editor;
constexpr float pi = 3.14159265358979323846f;
int byte(float v) { return int(std::nearbyint(std::clamp(v, 0.f, 255.f))); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float a, float b, float v) {
  const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
QRgb blend(QRgb a, QRgb b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  return qRgba(byte(lerp(qRed(a), qRed(b), t)), byte(lerp(qGreen(a), qGreen(b), t)),
               byte(lerp(qBlue(a), qBlue(b), t)), byte(lerp(qAlpha(a), qAlpha(b), t)));
}
QRgb shade(QRgb p, float k) {
  return qRgba(byte(qRed(p) * k), byte(qGreen(p) * k), byte(qBlue(p) * k), qAlpha(p));
}
QRgb over(QRgb v, QRgb bg) {
  const float a = qAlpha(v) / 255.f, ba = qAlpha(bg) / 255.f, oa = a + ba * (1 - a);
  if (oa == 0) return 0;
  return qRgba(byte((qRed(v) * a + qRed(bg) * ba * (1 - a)) / oa),
               byte((qGreen(v) * a + qGreen(bg) * ba * (1 - a)) / oa),
               byte((qBlue(v) * a + qBlue(bg) * ba * (1 - a)) / oa), byte(oa * 255));
}
QRgb sample(const QImage &s, float x, float y, bool transparent = false) {
  if (!std::isfinite(x) || !std::isfinite(y)) return 0;
  if (transparent && (x < 0 || y < 0 || x > s.width() - 1 || y > s.height() - 1)) return 0;
  x = std::clamp(x, 0.f, float(s.width() - 1)); y = std::clamp(y, 0.f, float(s.height() - 1));
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const int x1 = std::min(x0 + 1, s.width() - 1), y1 = std::min(y0 + 1, s.height() - 1);
  const QRgb a = s.pixel(x0, y0), b = s.pixel(x1, y0), c = s.pixel(x0, y1), d = s.pixel(x1, y1);
  const float tx = x - x0, ty = y - y0;
  const auto channel = [&](int shift) {
    return byte(lerp(lerp((a >> shift) & 255, (b >> shift) & 255, tx),
                     lerp((c >> shift) & 255, (d >> shift) & 255, tx), ty));
  };
  return qRgba(channel(16), channel(8), channel(0), channel(24));
}
template<class Fn>
EffectResult pixels(const QImage &s, QSize size, const EffectContext &c, Fn fn) {
  if (size.width() < 1 || size.height() < 1 || size.width() > ed::kMaxDimension ||
      size.height() > ed::kMaxDimension || qint64(size.width()) * size.height() > ed::kMaxPixels) return {{}, kInvalidInput};
  QImage out = size == s.size() ? makeResult(s) : QImage(size, QImage::Format_ARGB32);
  if (out.isNull()) return {{}, kFailed};
  out.setColorSpace(s.colorSpace());
  if (!runRows(c, size.height(), 0, 100, [&](int y) {
    auto row = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < size.width(); ++x) row[x] = fn(x, y);
  }) || c.cancelled()) return {{}, kCancelled};
  return {out, {}};
}
EffectResult identity(const QImage &s, const EffectContext &c) {
  return pixels(s, s.size(), c, [&](int x, int y) { return s.pixel(x, y); });
}
EffectContext phase(const EffectContext &c, int lo, int hi) {
  EffectContext part = c;
  part.progress = [=](int p) { c.progress(lo + p * (hi - lo) / 100); };
  return part;
}
EffectResult canvas(const QImage &s, const ed::CanvasOperation &op, const EffectContext &c) {
  ed::ArgbImage input{s.width(), s.height(), {}};
  input.pixels.resize(size_t(s.width()) * s.height());
  if (!runRows(c, s.height(), 0, 10, [&](int y) {
    std::copy_n(reinterpret_cast<const QRgb *>(s.constScanLine(y)), s.width(), input.pixels.begin() + size_t(y) * s.width());
  })) return {{}, kCancelled};
  ed::AnnotationDocument doc; doc.canvasWidth = s.width(); doc.canvasHeight = s.height();
  auto result = ed::applyCanvasOperation(doc, input, op, {c.cancelled, [&](int p) { c.progress(10 + p * 80 / 100); }});
  if (result.error) return {{}, *result.error == "canvas-cancelled" ? kCancelled : kInvalidInput};
  if (!result.image) return identity(s, phase(c, 90, 100));
  const auto &image = *result.image;
  return pixels(s, QSize(int(image.width), int(image.height)), phase(c, 90, 100),
                [&](int x, int y) { return image.at(x, y); });
}
EffectResult canvasEffect(const QString &id, const QImage &s, const ParamValues &p, const EffectContext &c) {
  ed::CanvasOperation op;
  if (id == "crop_image") {
    const int x = std::min(intOf(p, "x"), s.width()), y = std::min(intOf(p, "y"), s.height());
    const int w = std::min(intOf(p, "width"), s.width() - x), h = std::min(intOf(p, "height"), s.height() - y);
    if (!w || !h) return identity(s, c);
    op.action = ed::CanvasAction::Crop; op.start = {double(x), double(y)}; op.end = {double(x + w), double(y + h)};
  } else if (id == "auto_crop_image") {
    op.action = ed::CanvasAction::AutoCrop; op.autoCrop.border = ed::CropBorder::TopLeftColor;
    op.autoCrop.tolerance = intOf(p, "tolerance"); op.autoCrop.includeAnnotations = false;
  } else if (id == "resize_image") {
    int w = intOf(p, "width"), h = intOf(p, "height");
    if (boolOf(p, "maintain_aspect_ratio") && (w || h)) {
      const double scale = std::max(double(w) / s.width(), double(h) / s.height());
      w = std::max(1, int(std::nearbyint(s.width() * scale)));
      h = std::max(1, int(std::nearbyint(s.height() * scale)));
    }
    op.action = ed::CanvasAction::ResizeImage; op.width = w ? w : s.width(); op.height = h ? h : s.height();
    op.lockAspect = false;
    // Canvas bilinear replaces Skia Catmull-Rom so geometry and effects share identical pixels.
    op.interpolation = ed::Interpolation::Bilinear;
  } else if (id == "rotate_custom_angle") {
    op.action = ed::CanvasAction::RotateCustom; op.rotationDegrees = floatOf(p, "angle");
    op.expandCanvas = boolOf(p, "auto_resize"); op.interpolation = ed::Interpolation::Nearest;
  } else {
    const int left = intOf(p, "left"), top = intOf(p, "top");
    const int w = s.width() + left + intOf(p, "right"), h = s.height() + top + intOf(p, "bottom");
    if (w <= 0 || h <= 0) return identity(s, c);
    // Canvas padding preserves source pixels, unlike Skia source-over onto a coloured fill.
    op.action = ed::CanvasAction::ResizeCanvas; op.fill = colorOf(p, "background_color").rgba();
    op.anchor = ed::CanvasAnchor::BottomRight;
    op.width = s.width() + std::max(left, 0); op.height = s.height() + std::max(top, 0);
    auto padded = canvas(s, op, phase(c, 0, 33)); if (!padded.ok()) return padded;
    op.anchor = ed::CanvasAnchor::TopLeft;
    op.width = std::max(int(op.width), w + std::max(-left, 0));
    op.height = std::max(int(op.height), h + std::max(-top, 0));
    padded = canvas(padded.image, op, phase(c, 33, 66)); if (!padded.ok()) return padded;
    op.action = ed::CanvasAction::Crop;
    op.start = {double(std::max(-left, 0)), double(std::max(-top, 0))};
    op.end = {op.start.x + w, op.start.y + h};
    return canvas(padded.image, op, phase(c, 66, 100));
  }
  return canvas(s, op, c);
}

EffectResult radial(const QString &id, const QImage &s, const ParamValues &p, const EffectContext &c) {
  const float cx = floatOf(p, "center_x_percentage") / 100 * (s.width() - 1);
  const float cy = floatOf(p, "center_y_percentage") / 100 * (s.height() - 1);
  const float radius = std::max(1.f, std::min(s.width(), s.height()) * floatOf(p, "radius_percentage") / 100);
  const float strength = floatOf(p, "strength") / 100, angle = floatOf(p, "angle") * pi / 180;
  if (((id == "fisheye_lens" || id == "pinch_bulge") && std::abs(strength) < .0001f) ||
      (id == "twirl" && std::abs(angle) < .0001f) ||
      (id == "ripple_refraction" && floatOf(p, "amplitude") <= .01f) ||
      (id == "liquify_push_smudge" && std::abs(floatOf(p, "distance")) <= .01f)) return identity(s, c);
  return pixels(s, s.size(), c, [&](int x, int y) {
    const float dx = x - cx, dy = y - cy, dist = std::hypot(dx, dy);
    if (id == "ripple_refraction") {
      if (dist <= .0001f) return s.pixel(x, y);
      const float phase = dist * 2 * pi / floatOf(p, "wavelength") + floatOf(p, "phase") * pi / 180;
      const float amp = floatOf(p, "amplitude");
      const float k = (dist + std::sin(phase) * amp + std::cos(phase) * amp * floatOf(p, "refraction") / 100 * .35f) / dist;
      return sample(s, cx + dx * k, cy + dy * k);
    }
    if (dist >= radius) return s.pixel(x, y);
    if (id == "liquify_push_smudge") {
      const float smudge = floatOf(p, "smudge") / 100;
      const int n = 1 + int(std::nearbyint(smudge * 5));
      const float influence = 1 - smooth(0, 1, dist / radius);
      const float push = floatOf(p, "distance") * influence * influence;
      float r = 0, g = 0, b = 0, a = 0;
      for (int i = 0; i < n; ++i) {
        const float offset = push * (1 - (n == 1 ? 0 : float(i) / (n - 1)) * smudge);
        const QRgb v = sample(s, x - std::cos(angle) * offset, y - std::sin(angle) * offset);
        r += qRed(v); g += qGreen(v); b += qBlue(v); a += qAlpha(v);
      }
      return qRgba(byte(r / n), byte(g / n), byte(b / n), byte(a / n));
    }
    if (dist <= .0001f) return s.pixel(x, y);
    const float norm = dist / radius;
    if (id == "twirl") {
      const float theta = std::atan2(dy, dx) - angle * (1 - norm);
      // Batch M2 requires bilinear warps; the older reference rounds these coordinates.
      return sample(s, cx + dist * std::cos(theta), cy + dist * std::sin(theta));
    }
    // Pinch/bulge uses batch-required bilinear sampling instead of reference rounding.
    const float scale = id == "fisheye_lens" ? std::pow(norm, 1 + strength * 2.4f) * radius / dist
        : 1 / std::max(.05f, 1 + strength * (1 - norm * norm));
    return sample(s, cx + dx * scale, cy + dy * scale);
  });
}

EffectResult patterned(const QString &id, const QImage &s, const ParamValues &p, const EffectContext &c) {
  const int w = s.width(), h = s.height();
  const float cx = floatOf(p, "center_x_percentage") / 100 * (w - 1), cy = floatOf(p, "center_y_percentage") / 100 * (h - 1);
  const float rotation = floatOf(p, "rotation") * pi / 180;
  const bool vertical = choiceOf(p, "orientation") == "vertical";
  if ((id == "cylinder_wrap" && floatOf(p, "curvature") == 0) ||
      (id == "fold_crease_warp" && floatOf(p, "fold_depth") == 0)) return identity(s, c);
  const float maxRadius = std::min(w - 1, h - 1) * .5f * floatOf(p, "radius_scale") / 100;
  if (id == "polar_warp" && maxRadius <= .5f) return identity(s, c);
  return pixels(s, s.size(), c, [&](int x, int y) -> QRgb {
    if (id == "glass_sphere") {
      const float radius = std::min(w, h) * floatOf(p, "radius") / 200;
      const float dx = x - w / 2.f, dy = y - h / 2.f, dist = std::hypot(dx, dy);
      if (dist >= radius) return s.pixel(x, y);
      const float theta = std::asin(dist / radius);
      const float newDist = radius * std::sin(theta * floatOf(p, "refraction")) / std::sin(theta + .0001f);
      const float ratio = dist > .0001f ? newDist / dist : 1;
      const float sx = w / 2.f + dx * ratio, sy = h / 2.f + dy * ratio;
      // Bilinear replaces the reference's truncated pixel picks, retaining its original-pixel fallback.
      return sx >= 0 && sx < w && sy >= 0 && sy < h ? sample(s, sx, sy) : s.pixel(x, y);
    }
    if (id == "wave_distortion") {
      const QString direction = choiceOf(p, "direction");
      const float freq = 2 * pi / intOf(p, "wavelength"), amp = intOf(p, "amplitude");
      // Bilinear coordinates retain subpixel wave displacement instead of truncating it.
      return sample(s, x + (direction == "vertical" ? 0 : amp * std::sin(y * freq)),
                       y + (direction == "horizontal" ? 0 : amp * std::sin(x * freq)), true);
    }
    if (id == "symmetry") {
      const QString d = choiceOf(p, "direction");
      const bool flipX = (d == "left_to_right" && x >= w / 2.f) || (d == "right_to_left" && x < w / 2.f);
      const bool flipY = (d == "top_to_bottom" && y >= h / 2.f) || (d == "bottom_to_top" && y < h / 2.f);
      if (!flipX && !flipY) return s.pixel(x, y);
      return over(sample(s, flipX ? w - 1 - x : x, flipY ? h - 1 - y : y), s.pixel(x, y));
    }
    if (id == "mirror_tiles" || id == "tile_repeat") {
      const int cols = intOf(p, "columns"), rows = intOf(p, "rows");
      if (id == "mirror_tiles" && cols == 1 && rows == 1 && !boolOf(p, "mirror_alternate_columns") && !boolOf(p, "mirror_alternate_rows")) return s.pixel(x, y);
      const float u = (x + .5f) / w * cols, v = (y + .5f) / h * rows;
      float lx = u - std::floor(u), ly = v - std::floor(v);
      if (boolOf(p, "mirror_alternate_columns") && (int(u) & 1)) lx = 1 - lx;
      if (boolOf(p, "mirror_alternate_rows") && (int(v) & 1)) ly = 1 - ly;
      // Tile repeat's canvas scaling uses pixel-boundary coordinates.
      return id == "tile_repeat" ? s.pixel(std::min(w - 1, int(lx * w)), std::min(h - 1, int(ly * h)))
                                 : sample(s, lx * (w - 1), ly * (h - 1));
    }
    if (id == "cylinder_wrap") {
      const float maxAngle = .12f + floatOf(p, "curvature") / 100 * 1.28f;
      const float range = std::max(1, (vertical ? w : h) - 1);
      const float projected = (vertical ? x : y) / range * 2 - 1;
      const float a = std::asin(std::clamp(projected * std::sin(maxAngle), -1.f, 1.f));
      const float coord = (a / maxAngle + 1) * .5f * range;
      return shade(sample(s, vertical ? coord : x, vertical ? y : coord),
                   1 - floatOf(p, "edge_shading") / 100 * (1 - std::cos(a)) * .65f);
    }
    if (id == "fold_crease_warp") {
      const float span = float(std::max(1, (vertical ? w : h) - 1)) / intOf(p, "fold_count");
      const float phase = (vertical ? x : y) / std::max(1.f, span) * pi;
      const float offset = std::sin(phase) * span * (.06f + floatOf(p, "fold_depth") / 100 * .22f);
      const float shadow = floatOf(p, "shadow_strength") / 100;
      return shade(sample(s, x + (vertical ? offset : 0), y + (vertical ? 0 : offset)),
                   1 - shadow * std::abs(std::sin(phase)) * .22f + shadow * std::cos(phase) * .08f);
    }
    if (id == "polar_warp") {
      if (choiceOf(p, "mode") == "cartesian_to_polar") {
        const float a = x / float(std::max(1, w - 1)) * 2 * pi + rotation;
        const float r = y / float(std::max(1, h - 1)) * maxRadius;
        return sample(s, cx + std::cos(a) * r, cy + std::sin(a) * r, true);
      }
      const float dx = x - cx, dy = y - cy, r = std::hypot(dx, dy);
      if (r > maxRadius) return 0;
      float a = std::fmod(std::atan2(dy, dx) - rotation, 2 * pi); if (a < 0) a += 2 * pi;
      return sample(s, a / (2 * pi) * std::max(1, w - 1), r / maxRadius * std::max(1, h - 1));
    }
    const float zoom = floatOf(p, "zoom") / 100;
    const float dx = (x - cx) / zoom, dy = (y - cy) / zoom, dist = std::hypot(dx, dy);
    float a = std::fmod(std::atan2(dy, dx) - rotation, 2 * pi); if (a < 0) a += 2 * pi;
    const float segment = 2 * pi / intOf(p, "segments");
    const int index = int(std::floor(a / segment));
    float local = a - index * segment; if (index & 1) local = segment - local;
    return sample(s, cx + std::cos(local + rotation) * dist, cy + std::sin(local + rotation) * dist);
  });
}

EffectResult pageCurl(const QImage &s, const ParamValues &p, const EffectContext &c) {
  const QString corner = choiceOf(p, "corner");
  const bool right = corner == "top_right" || corner == "bottom_right";
  const bool bottom = corner == "bottom_left" || corner == "bottom_right";
  const float size = std::max(12.f, std::min(s.width(), s.height()) * floatOf(p, "curl_size") / 100);
  const float depth = floatOf(p, "curl_depth") / 100, shadow = floatOf(p, "shadow_strength") / 100;
  const float fold = size * (.78f + depth * .22f), shadowWidth = std::max(4.f, size * (.08f + shadow * .20f + depth * .10f));
  return pixels(s, s.size(), c, [&](int x, int y) {
    const QRgb original = s.pixel(x, y);
    const float lx = right ? s.width() - 1 - x : x, ly = bottom ? s.height() - 1 - y : y;
    if (lx > size + shadowWidth || ly > size + shadowWidth) return original;
    const float diagonal = lx + ly;
    if (diagonal <= fold) {
      const float rx = fold - ly, ry = fold - lx;
      const QRgb reflected = sample(s, right ? s.width() - 1 - rx : rx, bottom ? s.height() - 1 - ry : ry);
      const float mix = .28f + depth * .28f + (1 - diagonal / fold) * .18f;
      const QRgb curled = blend(reflected, colorOf(p, "back_color").rgba(), mix);
      const float highlight = (.05f + depth * .09f) * smooth(fold * .55f, 0, diagonal);
      return blend(curled, qRgba(255, 255, 255, qAlpha(curled)), highlight);
    }
    if (diagonal < fold + shadowWidth) return shade(original, 1 - shadow * (1 - (diagonal - fold) / shadowWidth) * .42f);
    return original;
  });
}
EffectResult displacement(const QImage &s, const ParamValues &p, const EffectContext &c) {
  if (c.cancelled()) return {{}, kCancelled};
  const QImage map = loadImageFile(choiceOf(p, "map_file_path"));
  if (c.cancelled()) return {{}, kCancelled};
  if (map.isNull()) return {{}, kInvalidInput};
  return pixels(s, s.size(), c, [&](int x, int y) {
    const int mx = s.width() <= 1 ? 0 : int(std::nearbyint(float(x) * (map.width() - 1) / (s.width() - 1)));
    const int my = s.height() <= 1 ? 0 : int(std::nearbyint(float(y) * (map.height() - 1) / (s.height() - 1)));
    const QRgb m = map.pixel(mx, my);
    // Bilinear displacement follows M2's sampling requirement; the reference rounds source coordinates.
    return sample(s, x + (qRed(m) / 255.f - .5f) * 2 * floatOf(p, "amount_x"),
                     y + (qGreen(m) / 255.f - .5f) * 2 * floatOf(p, "amount_y"));
  });
}
EffectResult tileShuffle(const QImage &s, const ParamValues &p, const EffectContext &c) {
  const int cols = intOf(p, "columns"), rows = intOf(p, "rows");
  std::vector<int> indices(size_t(cols * rows)); std::iota(indices.begin(), indices.end(), 0);
  // Context seed replaces the reference's private fixed seed; shared SplitMix64 drives Fisher-Yates.
  SplitMix64 rng(c.seed);
  for (int i = int(indices.size()) - 1; i > 0; --i) std::swap(indices[size_t(i)], indices[size_t(rng.below(i + 1))]);
  const float tw = float(s.width()) / cols, th = float(s.height()) / rows, gap = floatOf(p, "gap");
  const QRgb fill = colorOf(p, "gap_color").rgba();
  return pixels(s, s.size(), c, [&](int x, int y) {
    const int col = std::min(cols - 1, int((x + .5f) / tw)), row = std::min(rows - 1, int((y + .5f) / th));
    const float lx = x + .5f - col * tw - gap * .5f, ly = y + .5f - row * th - gap * .5f;
    if (tw <= gap || th <= gap || lx < 0 || ly < 0 || lx >= tw - gap || ly >= th - gap) return fill;
    const int index = indices[size_t(row * cols + col)];
    // Batch-required bilinear tile sampling replaces Skia canvas nearest sampling.
    const QRgb v = sample(s, (index % cols) * tw + lx / (tw - gap) * tw - .5f,
                            (index / cols) * th + ly / (th - gap) * th - .5f);
    // Source-over the gap colour, like the reference canvas.
    return over(v, fill);
  });
}

QPolygonF corners(const QImage &s, bool indices = false) {
  const double w = s.width() - int(indices), h = s.height() - int(indices);
  return QPolygonF{QPointF(0, 0), QPointF(w, 0), QPointF(w, h), QPointF(0, h)};
}
EffectResult perspective(const QImage &s, const ParamValues &p, const EffectContext &c) {
  const QPolygonF src = corners(s, true);
  QPolygonF dst = src;
  const std::array<const char *, 4> xs{"top_left_x", "top_right_x", "bottom_right_x", "bottom_left_x"};
  const std::array<const char *, 4> ys{"top_left_y", "top_right_y", "bottom_right_y", "bottom_left_y"};
  for (int i = 0; i < 4; ++i) dst[i] += QPointF(intOf(p, xs[i]), intOf(p, ys[i]));
  if (src == dst) return identity(s, c);
  QTransform inverse;
  if (!QTransform::quadToQuad(dst, src, inverse)) return identity(s, c);
  // The reference deliberately retains the original output canvas dimensions.
  return pixels(s, s.size(), c, [&](int x, int y) -> QRgb {
    if (std::abs(inverse.m13() * x + inverse.m23() * y + inverse.m33()) < 1e-8) return 0;
    const QPointF at = inverse.map(QPointF(x, y));
    if (!std::isfinite(at.x()) || !std::isfinite(at.y()) || at.x() < 0 || at.y() < 0 ||
        at.x() > s.width() - 1 || at.y() > s.height() - 1) return 0;
    return s.pixel(int(std::nearbyint(at.x())), int(std::nearbyint(at.y())));
  });
}

// A 3D point is rotated X, Y, Z, projected with w=1-z/depth, then translated back.
QPolygonF project(const QImage &s, const ParamValues &p, float localZ) {
  const float ax = floatOf(p, "rotate_x") * pi / 180, ay = floatOf(p, "rotate_y") * pi / 180;
  const float az = floatOf(p, "rotate_z") * pi / 180, depth = std::max(s.width(), s.height()) * 2.f;
  QPolygonF out;
  for (const QPointF &at : corners(s)) {
    const float x = float(at.x()) - s.width() / 2.f, y = float(at.y()) - s.height() / 2.f;
    const float y1 = y * std::cos(ax) - localZ * std::sin(ax), z1 = y * std::sin(ax) + localZ * std::cos(ax);
    const float x2 = x * std::cos(ay) + z1 * std::sin(ay), z2 = -x * std::sin(ay) + z1 * std::cos(ay);
    const float x3 = x2 * std::cos(az) - y1 * std::sin(az), y3 = x2 * std::sin(az) + y1 * std::cos(az);
    out << QPointF(x3 / (1 - z2 / depth) + s.width() / 2.f, y3 / (1 - z2 / depth) + s.height() / 2.f);
  }
  return out;
}
EffectResult transformed(const QString &id, const QImage &s, const ParamValues &p, const EffectContext &c) {
  if (c.cancelled()) return {{}, kCancelled};
  QPolygonF front, back;
  const QPolygonF src = corners(s);
  if (id == "skew") {
    const float hx = std::tan(floatOf(p, "horizontally") * pi / 180), vy = std::tan(floatOf(p, "vertically") * pi / 180);
    if (hx == 0 && vy == 0) return identity(s, c);
    for (const QPointF &at : src) front << QPointF(at.x() + hx * at.y(), at.y() + vy * at.x());
  } else {
    const bool noRotation = floatOf(p, "rotate_x") == 0 && floatOf(p, "rotate_y") == 0 && floatOf(p, "rotate_z") == 0;
    if (noRotation && (id == "rotate_3d" || floatOf(p, "depth") == 0)) return identity(s, c);
    front = project(s, p, 0);
    if (id == "rotate_3d_box") back = project(s, p, -floatOf(p, "depth"));
  }
  QRectF bounds = front.boundingRect(); if (!back.isEmpty()) bounds = bounds.united(back.boundingRect());
  const bool expand = boolOf(p, "auto_resize");
  const double dw = expand ? std::ceil(bounds.width()) : s.width(), dh = expand ? std::ceil(bounds.height()) : s.height();
  if (!std::isfinite(dw) || !std::isfinite(dh) || dw > ed::kMaxDimension || dh > ed::kMaxDimension || dw * dh > ed::kMaxPixels)
    return {{}, kInvalidInput};
  const QSize size(std::max(1, int(dw)), std::max(1, int(dh)));
  const QPointF offset = expand ? bounds.topLeft() : QPointF();
  QImage faces(size, QImage::Format_ARGB32); if (faces.isNull()) return {{}, kFailed}; faces.fill(Qt::transparent);
  if (!back.isEmpty()) {
    QPainter painter(&faces); painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-offset);
    const auto visible = [&](int a, int b) {
      const QPointF u = front[b] - front[a], v = back[a] - front[a];
      return u.x() * v.y() - u.y() * v.x() > 0;
    };
    const auto face = [&](int a, int b, QRgb edge, float k) {
      if (c.cancelled()) return;
      const QColor color(int(qRed(edge) * k), int(qGreen(edge) * k), int(qBlue(edge) * k));
      painter.setBrush(color); painter.setPen(QPen(color, 1));
      painter.drawPolygon(QPolygonF{front[a], front[b], back[b], back[a]});
    };
    // Qt path coverage approximates Skia's antialiasing of the solid side faces.
    if (!visible(0, 1)) face(0, 1, s.pixel(s.width() / 2, 0), .85f);
    else face(3, 2, s.pixel(s.width() / 2, s.height() - 1), .85f);
    if (visible(0, 3)) face(0, 3, s.pixel(0, s.height() / 2), .7f);
    else face(1, 2, s.pixel(s.width() - 1, s.height() / 2), .7f);
  }
  QTransform inverse;
  if (!QTransform::quadToQuad(front, src, inverse)) return pixels(s, size, c, [&](int x, int y) { return faces.pixel(x, y); });
  return pixels(s, size, c, [&](int x, int y) {
    const QPointF dest = QPointF(x + .5, y + .5) + offset;
    if (std::abs(inverse.m13() * dest.x() + inverse.m23() * dest.y() + inverse.m33()) < 1e-8) return faces.pixel(x, y);
    const QPointF at = inverse.map(dest);
    if (!std::isfinite(at.x()) || !std::isfinite(at.y())) return faces.pixel(x, y);
    if (at.x() < 0 || at.y() < 0 || at.x() >= s.width() || at.y() >= s.height()) return faces.pixel(x, y);
    if (id != "rotate_3d_box") return s.pixel(int(at.x()), int(at.y()));
    // Bilinear front-face sampling approximates Skia Catmull-Rom for extruded boxes.
    const QRgb v = sample(s, float(at.x() - .5), float(at.y() - .5));
    const QRgb bg = faces.pixel(x, y);
    return over(v, bg);
  });
}

void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference,
         QVector<ParamSpec> parameters, ApplyFn fn, bool changesSize = false, bool usesSeed = false) {
  EffectSpec spec{id, name, Category::Manipulations, Mode::Previewable, std::move(parameters), changesSize, usesSeed,
      "ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Manipulations/" + reference};
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}
}  // namespace

void registerManipulationEffects2(EffectRegistry &registry) {
  add(registry, "auto_crop_image", "Auto crop image", "AutoCropImageEffect.cs",
      {intParam("tolerance", "Tolerance", 0, 0, 255)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return canvasEffect("auto_crop_image", s, p, c); }, true);
  add(registry, "crop_image", "Crop image", "CropImageEffect.cs",
      {intParam("x", "X", 0, 0, 100000),
       intParam("y", "Y", 0, 0, 100000),
       intParam("width", "Width", 100, 1, 100000),
       intParam("height", "Height", 100, 1, 100000)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return canvasEffect("crop_image", s, p, c); }, true);
  add(registry, "cylinder_wrap", "Cylinder wrap", "CylinderWrapImageEffect.cs",
      {enumParam("orientation", "Orientation", "vertical", {"vertical", "horizontal"}),
       floatParam("curvature", "Curvature", 65, 0, 100),
       floatParam("edge_shading", "Edge shading", 35, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("cylinder_wrap", s, p, c); }, false);
  add(registry, "displacement_map", "Displacement map", "DisplacementMapImageEffect.cs",
      {filePathParam("map_file_path", "Map file path"),
       floatParam("amount_x", "Amount X", 20, -200, 200),
       floatParam("amount_y", "Amount Y", 20, -200, 200)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return displacement(s, p, c); }, false);
  add(registry, "fisheye_lens", "Fisheye lens", "FisheyeLensImageEffect.cs",
      {floatParam("strength", "Strength", 58, 0, 100),
       floatParam("radius_percentage", "Radius percentage", 100, 1, 100),
       floatParam("center_x_percentage", "Center X percentage", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y percentage", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return radial("fisheye_lens", s, p, c); }, false);
  add(registry, "fold_crease_warp", "Fold crease warp", "FoldCreaseWarpImageEffect.cs",
      {enumParam("orientation", "Orientation", "vertical", {"vertical", "horizontal"}),
       intParam("fold_count", "Fold count", 3, 1, 10),
       floatParam("fold_depth", "Fold depth", 30, 0, 100),
       floatParam("shadow_strength", "Shadow strength", 40, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("fold_crease_warp", s, p, c); }, false);
  add(registry, "glass_sphere", "Glass sphere", "GlassSphereImageEffect.cs",
      {floatParam("radius", "Radius %", 50, 10, 100),
       floatParam("refraction", "Refraction", 1.5, 0.1, 2.0)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("glass_sphere", s, p, c); }, false);
  add(registry, "kaleidoscope", "Kaleidoscope", "KaleidoscopeImageEffect.cs",
      {intParam("segments", "Segments", 8, 2, 24),
       floatParam("rotation", "Rotation", 0, 0, 360),
       floatParam("zoom", "Zoom", 100, 20, 300),
       floatParam("center_x_percentage", "Center X percentage", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y percentage", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("kaleidoscope", s, p, c); }, false);
  add(registry, "liquify_push_smudge", "Liquify push / smudge", "LiquifyPushSmudgeImageEffect.cs",
      {floatParam("angle", "Angle", 0, 0, 360),
       floatParam("distance", "Distance", 40, -200, 200),
       floatParam("radius_percentage", "Radius percentage", 25, 1, 100),
       floatParam("smudge", "Smudge", 35, 0, 100),
       floatParam("center_x_percentage", "Center X percentage", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y percentage", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return radial("liquify_push_smudge", s, p, c); }, false);
  add(registry, "mirror_tiles", "Mirror tiles", "MirrorTilesImageEffect.cs",
      {intParam("columns", "Columns", 3, 1, 16),
       intParam("rows", "Rows", 3, 1, 16),
       boolParam("mirror_alternate_columns", "Mirror alternate columns", true),
       boolParam("mirror_alternate_rows", "Mirror alternate rows", true)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("mirror_tiles", s, p, c); }, false);
  add(registry, "page_curl", "Page curl", "PageCurlImageEffect.cs",
      {enumParam("corner", "Corner", "bottom_right", {"top_left", "top_right", "bottom_right", "bottom_left"}),
       floatParam("curl_size", "Curl size", 28, 5, 80),
       floatParam("curl_depth", "Curl depth", 55, 0, 100),
       floatParam("shadow_strength", "Shadow strength", 60, 0, 100),
       colorParam("back_color", "Back color", QColor(248, 244, 236))},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return pageCurl(s, p, c); }, false);
  add(registry, "perspective_warp", "Perspective warp", "PerspectiveWarpImageEffect.cs",
      {intParam("top_left_x", "Top left X", 0, -500, 500),
       intParam("top_left_y", "Top left Y", 0, -500, 500),
       intParam("top_right_x", "Top right X", 0, -500, 500),
       intParam("top_right_y", "Top right Y", 0, -500, 500),
       intParam("bottom_right_x", "Bottom right X", 0, -500, 500),
       intParam("bottom_right_y", "Bottom right Y", 0, -500, 500),
       intParam("bottom_left_x", "Bottom left X", 0, -500, 500),
       intParam("bottom_left_y", "Bottom left Y", 0, -500, 500)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return perspective(s, p, c); }, false);
  add(registry, "pinch_bulge", "Pinch / bulge", "PinchBulgeImageEffect.cs",
      {floatParam("strength", "Strength", 35, -100, 100),
       floatParam("radius_percentage", "Radius %", 50, 1, 100),
       floatParam("center_x_percentage", "Center X %", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y %", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return radial("pinch_bulge", s, p, c); }, false);
  add(registry, "polar_warp", "Polar warp", "PolarWarpImageEffect.cs",
      {enumParam("mode", "Mode", "cartesian_to_polar", {"cartesian_to_polar", "polar_to_cartesian"}),
       floatParam("rotation", "Rotation", 0, -360, 360),
       floatParam("radius_scale", "Radius scale", 100, 20, 200),
       floatParam("center_x_percentage", "Center X %", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y %", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("polar_warp", s, p, c); }, false);
  add(registry, "resize_canvas", "Resize canvas", "ResizeCanvasImageEffect.cs",
      {intParam("top", "Top", 0, -10000, 10000),
       intParam("right", "Right", 0, -10000, 10000),
       intParam("bottom", "Bottom", 0, -10000, 10000),
       intParam("left", "Left", 0, -10000, 10000),
       colorParam("background_color", "Background color", QColor(0, 0, 0, 0))},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return canvasEffect("resize_canvas", s, p, c); }, true);
  add(registry, "resize_image", "Resize image", "ResizeImageEffect.cs",
      {intParam("width", "Width", 0, 0, 10000),
       intParam("height", "Height", 0, 0, 10000),
       boolParam("maintain_aspect_ratio", "Maintain aspect ratio", false)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return canvasEffect("resize_image", s, p, c); }, true);
  add(registry, "ripple_refraction", "Ripple refraction", "RippleRefractionImageEffect.cs",
      {floatParam("amplitude", "Amplitude", 12, 0, 80),
       floatParam("wavelength", "Wavelength", 32, 4, 400),
       floatParam("phase", "Phase", 0, -360, 360),
       floatParam("refraction", "Refraction", 35, 0, 100),
       floatParam("center_x_percentage", "Center X %", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y %", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return radial("ripple_refraction", s, p, c); }, false);
  add(registry, "rotate_3d", "Rotate 3D", "Rotate3DImageEffect.cs",
      {floatParam("rotate_x", "Rotate X", 0, -180, 180),
       floatParam("rotate_y", "Rotate Y", 0, -180, 180),
       floatParam("rotate_z", "Rotate Z", 0, -180, 180),
       boolParam("auto_resize", "Auto resize", true)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return transformed("rotate_3d", s, p, c); }, true);
  add(registry, "rotate_3d_box", "3D Box / Extrude", "Rotate3DBoxImageEffect.cs",
      {floatParam("depth", "Depth", 50, 0, 500),
       floatParam("rotate_x", "Rotate X", 0, -180, 180),
       floatParam("rotate_y", "Rotate Y", 0, -180, 180),
       floatParam("rotate_z", "Rotate Z", 0, -180, 180),
       boolParam("auto_resize", "Auto resize", true)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return transformed("rotate_3d_box", s, p, c); }, true);
  add(registry, "rotate_custom_angle", "Rotate", "RotateImageEffect.cs",
      {floatParam("angle", "Angle", 0, -360, 360),
       boolParam("auto_resize", "Auto resize", true)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return canvasEffect("rotate_custom_angle", s, p, c); }, true);
  add(registry, "skew", "Skew", "SkewImageEffect.cs",
      {floatParam("horizontally", "Horizontally", 0, -89, 89),
       floatParam("vertically", "Vertically", 0, -89, 89),
       boolParam("auto_resize", "Auto resize", true)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return transformed("skew", s, p, c); }, true);
  add(registry, "symmetry", "Symmetry", "SymmetryImageEffect.cs",
      {enumParam("direction", "Direction", "left_to_right", {"left_to_right", "right_to_left", "top_to_bottom", "bottom_to_top"})},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("symmetry", s, p, c); }, false);
  add(registry, "tile_repeat", "Tile repeat", "TileRepeatImageEffect.cs",
      {intParam("columns", "Columns", 3, 2, 10),
       intParam("rows", "Rows", 3, 2, 10)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("tile_repeat", s, p, c); }, false);
  add(registry, "tile_shuffle", "Tile Shuffle", "TileShuffleImageEffect.cs",
      {intParam("columns", "Columns", 4, 2, 20),
       intParam("rows", "Rows", 4, 2, 20),
       floatParam("gap", "Gap", 0, 0, 10),
       colorParam("gap_color", "Gap Color", QColor(0, 0, 0, 255))},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return tileShuffle(s, p, c); }, false, true);
  add(registry, "twirl", "Twirl", "TwirlImageEffect.cs",
      {floatParam("angle", "Angle", 90, -360, 360),
       floatParam("radius_percentage", "Radius %", 50, 1, 100),
       floatParam("center_x_percentage", "Center X %", 50, 0, 100),
       floatParam("center_y_percentage", "Center Y %", 50, 0, 100)},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return radial("twirl", s, p, c); }, false);
  add(registry, "wave_distortion", "Wave distortion", "WaveDistortionImageEffect.cs",
      {intParam("amplitude", "Amplitude", 20, 1, 100),
       intParam("wavelength", "Wavelength", 100, 10, 500),
       enumParam("direction", "Direction", "horizontal", {"horizontal", "vertical", "both"})},
      [](const QImage &s, const ParamValues &p, const EffectContext &c) { return patterned("wave_distortion", s, p, c); }, false);
}
}  // namespace xerahs::effects
