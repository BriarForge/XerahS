// EDITOR-EFFECTS-001: Filters batch F2. EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); census paths are
// EffectSpec::referencePath.
//
// Declared deviations (EE-018):
//  - SKImageFilter.CreateBlur is a normalised separable Gaussian (3-sigma) on
//    premultiplied pixels. Blur and Gaussian blur still clamp-pad first.
//  - Film grain, glitch shift and slice take SplitMix64(context.seed) instead of
//    System.Random(42), the glitch Seed property, or Random.Shared.
//  - Add noise keeps the reference hash and reads context.seed in place of Seed 1337.
//  - Stripe coverage is the pixel centre inside the rotated rect (no antialiasing).
//  - Colour-halftone dots are hard-edged; Multiply is the W3C formula. The reference
//    computes a rotation and then ignores it; this port does the same.
//  - Diamond, hex and triangle tiles use QPainter's non-antialiased fill.
//  - Pop-art cells use nearest pixel-centre sampling.
//  - Screen, Overlay and SrcIn use the W3C blend formulas in straight alpha.
//  - Retro-sunset stops are lerped in straight colour; Skia gradients are premultiplied.
#include "Effect.h"
#include "EffectSupport.h"

#include <QColorSpace>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;
const QString kFilters = QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");
constexpr float kPi = 3.14159265358979323846f;

int truncByte(float v) {
  if (!(v > 0.f)) return 0;
  if (v >= 255.f) return 255;
  return int(v);
}
int mulDiv255(int a, int b) { return (a * b + 127) / 255; }
int clampCoord(int v, int limit) { return v < 0 ? 0 : (v >= limit ? limit - 1 : v); }
int roundAway(float v) {
  if (std::isnan(v)) return 0;
  const float rounded = v >= 0.f ? std::floor(v + 0.5f) : std::ceil(v - 0.5f);
  if (rounded <= 0.f) return 0;
  if (rounded >= 255.f) return 255;
  return int(rounded);
}

QRgb blendOver(QRgb dst, float sr, float sg, float sb, float as, bool overlay) {
  const float ab = qAlpha(dst) / 255.f;
  const float ar = as + ab * (1.f - as);
  if (ar <= 0.f) return 0;
  const float cb[3]{qRed(dst) / 255.f, qGreen(dst) / 255.f, qBlue(dst) / 255.f};
  const float cs[3]{sr, sg, sb};
  int out[3];
  for (int c = 0; c < 3; ++c) {
    const float blended = overlay ? (cb[c] <= 0.5f ? cs[c] * 2.f * cb[c]
                                                   : cs[c] + (2.f * cb[c] - 1.f) - cs[c] * (2.f * cb[c] - 1.f))
                                  : 1.f - (1.f - cb[c]) * (1.f - cs[c]);
    const float value = ((1.f - ab) * as * cs[c] + (1.f - as) * ab * cb[c] + as * ab * blended) / ar;
    out[c] = int(std::nearbyintf(std::clamp(value, 0.f, 1.f) * 255.f));
  }
  return qRgba(out[0], out[1], out[2], int(std::nearbyintf(ar * 255.f)));
}
QRgb screenOver(QRgb dst, int r, int g, int b, int a) {
  return blendOver(dst, r / 255.f, g / 255.f, b / 255.f, a / 255.f, false);
}
QRgb overlayOver(QRgb dst, int r, int g, int b, int a) {
  return blendOver(dst, r / 255.f, g / 255.f, b / 255.f, a / 255.f, true);
}

const QRgb *rowAt(const QImage &image, int y) { return reinterpret_cast<const QRgb *>(image.constScanLine(y)); }
QRgb *rowAt(QImage &image, int y) { return reinterpret_cast<QRgb *>(image.scanLine(y)); }

EffectResult copyImage(const QImage &source, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, source.height(), 0, 100, [&](int y) {
        std::memcpy(result.scanLine(y), source.constScanLine(y), size_t(source.width()) * 4);
      }))
    return {{}, kCancelled};
  return {result, {}};
}

QImage blurExpanded(const QImage &source, float sigma, int pad, const EffectContext &context, int lo, int hi, bool &ok) {
  const int w = source.width(), h = source.height();
  const int span = std::max(1, hi - lo);
  auto at = [&](int index) { return lo + span * index / 3; };
  QImage expanded(w + pad * 2, h + pad * 2, QImage::Format_ARGB32);
  if (expanded.isNull()) {
    ok = false;
    return {};
  }
  expanded.setColorSpace(source.colorSpace());
  if (!runRows(context, expanded.height(), lo, at(1), [&](int y) {
        const int sy = clampCoord(y - pad, h);
        const QRgb *src = rowAt(source, sy);
        QRgb *dst = rowAt(expanded, y);
        for (int x = 0; x < expanded.width(); ++x) dst[x] = src[clampCoord(x - pad, w)];
      })) {
    ok = false;
    return {};
  }
  QImage blurred = approximateGaussianBlur(expanded, sigma, BlurBorder::Decal, context, at(1), at(2));
  if (blurred.isNull()) {
    ok = false;
    return {};
  }
  QImage cropped = makeResult(source);
  if (cropped.isNull() || !runRows(context, h, at(2), hi, [&](int y) {
        std::memcpy(cropped.scanLine(y), blurred.constScanLine(y + pad) + qsizetype(pad) * 4, size_t(w) * 4);
      })) {
    ok = false;
    return {};
  }
  ok = true;
  return cropped;
}

void rgbToHslPixel(QRgb p, float &h, float &s, float &l) { rgbToHsl(qRed(p), qGreen(p), qBlue(p), h, s, l); }

EffectResult colorSplash(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float target = float(intOf(params, "hue"));
  const float tolerance = float(intOf(params, "tolerance"));
  return mapPixels(source, context, [&](int, int, QRgb p) {
    float h, s, l;
    rgbToHslPixel(p, h, s, l);
    float diff = std::fabs(h - target);
    if (diff > 180.f) diff = 360.f - diff;
    if (diff <= tolerance && s > 5.f) return p;
    const int gray = truncByte(0.2126f * qRed(p) + 0.7152f * qGreen(p) + 0.0722f * qBlue(p));
    return qRgba(gray, gray, gray, qAlpha(p));
  });
}

QRgb heatColor(float t, int alpha) {
  int r = 0, g = 0, b = 0;
  if (t < 0.2f) {
    b = truncByte((t / 0.2f) * 255.f);
  } else if (t < 0.4f) {
    g = truncByte(((t - 0.2f) / 0.2f) * 255.f);
    b = 255;
  } else if (t < 0.6f) {
    const float s = (t - 0.4f) / 0.2f;
    g = 255;
    b = truncByte((1.f - s) * 255.f);
  } else if (t < 0.8f) {
    r = truncByte(((t - 0.6f) / 0.2f) * 255.f);
    g = 255;
  } else {
    const float s = (t - 0.8f) / 0.2f;
    r = 255;
    g = truncByte((1.f - s) * 255.f);
    b = truncByte(s * 255.f);
  }
  return qRgba(r, g, b, alpha);
}
EffectResult heatMap(const QImage &source, const ParamValues &, const EffectContext &context) {
  return mapPixels(source, context, [](int, int, QRgb p) {
    const float lum = (0.2126f * qRed(p) + 0.7152f * qGreen(p) + 0.0722f * qBlue(p)) / 255.f;
    return heatColor(lum, qAlpha(p));
  });
}

EffectResult dreamGlow(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float alpha = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  const int radius = intOf(params, "radius");
  if (alpha <= 0.f || radius <= 0) return copyImage(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  QImage blurred = approximateGaussianBlur(source, float(radius), BlurBorder::Decal, context, 0, 70);
  if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
  const int paintA = truncByte(255.f * alpha);
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const QRgb b = rowAt(blurred, y)[x];
    return screenOver(p, qRed(b), qGreen(b), qBlue(b), mulDiv255(qAlpha(b), paintA));
  }, 70, 100);
}

EffectResult radialRainbow(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float strength = std::clamp(floatOf(params, "strength") / 100.f, 0.f, 1.f);
  if (strength <= 0.f) return copyImage(source, context);
  const int rotations = std::max(1, intOf(params, "rotations"));
  const float cx = source.width() / 2.f, cy = source.height() / 2.f;
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const float angle = std::atan2(y - cy, x - cx);
    const float hueShift = ((angle + kPi) / (2.f * kPi)) * 360.f * float(rotations);
    float h, s, l;
    rgbToHslPixel(p, h, s, l);
    float hue = std::fmod(h + hueShift * strength, 360.f);
    if (hue < 0.f) hue += 360.f;
    return hslToRgb(hue, s, l, qAlpha(p));
  });
}

template <class Build>
EffectResult paintTiles(const QImage &source, const EffectContext &context, Build build) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  result.fill(Qt::transparent);
  std::vector<std::pair<QPainterPath, QRgb>> tiles;
  build(tiles);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  const int count = int(tiles.size());
  const int height = std::max(source.height(), 1);
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    for (int i = 0; i < count; ++i) {
      if (i * height / std::max(count, 1) != y) continue;
      painter.setBrush(QColor::fromRgba(tiles[size_t(i)].second));
      painter.drawPath(tiles[size_t(i)].first);
    }
  });
  painter.end();
  result.setColorSpace(source.colorSpace());
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

EffectResult diamondPixelate(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int size = std::max(4, intOf(params, "size"));
  const int half = size / 2;
  const int w = source.width(), h = source.height();
  return paintTiles(source, context, [&](std::vector<std::pair<QPainterPath, QRgb>> &tiles) {
    for (int cy = 0; cy < h + size; cy += half) {
      for (int cx = 0; cx < w + size; cx += size) {
        const int dx = cx + (((cy / half) % 2 == 0) ? 0 : half);
        const QRgb color = rowAt(source, clampCoord(cy, h))[clampCoord(dx, w)];
        QPainterPath path;
        path.moveTo(dx, cy - half);
        path.lineTo(dx + half, cy);
        path.lineTo(dx, cy + half);
        path.lineTo(dx - half, cy);
        path.closeSubpath();
        tiles.emplace_back(path, color);
      }
    }
  });
}

EffectResult ghostTrail(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int copies = std::max(1, intOf(params, "copies"));
  const int ox = intOf(params, "offset_x"), oy = intOf(params, "offset_y");
  const float fade = std::clamp(floatOf(params, "fade") / 100.f, 0.1f, 0.9f);
  const int w = source.width(), h = source.height();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  result.fill(Qt::transparent);
  auto stamp = [&](int step, int paintA, int lo, int hi) {
    return runRows(context, h, lo, hi, [&](int y) {
      QRgb *dst = rowAt(result, y);
      const int sy = y - oy * step;
      for (int x = 0; x < w; ++x) {
        const int sx = x - ox * step;
        if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
        const QRgb s = rowAt(source, sy)[sx];
        const int sa = mulDiv255(qAlpha(s), paintA);
        dst[x] = sourceOverTop(qRgba(qRed(s), qGreen(s), qBlue(s), sa), dst[x]);
      }
    });
  };
  const int passes = copies + 1;
  for (int i = copies; i >= 1; --i) {
    const int paintA = truncByte(255.f * std::pow(1.f - fade, float(i)));
    const int index = copies - i;
    if (!stamp(i, paintA, index * 100 / passes, (index + 1) * 100 / passes)) return {{}, kCancelled};
  }
  if (!stamp(0, 255, copies * 100 / passes, 100)) return {{}, kCancelled};
  return {result, {}};
}

EffectResult filmGrain(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int range = int(floatOf(params, "amount") * 2.55f);
  const bool mono = boolOf(params, "monochrome");
  SplitMix64 rng(context.seed);
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, source.height(), 0, 100, [&](int y) {
        const QRgb *in = rowAt(source, y);
        QRgb *out = rowAt(result, y);
        for (int x = 0; x < source.width(); ++x) {
          auto noise = [&] { return rng.range(-range, range + 1); };
          const int n = noise();
          const int r = mono ? n : n;
          const int g = mono ? n : noise();
          const int b = mono ? n : noise();
          out[x] = qRgba(std::clamp(qRed(in[x]) + r, 0, 255), std::clamp(qGreen(in[x]) + g, 0, 255),
                         std::clamp(qBlue(in[x]) + b, 0, 255), qAlpha(in[x]));
        }
      }))
    return {{}, kCancelled};
  return {result, {}};
}

EffectResult glitchShift(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (intOf(params, "intensity") <= 0) return copyImage(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const int w = source.width(), h = source.height();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, h, 0, 20, [&](int y) { std::memcpy(result.scanLine(y), source.constScanLine(y), size_t(w) * 4); }))
    return {{}, kCancelled};
  SplitMix64 rng(context.seed);
  const int maxShift = w * intOf(params, "intensity") / 100;
  const int bands = std::max(2, intOf(params, "bands"));
  for (int b = 0; b < bands; ++b) {
    if (context.cancelled()) return {{}, kCancelled};
    const int bandStart = rng.range(0, h);
    const int bandHeight = std::min(rng.range(2, std::max(3, h / bands)), h - bandStart);
    const int shift = rng.range(-maxShift, maxShift + 1);
    for (int y = bandStart; y < bandStart + bandHeight; ++y) {
      if (context.cancelled()) return {{}, kCancelled};
      const QRgb *src = rowAt(source, y);
      QRgb *dst = rowAt(result, y);
      for (int x = 0; x < w; ++x) {
        const int sx = x - shift;
        dst[x] = (sx >= 0 && sx < w) ? src[sx] : qRgba(0, 0, 0, 255);
      }
    }
    context.progress(20 + (b + 1) * 80 / bands);
  }
  return {result, {}};
}

uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}
EffectResult addNoise(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = std::clamp(floatOf(params, "amount"), 0.f, 100.f);
  const int amplitude = int(std::nearbyintf(amount * 1.27f));
  if (amplitude <= 0) return copyImage(source, context);
  const uint32_t seed = uint32_t(context.seed) * 747796405u;
  const int w = source.width();
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    uint32_t h = hash32((uint32_t(y) * uint32_t(w) + uint32_t(x)) ^ seed);
    const int r = ((int(h & 0xFF) - 128) * amplitude) / 128;
    h = hash32(h ^ 0x9E3779B9u);
    const int g = ((int(h & 0xFF) - 128) * amplitude) / 128;
    h = hash32(h ^ 0x85EBCA6Bu);
    const int b = ((int(h & 0xFF) - 128) * amplitude) / 128;
    return qRgba(std::clamp(qRed(p) + r, 0, 255), std::clamp(qGreen(p) + g, 0, 255), std::clamp(qBlue(p) + b, 0, 255),
                 qAlpha(p));
  });
}

EffectResult hexagonalPixelate(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int size = std::max(4, intOf(params, "size"));
  const int w = source.width(), h = source.height();
  const float hexH = float(size) * std::sqrt(3.f);
  const float hexW = float(size) * 2.f;
  return paintTiles(source, context, [&](std::vector<std::pair<QPainterPath, QRgb>> &tiles) {
    for (int row = -1; float(row) * hexH / 2.f < float(h) + hexH; ++row) {
      const float yCenter = float(row) * hexH / 2.f;
      const float xOffset = (row % 2 == 0) ? 0.f : float(size) * 1.5f;
      for (int col = -1; float(col) * hexW * 0.75f < float(w) + hexW; ++col) {
        const float xCenter = float(col) * float(size) * 3.f + xOffset;
        const QRgb color = rowAt(source, clampCoord(int(yCenter), h))[clampCoord(int(xCenter), w)];
        QPainterPath path;
        for (int i = 0; i < 6; ++i) {
          const float angle = kPi / 3.f * float(i) - kPi / 6.f;
          const float x = xCenter + float(size) * std::cos(angle);
          const float y = yCenter + float(size) * std::sin(angle);
          if (i == 0) path.moveTo(x, y);
          else path.lineTo(x, y);
        }
        path.closeSubpath();
        tiles.emplace_back(path, color);
      }
    }
  });
}

EffectResult stripeOverlay(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float alpha = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  if (alpha <= 0.f) return copyImage(source, context);
  const QColor color = colorOf(params, "color");
  const int stripe = intOf(params, "width");
  const int period = std::max(4, stripe + intOf(params, "gap"));
  const float angle = float(intOf(params, "angle")) * kPi / 180.f;
  const int w = source.width(), h = source.height();
  const float diagonal = std::sqrt(float(w) * w + float(h) * h);
  const int count = int(diagonal / float(period)) + 2;
  const float startX = -diagonal / 2.f + w / 2.f;
  const float startY = -diagonal / 2.f + h / 2.f;
  const float cx = w / 2.f, cy = h / 2.f;
  const float c = std::cos(-angle), s = std::sin(-angle);
  const int paintA = truncByte(float(color.alpha()) * alpha);
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
    const float lx = c * dx - s * dy + cx;
    const float ly = s * dx + c * dy + cy;
    if (ly < startY || ly >= startY + diagonal * 2.f) return p;
    for (int i = 0; i < count; ++i) {
      const float left = startX + float(i) * float(period);
      if (lx >= left && lx < left + float(stripe)) return sourceOverTop(qRgba(color.red(), color.green(), color.blue(), paintA), p);
    }
    return p;
  });
}

EffectResult gaussianBlur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int radius = std::max(1, intOf(params, "radius"));
  bool ok = false;
  QImage blurred = blurExpanded(source, radius / 3.f, radius * 2, context, 0, 100, ok);
  if (!ok || blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
  return {blurred, {}};
}
EffectResult blur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (intOf(params, "radius") <= 0) return copyImage(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const int radius = intOf(params, "radius");
  bool ok = false;
  QImage blurred = blurExpanded(source, float(radius), radius * 2, context, 0, 100, ok);
  if (!ok || blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
  return {blurred, {}};
}

EffectResult trianglePixelate(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int size = std::max(8, intOf(params, "size"));
  const int w = source.width(), h = source.height();
  const float half = size / 2.f;
  return paintTiles(source, context, [&](std::vector<std::pair<QPainterPath, QRgb>> &tiles) {
    for (int row = 0; float(row) * half < float(h) + float(size); ++row) {
      for (int col = 0; col * size < w + size; ++col) {
        const float x = float(col * size), y = float(row) * half;
        const bool flipped = (row + col) % 2 == 0;
        QPointF p1, p2, p3;
        if (flipped) {
          p1 = {x, y};
          p2 = {x + size, y};
          p3 = {x + size / 2.f, y + half};
        } else {
          p1 = {x, y + half};
          p2 = {x + size, y + half};
          p3 = {x + size / 2.f, y};
        }
        const float cx = (p1.x() + p2.x() + p3.x()) / 3.f;
        const float cy = (p1.y() + p2.y() + p3.y()) / 3.f;
        const QRgb color = rowAt(source, clampCoord(int(std::trunc(cy)), h))[clampCoord(int(std::trunc(cx)), w)];
        QPainterPath path;
        path.moveTo(p1);
        path.lineTo(p2);
        path.lineTo(p3);
        path.closeSubpath();
        tiles.emplace_back(path, color);
      }
    }
  });
}

EffectResult convolutionMatrix(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float kernel[9]{float(intOf(params, "x0y0")), float(intOf(params, "x1y0")), float(intOf(params, "x2y0")),
                        float(intOf(params, "x0y1")), float(intOf(params, "x1y1")), float(intOf(params, "x2y1")),
                        float(intOf(params, "x0y2")), float(intOf(params, "x1y2")), float(intOf(params, "x2y2"))};
  const float gain = 1.f / std::max(0.01f, floatOf(params, "factor"));
  const float bias = float(intOf(params, "offset"));
  const int w = source.width(), h = source.height();
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    float sum[3]{0, 0, 0};
    int k = 0;
    for (int ky = -1; ky <= 1; ++ky) {
      const QRgb *row = rowAt(source, clampCoord(y + ky, h));
      for (int kx = -1; kx <= 1; ++kx, ++k) {
        const QRgb s = row[clampCoord(x + kx, w)];
        sum[0] += kernel[k] * qRed(s);
        sum[1] += kernel[k] * qGreen(s);
        sum[2] += kernel[k] * qBlue(s);
      }
    }
    return qRgba(roundAway(sum[0] * gain + bias), roundAway(sum[1] * gain + bias), roundAway(sum[2] * gain + bias), qAlpha(p));
  });
}

EffectResult retroSunset(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float alpha = std::clamp(floatOf(params, "strength") / 100.f, 0.f, 1.f);
  if (alpha <= 0.f) return copyImage(source, context);
  const bool lines = boolOf(params, "horizontal_lines");
  const int h = source.height(), w = source.width();
  const int gradA = truncByte(200.f * alpha);
  const int lineA = truncByte(60.f * alpha);
  const int spacing = std::max(3, h / 80);
  // Straight-channel stops; Skia's linear gradient interpolates premultiplied colours.
  const struct Stop { float at; int r, g, b; } stops[4]{{0.f, 120, 0, 180}, {0.33f, 220, 20, 80}, {0.66f, 255, 100, 0}, {1.f, 255, 200, 0}};
  return mapPixels(source, context, [&](int, int y, QRgb p) {
    const float t = std::clamp((y + 0.5f) / float(h), 0.f, 1.f);
    int index = 0;
    while (index < 2 && t > stops[index + 1].at) ++index;
    const float span = stops[index + 1].at - stops[index].at;
    const float local = span > 0.f ? (t - stops[index].at) / span : 0.f;
    auto mix = [&](int a, int b) { return int(std::nearbyintf(a + (b - a) * local)); };
    QRgb out = overlayOver(p, mix(stops[index].r, stops[index + 1].r), mix(stops[index].g, stops[index + 1].g),
                           mix(stops[index].b, stops[index + 1].b), gradA);
    if (lines && spacing > 0 && y % spacing == 0) out = sourceOverTop(qRgba(0, 0, 0, lineA), out);
    (void)w;
    return out;
  });
}

EffectResult motionBlur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int distance = std::clamp(intOf(params, "distance"), 1, 200);
  if (distance <= 1) return copyImage(source, context);
  const float rad = floatOf(params, "angle") * kPi / 180.f;
  const float dx = std::cos(rad), dy = std::sin(rad);
  const int start = -((distance - 1) / 2);
  const int end = distance / 2;
  const int w = source.width(), h = source.height();
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    int sum[4]{0, 0, 0, 0}, samples = 0;
    for (int i = start; i <= end; ++i) {
      const int sx = clampCoord(int(std::nearbyintf(x + i * dx)), w);
      const int sy = clampCoord(int(std::nearbyintf(y + i * dy)), h);
      const QRgb s = rowAt(source, sy)[sx];
      sum[0] += qRed(s);
      sum[1] += qGreen(s);
      sum[2] += qBlue(s);
      sum[3] += qAlpha(s);
      ++samples;
    }
    return qRgba(sum[0] / samples, sum[1] / samples, sum[2] / samples, sum[3] / samples);
  });
}

EffectResult medianFilter(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int radius = std::clamp(intOf(params, "radius"), 1, 5);
  const int w = source.width(), h = source.height();
  const int maxSamples = (radius * 2 + 1) * (radius * 2 + 1);
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    std::vector<int> ch[4];
    for (int c = 0; c < 4; ++c) ch[c].reserve(size_t(maxSamples));
    for (int ky = -radius; ky <= radius; ++ky) {
      const QRgb *row = rowAt(source, clampCoord(y + ky, h));
      for (int kx = -radius; kx <= radius; ++kx) {
        const QRgb s = row[clampCoord(x + kx, w)];
        ch[0].push_back(qRed(s));
        ch[1].push_back(qGreen(s));
        ch[2].push_back(qBlue(s));
        ch[3].push_back(qAlpha(s));
      }
    }
    const int mid = int(ch[0].size() >> 1);
    for (int c = 0; c < 4; ++c) std::nth_element(ch[c].begin(), ch[c].begin() + mid, ch[c].end());
    return qRgba(ch[0][size_t(mid)], ch[1][size_t(mid)], ch[2][size_t(mid)], ch[3][size_t(mid)]);
  });
}

EffectResult vignette(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float strength = std::clamp(floatOf(params, "strength"), 0.f, 1.f);
  const float radius = std::clamp(floatOf(params, "radius"), 0.05f, 0.999f);
  if (strength <= 0.f) return copyImage(source, context);
  const int w = source.width(), h = source.height();
  const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
  const float invCx = cx > 0.f ? 1.f / cx : 1.f;
  const float invCy = cy > 0.f ? 1.f / cy : 1.f;
  constexpr float invSqrt2 = 0.70710678f;
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const float dx = (x - cx) * invCx, dy = (y - cy) * invCy;
    const float distance = std::sqrt(dx * dx + dy * dy) * invSqrt2;
    float t = (distance - radius) / (1.f - radius);
    t = std::clamp(t, 0.f, 1.f);
    const float falloff = t * t * (3.f - 2.f * t);
    const float factor = 1.f - strength * falloff;
    return qRgba(toByte(qRed(p) * factor), toByte(qGreen(p) * factor), toByte(qBlue(p) * factor), qAlpha(p));
  });
}

EffectResult windStreaks(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float alpha = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  if (alpha <= 0.f) return copyImage(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const int len = std::max(1, intOf(params, "length"));
  const int threshold = intOf(params, "threshold");
  const int dir = choiceOf(params, "direction") == QLatin1String("left") ? -1 : 1;
  const int w = source.width(), h = source.height();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, h, 0, 100, [&](int y) {
        std::memcpy(result.scanLine(y), source.constScanLine(y), size_t(w) * 4);
        const QRgb *src = rowAt(source, y);
        QRgb *dst = rowAt(result, y);
        for (int x = 0; x < w; ++x) {
          const QRgb c = src[x];
          const float lum = 0.2126f * qRed(c) + 0.7152f * qGreen(c) + 0.0722f * qBlue(c);
          if (lum < float(threshold)) continue;
          const float streak = alpha * ((lum - float(threshold)) / (255.f - float(threshold)));
          for (int s = 1; s <= len; ++s) {
            const int tx = x + s * dir;
            if (tx < 0 || tx >= w) break;
            const float blend = streak * (1.f - float(s) / float(len));
            const QRgb existing = dst[tx];
            dst[tx] = qRgba(truncByte(qRed(existing) + (qRed(c) - qRed(existing)) * blend),
                            truncByte(qGreen(existing) + (qGreen(c) - qGreen(existing)) * blend),
                            truncByte(qBlue(existing) + (qBlue(c) - qBlue(existing)) * blend), qAlpha(existing));
          }
        }
      }))
    return {{}, kCancelled};
  return {result, {}};
}

EffectResult lightLeak(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float alpha = std::clamp(floatOf(params, "intensity") / 100.f, 0.f, 1.f);
  if (alpha <= 0.f) return copyImage(source, context);
  const QColor color = colorOf(params, "color");
  const QString position = choiceOf(params, "position");
  const int w = source.width(), h = source.height();
  float cx = float(w), cy = 0.f;
  if (position == QLatin1String("top_left")) {
    cx = 0;
    cy = 0;
  } else if (position == QLatin1String("bottom_left")) {
    cx = 0;
    cy = float(h);
  } else if (position == QLatin1String("bottom_right")) {
    cx = float(w);
    cy = float(h);
  } else if (position == QLatin1String("left")) {
    cx = 0;
    cy = h / 2.f;
  } else if (position == QLatin1String("right")) {
    cx = float(w);
    cy = h / 2.f;
  } else if (position == QLatin1String("top_right")) {
    cx = float(w);
    cy = 0;
  }
  const float radius = std::sqrt(float(w) * w + float(h) * h) * 0.8f;
  const int leakA = truncByte(255.f * alpha);
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const float dist = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
    const float t = radius > 0.f ? std::clamp(dist / radius, 0.f, 1.f) : 1.f;
    const int a = truncByte(leakA * (1.f - t));
    return screenOver(p, color.red(), color.green(), color.blue(), a);
  });
}

EffectResult slice(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int minHeight = std::max(1, std::min(intOf(params, "min_height"), intOf(params, "max_height")));
  const int maxHeight = std::max(minHeight, std::max(intOf(params, "min_height"), intOf(params, "max_height")));
  const int minShift = std::min(std::abs(intOf(params, "min_shift")), std::abs(intOf(params, "max_shift")));
  const int maxShift = std::max(std::abs(intOf(params, "min_shift")), std::abs(intOf(params, "max_shift")));
  const int w = source.width(), h = source.height();
  QImage result(w + maxShift * 2, h, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  result.fill(Qt::transparent);
  SplitMix64 rng(context.seed);
  context.progress(0);
  for (int y = 0; y < h;) {
    if (context.cancelled()) return {{}, kCancelled};
    int sliceHeight = rng.range(minHeight, maxHeight + 1);
    sliceHeight = std::min(sliceHeight, h - y);
    const int shift = rng.range(0, 2) == 0 ? rng.range(-maxShift, -minShift + 1) : rng.range(minShift, maxShift + 1);
    for (int row = 0; row < sliceHeight; ++row) {
      if (context.cancelled()) return {{}, kCancelled};
      const QRgb *src = rowAt(source, y + row);
      QRgb *dst = rowAt(result, y + row);
      for (int x = 0; x < w; ++x) dst[maxShift + shift + x] = src[x];
      context.progress(int(qint64(y + row + 1) * 100 / h));
    }
    y += sliceHeight;
  }
  return {result, {}};
}

EffectResult unsharpMask(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = std::clamp(floatOf(params, "amount"), 0.f, 500.f) / 100.f;
  if (amount <= 0.f) return copyImage(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const float sigma = std::clamp(floatOf(params, "radius"), 1.f, 100.f) / 3.f;
  const int threshold = std::clamp(intOf(params, "threshold"), 0, 255);
  QImage blurred = approximateGaussianBlur(source, sigma, BlurBorder::Decal, context, 0, 70);
  if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const QRgb b = rowAt(blurred, y)[x];
    auto sharpen = [&](int original, int soft) {
      const int diff = original - soft;
      if (std::abs(diff) < threshold) return original;
      return toByte(original + diff * amount);
    };
    return qRgba(sharpen(qRed(p), qRed(b)), sharpen(qGreen(p), qGreen(b)), sharpen(qBlue(p), qBlue(b)), qAlpha(p));
  }, 70, 100);
}

EffectResult colorHalftone(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int size = std::max(4, intOf(params, "size"));
  const int w = source.width(), h = source.height();
  const struct Channel { int r, g, b; } channels[4]{{0, 255, 255}, {255, 0, 255}, {255, 255, 0}, {0, 0, 0}};
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    int r = 255, g = 255, b = 255;
    for (const Channel &channel : channels) {
      float best = 0.f;
      const int i0 = x / size - 2, i1 = x / size + 2, j0 = y / size - 2, j1 = y / size + 2;
      for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
          const int gx = i * size, gy = j * size;
          if (gx < -size || gy < -size || gx >= w + size * 2 || gy >= h + size * 2) continue;
          const QRgb s = rowAt(source, clampCoord(gy, h))[clampCoord(gx, w)];
          float value;
          if (channel.r == 0 && channel.g == 0 && channel.b == 0)
            value = 1.f - (0.2126f * qRed(s) + 0.7152f * qGreen(s) + 0.0722f * qBlue(s)) / 255.f;
          else if (channel.r == 0) value = 1.f - qRed(s) / 255.f;
          else if (channel.g == 0) value = 1.f - qGreen(s) / 255.f;
          else value = 1.f - qBlue(s) / 255.f;
          const float dot = float(size) * 0.5f * value;
          if (dot <= 0.5f) continue;
          const float dx = x + 0.5f - gx, dy = y + 0.5f - gy;
          if (dx * dx + dy * dy <= dot * dot) best = std::max(best, dot);
        }
      }
      if (best > 0.5f) {
        r = mulDiv255(r, channel.r);
        g = mulDiv255(g, channel.g);
        b = mulDiv255(b, channel.b);
      }
    }
    return qRgba(r, g, b, 255);
  });
}

EffectResult popArtGrid(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QColor tints[4]{colorOf(params, "color1"), colorOf(params, "color2"), colorOf(params, "color3"), colorOf(params, "color4")};
  const float strength = std::clamp(floatOf(params, "strength") / 100.f, 0.f, 1.f);
  const int tintA = truncByte(255.f * strength);
  const int w = source.width(), h = source.height();
  const int halfW = w / 2, halfH = h / 2;
  const struct Cell { int x, y, w, h; int tint; } cells[4]{{0, 0, halfW, halfH, 0},
                                                          {halfW, 0, w - halfW, halfH, 1},
                                                          {0, halfH, halfW, h - halfH, 2},
                                                          {halfW, halfH, w - halfW, h - halfH, 3}};
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const Cell *cell = nullptr;
    for (const Cell &candidate : cells) {
      if (candidate.w > 0 && candidate.h > 0 && x >= candidate.x && y >= candidate.y && x < candidate.x + candidate.w &&
          y < candidate.y + candidate.h)
        cell = &candidate;
    }
    if (!cell) return qRgba(0, 0, 0, 0);
    const float sx = (x + 0.5f - cell->x) * (float(w) / float(cell->w)) - 0.5f;
    const float sy = (y + 0.5f - cell->y) * (float(h) / float(cell->h)) - 0.5f;
    const QRgb s = rowAt(source, clampCoord(int(std::floor(sy + 0.5f)), h))[clampCoord(int(std::floor(sx + 0.5f)), w)];
    const int gray = toByte(0.33f * qRed(s) + 0.33f * qGreen(s) + 0.33f * qBlue(s));
    const QColor tint = tints[cell->tint];
    return qRgba(mulDiv255(tint.red(), gray), mulDiv255(tint.green(), gray), mulDiv255(tint.blue(), gray),
                 mulDiv255(tintA, qAlpha(s)));
  });
}

EffectResult sobelEdge(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float strength = std::clamp(floatOf(params, "strength"), 0.1f, 5.f);
  const int threshold = std::clamp(intOf(params, "threshold"), 0, 255);
  const int w = source.width(), h = source.height();
  std::vector<int> luma(size_t(w) * size_t(h));
  for (int y = 0; y < h; ++y) {
    const QRgb *row = rowAt(source, y);
    for (int x = 0; x < w; ++x) luma[size_t(y * w + x)] = ((qRed(row[x]) * 77) + (qGreen(row[x]) * 150) + (qBlue(row[x]) * 29)) >> 8;
  }
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    auto at = [&](int px, int py) { return luma[size_t(clampCoord(py, h) * w + clampCoord(px, w))]; };
    const int p00 = at(x - 1, y - 1), p01 = at(x, y - 1), p02 = at(x + 1, y - 1);
    const int p10 = at(x - 1, y), p12 = at(x + 1, y);
    const int p20 = at(x - 1, y + 1), p21 = at(x, y + 1), p22 = at(x + 1, y + 1);
    const int gx = (-p00 + p02) + (-2 * p10 + 2 * p12) + (-p20 + p22);
    const int gy = (-p00 - 2 * p01 - p02) + (p20 + 2 * p21 + p22);
    const float magnitude = std::sqrt(float(gx * gx + gy * gy)) * strength;
    const int edge = magnitude >= float(threshold) ? toByte(magnitude) : 0;
    return qRgba(edge, edge, edge, qAlpha(p));
  });
}

EffectResult glow(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int size = std::max(1, intOf(params, "size"));
  const bool resize = boolOf(params, "auto_resize");
  const int offsetX = intOf(params, "offset_x"), offsetY = intOf(params, "offset_y");
  const int pad = resize ? size : 0;
  const int expandLeft = resize ? std::max(0, -offsetX) + pad : 0;
  const int expandRight = resize ? std::max(0, offsetX) + pad : 0;
  const int expandTop = resize ? std::max(0, -offsetY) + pad : 0;
  const int expandBottom = resize ? std::max(0, offsetY) + pad : 0;
  const int newW = source.width() + expandLeft + expandRight;
  const int newH = source.height() + expandTop + expandBottom;
  const QColor color = colorOf(params, "color");
  const int glowA = truncByte(255.f * floatOf(params, "strength") / 100.f);
  QImage canvas(newW, newH, QImage::Format_ARGB32);
  if (canvas.isNull()) return {{}, kFailed};
  canvas.setColorSpace(source.colorSpace());
  canvas.fill(Qt::transparent);
  const int glowX = expandLeft + offsetX, glowY = expandTop + offsetY;
  if (!runRows(context, source.height(), 0, 20, [&](int y) {
        const int dy = glowY + y;
        if (dy < 0 || dy >= newH) return;
        const QRgb *src = rowAt(source, y);
        QRgb *dst = rowAt(canvas, dy);
        for (int x = 0; x < source.width(); ++x) {
          const int dx = glowX + x;
          if (dx < 0 || dx >= newW) continue;
          dst[dx] = qRgba(color.red(), color.green(), color.blue(), mulDiv255(glowA, qAlpha(src[x])));
        }
      }))
    return {{}, kCancelled};
  QImage blurred = approximateGaussianBlur(canvas, float(size), BlurBorder::Decal, context, 20, 80);
  if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
  const int imageX = expandLeft, imageY = expandTop;
  if (!runRows(context, newH, 80, 100, [&](int y) {
        QRgb *dst = rowAt(blurred, y);
        const int sy = y - imageY;
        for (int x = 0; x < newW; ++x) {
          const int sx = x - imageX;
          if (sy < 0 || sx < 0 || sy >= source.height() || sx >= source.width()) continue;
          dst[x] = sourceOverTop(rowAt(source, sy)[sx], dst[x]);
        }
      }))
    return {{}, kCancelled};
  blurred.setColorSpace(source.colorSpace());
  return {blurred, {}};
}

EffectResult miniature(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int radius = std::max(1, intOf(params, "blur_radius"));
  QImage blurred = approximateGaussianBlur(source, float(radius), BlurBorder::Decal, context, 0, 60);
  if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
  const int h = source.height();
  const float focusY = h * floatOf(params, "focus_position") / 100.f;
  const float bandHalf = h * floatOf(params, "focus_band") / 200.f;
  const float top = focusY - bandHalf, bottom = focusY + bandHalf;
  const float satBoost = 1.f + floatOf(params, "saturation") / 100.f;
  return mapPixels(source, context, [&](int x, int y, QRgb sharp) {
    float t;
    if (y < top) t = 1.f - std::clamp((top - y) / (top + 1.f), 0.f, 1.f);
    else if (y > bottom) t = 1.f - std::clamp((y - bottom) / (h - bottom + 1.f), 0.f, 1.f);
    else t = 1.f;
    const float sharpWeight = t * t * (3.f - 2.f * t);
    const QRgb soft = rowAt(blurred, y)[x];
    const int r = truncByte(qRed(sharp) * sharpWeight + qRed(soft) * (1.f - sharpWeight));
    const int g = truncByte(qGreen(sharp) * sharpWeight + qGreen(soft) * (1.f - sharpWeight));
    const int b = truncByte(qBlue(sharp) * sharpWeight + qBlue(soft) * (1.f - sharpWeight));
    float hue, sat, lum;
    rgbToHsl(r, g, b, hue, sat, lum);
    return hslToRgb(hue, std::clamp(sat * satBoost, 0.f, 100.f), lum, qAlpha(sharp));
  }, 60, 100);
}

EffectResult oilPaint(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int radius = std::clamp(intOf(params, "radius"), 1, 6);
  const int levels = std::clamp(intOf(params, "levels"), 8, 64);
  const int w = source.width(), h = source.height();
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const size_t bins = size_t(levels);
    std::vector<int> count(bins), sumR(bins), sumG(bins), sumB(bins), sumA(bins);
    for (int ky = -radius; ky <= radius; ++ky) {
      const QRgb *row = rowAt(source, clampCoord(y + ky, h));
      for (int kx = -radius; kx <= radius; ++kx) {
        const QRgb c = row[clampCoord(x + kx, w)];
        int bin = ((qRed(c) + qGreen(c) + qBlue(c)) * levels) / 768;
        if (bin >= levels) bin = levels - 1;
        ++count[size_t(bin)];
        sumR[size_t(bin)] += qRed(c);
        sumG[size_t(bin)] += qGreen(c);
        sumB[size_t(bin)] += qBlue(c);
        sumA[size_t(bin)] += qAlpha(c);
      }
    }
    int best = 0;
    for (int i = 1; i < levels; ++i)
      if (count[size_t(i)] > count[size_t(best)]) best = i;
    const int n = std::max(1, count[size_t(best)]);
    return qRgba(sumR[size_t(best)] / n, sumG[size_t(best)] / n, sumB[size_t(best)] / n, sumA[size_t(best)] / n);
  });
}

EffectResult zoomBlur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float strength = std::clamp(floatOf(params, "strength"), 0.f, 100.f) / 100.f;
  const int samples = std::clamp(intOf(params, "samples"), 4, 64);
  if (strength <= 0.0001f) return copyImage(source, context);
  const int w = source.width(), h = source.height();
  const float cx = (std::clamp(floatOf(params, "center_x"), 0.f, 100.f) / 100.f) * (w - 1);
  const float cy = (std::clamp(floatOf(params, "center_y"), 0.f, 100.f) / 100.f) * (h - 1);
  const float inv = samples <= 1 ? 1.f : 1.f / float(samples - 1);
  const size_t sampleCount = size_t(samples);
  std::vector<float> factors(sampleCount), weights(sampleCount);
  for (int i = 0; i < samples; ++i) {
    const float t = float(i) * inv;
    factors[size_t(i)] = 1.f - t * strength;
    weights[size_t(i)] = 1.f - t * 0.6f;
  }
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const float dx = x - cx, dy = y - cy;
    float sum[4]{0, 0, 0, 0}, sumW = 0.f;
    for (int i = 0; i < samples; ++i) {
      const QRgb s = bilinear(source, cx + dx * factors[size_t(i)], cy + dy * factors[size_t(i)]);
      const float weight = weights[size_t(i)];
      sum[0] += qRed(s) * weight;
      sum[1] += qGreen(s) * weight;
      sum[2] += qBlue(s) * weight;
      sum[3] += qAlpha(s) * weight;
      sumW += weight;
    }
    const float invW = 1.f / std::max(0.0001f, sumW);
    return qRgba(toByte(sum[0] * invW), toByte(sum[1] * invW), toByte(sum[2] * invW), toByte(sum[3] * invW));
  });
}

void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &file, Mode mode,
         QVector<ParamSpec> parameters, ApplyFn fn, bool changesSize = false, bool usesSeed = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Filters;
  spec.mode = mode;
  spec.parameters = std::move(parameters);
  spec.changesSize = changesSize;
  spec.usesSeed = usesSeed;
  spec.referencePath = kFilters + file;
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}
}  // namespace

void registerFilterEffects2(EffectRegistry &registry) {
  const auto P = Mode::Previewable;
  add(registry, "color_splash", "Color splash", "ColorSplashImageEffect.cs", P,
      {intParam("hue", "Target hue", 0, 0, 360), intParam("tolerance", "Tolerance", 30, 1, 180)}, colorSplash);
  add(registry, "heat_map", "Heat map", "HeatMapImageEffect.cs", Mode::Immediate, {}, heatMap);
  add(registry, "dream_glow", "Dream glow", "DreamGlowImageEffect.cs", P,
      {intParam("radius", "Blur radius", 15, 1, 50), floatParam("opacity", "Blend opacity", 50, 0, 100)}, dreamGlow);
  add(registry, "radial_rainbow", "Radial rainbow", "RadialRainbowImageEffect.cs", P,
      {floatParam("strength", "Strength", 50, 0, 100), intParam("rotations", "Rotations", 1, 1, 5)}, radialRainbow);
  add(registry, "diamond_pixelate", "Diamond pixelate", "DiamondPixelateImageEffect.cs", P,
      {intParam("size", "Size", 16, 4, 100)}, diamondPixelate);
  add(registry, "ghost_trail", "Ghost trail", "GhostTrailImageEffect.cs", P,
      {intParam("copies", "Copies", 3, 1, 8), intParam("offset_x", "Offset X", 15, -50, 50),
       intParam("offset_y", "Offset Y", 5, -50, 50), floatParam("fade", "Fade", 50, 10, 90)},
      ghostTrail);
  add(registry, "film_grain", "Film grain", "FilmGrainImageEffect.cs", P,
      {intParam("amount", "Amount", 30, 1, 100), boolParam("monochrome", "Monochrome", true)}, filmGrain, false, true);
  add(registry, "glitch_shift", "Glitch shift", "GlitchShiftImageEffect.cs", P,
      {intParam("intensity", "Intensity", 30, 1, 100), intParam("bands", "Band count", 12, 2, 50)}, glitchShift, false, true);
  add(registry, "add_noise", "Add noise", "AddNoiseImageEffect.cs", P, {floatParam("amount", "Amount", 8, 0, 100)}, addNoise, false,
      true);
  add(registry, "hexagonal_pixelate", "Hexagonal pixelate", "HexagonalPixelateImageEffect.cs", P,
      {intParam("size", "Size", 16, 4, 100)}, hexagonalPixelate);
  add(registry, "stripe_overlay", "Stripe overlay", "StripeOverlayImageEffect.cs", P,
      {intParam("width", "Stripe width", 10, 2, 50), intParam("gap", "Gap width", 10, 2, 50), intParam("angle", "Angle", 45, 0, 180),
       floatParam("opacity", "Opacity", 40, 0, 100), colorParam("color", "Color", QColor(0, 0, 0))},
      stripeOverlay);
  add(registry, "gaussian_blur", "Gaussian blur", "GaussianBlurImageEffect.cs", P, {intParam("radius", "Radius", 15, 1, 200)},
      gaussianBlur);
  add(registry, "blur", "Blur", "BlurImageEffect.cs", P, {intParam("radius", "Radius", 10, 1, 200)}, blur);
  add(registry, "triangle_pixelate", "Triangle pixelate", "TrianglePixelateImageEffect.cs", P,
      {intParam("size", "Size", 24, 8, 100)}, trianglePixelate);
  add(registry, "convolution_matrix", "Convolution matrix", "ConvolutionMatrixImageEffect.cs", P,
      {intParam("x0y0", "X0 Y0", 0, -99, 99), intParam("x1y0", "X1 Y0", 0, -99, 99), intParam("x2y0", "X2 Y0", 0, -99, 99),
       intParam("x0y1", "X0 Y1", 0, -99, 99), intParam("x1y1", "X1 Y1", 1, -99, 99), intParam("x2y1", "X2 Y1", 0, -99, 99),
       intParam("x0y2", "X0 Y2", 0, -99, 99), intParam("x1y2", "X1 Y2", 0, -99, 99), intParam("x2y2", "X2 Y2", 0, -99, 99),
       floatParam("factor", "Factor", 1, 0.01, 10), intParam("offset", "Offset", 0, -255, 255)},
      convolutionMatrix);
  add(registry, "retro_sunset", "Retro sunset", "RetroSunsetImageEffect.cs", P,
      {floatParam("strength", "Strength", 50, 0, 100), boolParam("horizontal_lines", "Horizontal lines", true)}, retroSunset);
  add(registry, "motion_blur", "Motion blur", "MotionBlurImageEffect.cs", P,
      {intParam("distance", "Distance", 12, 1, 200), floatParam("angle", "Angle", 0, 0, 360)}, motionBlur);
  add(registry, "median_filter", "Median filter", "MedianFilterImageEffect.cs", P, {intParam("radius", "Radius", 1, 1, 5)},
      medianFilter);
  add(registry, "vignette", "Vignette", "VignetteImageEffect.cs", P,
      {floatParam("strength", "Strength", 0.5, 0, 1), floatParam("radius", "Radius", 0.75, 0.05, 0.999)}, vignette);
  add(registry, "wind_streaks", "Wind streaks", "WindStreaksImageEffect.cs", P,
      {intParam("length", "Streak length", 30, 5, 100), intParam("threshold", "Brightness threshold", 200, 100, 254),
       floatParam("opacity", "Opacity", 40, 0, 100), enumParam("direction", "Direction", "right", {"left", "right"})},
      windStreaks);
  add(registry, "light_leak", "Light leak", "LightLeakImageEffect.cs", P,
      {floatParam("intensity", "Intensity", 50, 0, 100), colorParam("color", "Color", QColor(255, 160, 50)),
       enumParam("position", "Position", "top_right",
                 {"top_left", "top_right", "bottom_left", "bottom_right", "left", "right"})},
      lightLeak);
  add(registry, "slice", "Slice", "SliceImageEffect.cs", P,
      {intParam("min_height", "Min Height", 10, 1, 100), intParam("max_height", "Max Height", 100, 1, 200),
       intParam("min_shift", "Min Shift", 0, 0, 100), intParam("max_shift", "Max Shift", 10, 0, 100)},
      slice, true, true);
  add(registry, "unsharp_mask", "Unsharp mask", "UnsharpMaskImageEffect.cs", P,
      {floatParam("radius", "Radius", 5, 1, 100), floatParam("amount", "Amount", 150, 0, 500),
       intParam("threshold", "Threshold", 0, 0, 255)},
      unsharpMask);
  add(registry, "color_halftone", "Color halftone", "ColorHalftoneImageEffect.cs", P, {intParam("size", "Dot size", 8, 4, 30)},
      colorHalftone);
  add(registry, "pop_art_grid", "Pop art grid", "PopArtGridImageEffect.cs", P,
      {colorParam("color1", "Color 1", QColor(255, 50, 50)), colorParam("color2", "Color 2", QColor(50, 200, 50)),
       colorParam("color3", "Color 3", QColor(50, 50, 255)), colorParam("color4", "Color 4", QColor(255, 200, 50)),
       floatParam("strength", "Color strength", 60, 0, 100)},
      popArtGrid);
  add(registry, "sobel_edge", "Sobel edge", "SobelEdgeImageEffect.cs", P,
      {floatParam("strength", "Strength", 1.2, 0.1, 5), intParam("threshold", "Threshold", 20, 0, 255)}, sobelEdge);
  add(registry, "glow", "Glow", "GlowImageEffect.cs", P,
      {intParam("size", "Size", 20, 1, 100), floatParam("strength", "Strength", 80, 1, 100), intParam("offset_x", "Offset X", 0, -100, 100),
       intParam("offset_y", "Offset Y", 0, -100, 100), colorParam("color", "Color", QColor(255, 255, 255)),
       boolParam("auto_resize", "Auto resize", true)},
      glow, true);
  add(registry, "miniature", "Miniature", "MiniatureImageEffect.cs", P,
      {intParam("blur_radius", "Blur radius", 15, 2, 40), floatParam("focus_position", "Focus position %", 50, 10, 90),
       floatParam("focus_band", "Focus band %", 20, 5, 50), floatParam("saturation", "Saturation boost", 20, 0, 50)},
      miniature);
  add(registry, "oil_paint", "Oil paint", "OilPaintImageEffect.cs", P,
      {intParam("radius", "Radius", 3, 1, 6), intParam("levels", "Levels", 24, 8, 64)}, oilPaint);
  add(registry, "zoom_blur", "Zoom blur", "ZoomBlurImageEffect.cs", P,
      {floatParam("strength", "Strength", 35, 0, 100), intParam("samples", "Samples", 24, 4, 64),
       floatParam("center_x", "Center X", 50, 0, 100), floatParam("center_y", "Center Y", 50, 0, 100)},
      zoomBlur);
}
}  // namespace xerahs::effects
