// EDITOR-EFFECTS-001: the Filters category, batch 3. EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census source
// path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Skia blurs (SKImageFilter.CreateBlur, SKMaskFilter blur) are a true separable Gaussian on
//    premultiplied float pixels with the same sigma; Skia switches to box-blur passes for large sigma.
//  - Skia vector drawing (circles, paths, lines, gradients) is QPainter with antialiasing; pixel
//    coverage at shape edges can differ by a few levels.
//  - Position-hash noise (Hash01) is ported as-is. Where the reference has a settable Seed property
//    (cyanotype, hologram_scan, pointillism, paper_stencil_mask, anime_speed_lines, wet_plate_collodion,
//    crystal_prism, disposable_camera) the hash seed is that default XOR the low/high halves of
//    EffectContext::seed, so seed 0 reproduces the reference and other seeds vary it (usesSeed).
//    System.Random in block_glitch is SplitMix64 from EffectContext::seed. Hash constants fixed in
//    the reference (x_ray_scan, night_vision, vhs_tape_damage, mosaic_tile_border, chalk_drawing's
//    xorshift) stay fixed and those effects do not use the seed.
//  - SkiaSharp SKColor.ToHsl/FromHsl/FromHsv are reimplemented from their documented formulas.
//  - Text (polaroid_frame caption) uses QPainter/QFont with the application default font.
#include "Effect.h"
#include "EffectSupport.h"

#include <QBrush>
#include <QColorSpace>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;
const QString kReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");
constexpr float kPi = 3.14159265358979f;

float applyContrast(float v, float contrast) { return clamp01((v - 0.5f) * contrast + 0.5f); }

QRgb rgbaF(float r, float g, float b, int a) { return qRgba(toByte(r * 255.f), toByte(g * 255.f), toByte(b * 255.f), a); }

// ---- plumbing -------------------------------------------------------------------------------------

// A premultiplied painting surface the size of `like`, filled with `fill`.
QImage paintSurface(QSize size, QRgb fill) {
  QImage surface(size, QImage::Format_ARGB32_Premultiplied);
  surface.fill(QColor::fromRgba(fill));
  return surface;
}
EffectResult finishSurface(const QImage &surface, const QImage &like) {
  QImage out = surface.convertToFormat(QImage::Format_ARGB32);
  if (out.isNull()) return {{}, kFailed};
  out.setColorSpace(like.colorSpace());
  return {out, {}};
}

void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference,
         QVector<ParamSpec> parameters, ApplyFn fn, bool usesSeed = false, bool changesSize = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Filters;
  spec.mode = Mode::Previewable;
  spec.parameters = std::move(parameters);
  spec.usesSeed = usesSeed;
  spec.changesSize = changesSize;
  spec.referencePath = kReferenceDirectory + reference + QStringLiteral("ImageEffect.cs");
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}

// ---- crosshatch -----------------------------------------------------------------------------------
bool isLine(float value, float spacing, float thickness) {
  float mod = std::fmod(value, spacing);
  if (mod < 0.f) mod += spacing;
  return std::min(mod, spacing - mod) <= thickness * 0.5f;
}
EffectResult crosshatch(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float spacing = float(intOf(params, "lineSpacing"));
  const float thickness = floatOf(params, "lineThickness");
  const float contrast = floatOf(params, "contrast") / 100.f;
  const int layers = intOf(params, "layerCount");
  const float thresholds[]{0.14f, 0.28f, 0.42f, 0.58f, 0.74f, 0.88f};
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float lum = luminance01(src);
    lum = clamp01((lum - 0.5f) * contrast + 0.5f);
    const float darkness = 1.f - lum;
    const float fx = float(x), fy = float(y);
    int active = 0;
    if (layers >= 1 && darkness > thresholds[0] && isLine(std::fabs((fx + fy) * 0.7071f), spacing, thickness)) ++active;
    if (layers >= 2 && darkness > thresholds[1] && isLine(std::fabs((fx - fy) * 0.7071f), spacing, thickness)) ++active;
    if (layers >= 3 && darkness > thresholds[2] && isLine(fy, spacing, thickness)) ++active;
    if (layers >= 4 && darkness > thresholds[3] && isLine(fx, spacing, thickness)) ++active;
    if (layers >= 5 && darkness > thresholds[4] && isLine(fx * 0.5f + fy, spacing, thickness)) ++active;
    if (layers >= 6 && darkness > thresholds[5] && isLine(fx * 0.5f - fy, spacing, thickness)) ++active;
    const float ink = float(active) / float(layers);
    float value = lum * (1.f - ink * 0.94f);
    value = clamp01(std::min(value, lum + 0.06f));
    const int v = toByte(value * 255.f);
    return qRgba(v, v, v, qAlpha(src));
  });
}

// ---- surface_blur ---------------------------------------------------------------------------------
EffectResult surfaceBlur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int radius = intOf(params, "radius");
  const int threshold = intOf(params, "threshold");
  const int thresholdSum = threshold * 3;
  const float weightScale = 0.65f / float(std::max(1, thresholdSum));
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb center) {
    float sumR = float(qRed(center)), sumG = float(qGreen(center)), sumB = float(qBlue(center)), sumA = float(qAlpha(center));
    float sumW = 1.f;
    for (int ky = -radius; ky <= radius; ++ky) {
      for (int kx = -radius; kx <= radius; ++kx) {
        if (kx == 0 && ky == 0) continue;
        const QRgb c = p.clamped(x + kx, y + ky);
        const int delta = std::abs(qRed(c) - qRed(center)) + std::abs(qGreen(c) - qGreen(center)) + std::abs(qBlue(c) - qBlue(center));
        if (delta > thresholdSum) continue;
        const float w = 1.f - float(delta) * weightScale;
        sumR += float(qRed(c)) * w;
        sumG += float(qGreen(c)) * w;
        sumB += float(qBlue(c)) * w;
        sumA += float(qAlpha(c)) * w;
        sumW += w;
      }
    }
    const float inv = 1.f / std::max(0.0001f, sumW);
    return qRgba(toByte(sumR * inv), toByte(sumG * inv), toByte(sumB * inv), toByte(sumA * inv));
  });
}

// ---- rgb_split ------------------------------------------------------------------------------------
EffectResult rgbSplit(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int rx = intOf(params, "offset_red_x"), ry = intOf(params, "offset_red_y");
  const int gx = intOf(params, "offset_green_x"), gy = intOf(params, "offset_green_y");
  const int bx = intOf(params, "offset_blue_x"), by = intOf(params, "offset_blue_y");
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const QRgb cr = p.clamped(x - rx, y - ry), cg = p.clamped(x - gx, y - gy), cb = p.clamped(x - bx, y - by);
    // Channels are premultiplied by their own pixel's alpha (integer division) and stored unpremultiplied-style, as the reference does.
    const int red = qRed(cr) * qAlpha(cr) / 255;
    const int green = qGreen(cg) * qAlpha(cg) / 255;
    const int blue = qBlue(cb) * qAlpha(cb) / 255;
    const int alpha = (qAlpha(cr) + qAlpha(cg) + qAlpha(cb)) / 3;
    return qRgba(red, green, blue, alpha);
  });
}

// ---- dot_matrix -----------------------------------------------------------------------------------
EffectResult dotMatrix(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const int size = intOf(params, "cell_size");
  const float dotScale = floatOf(params, "dot_scale");
  const bool colorMode = boolOf(params, "color_mode");
  const QColor dotColor = colorOf(params, "dot_color"), paperColor = colorOf(params, "paper_color");
  const float maxRadius = float(size) * 0.5f * dotScale;
  const Pixels p(source);
  QImage surface = paintSurface(source.size(), paperColor.rgba());
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  const int rows = (height + size - 1) / size, cols = (width + size - 1) / size;
  for (int gy = 0; gy < rows; ++gy) {
    if (context.cancelled()) return {{}, kCancelled};
    for (int gx = 0; gx < cols; ++gx) {
      const int x0 = gx * size, y0 = gy * size, x1 = std::min(x0 + size, width), y1 = std::min(y0 + size, height);
      qint64 sumR = 0, sumG = 0, sumB = 0;
      int count = 0;
      for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
          const QRgb c = p.at(px, py);
          sumR += qRed(c);
          sumG += qGreen(c);
          sumB += qBlue(c);
          ++count;
        }
      if (count == 0) continue;
      const int avgR = int(sumR / count), avgG = int(sumG / count), avgB = int(sumB / count);
      const float lum = (0.299f * float(avgR) + 0.587f * float(avgG) + 0.114f * float(avgB)) / 255.f;
      const float radius = (1.f - lum) * maxRadius;
      if (radius < 0.3f) continue;
      painter.setBrush(colorMode ? QColor(avgR, avgG, avgB) : dotColor);
      painter.drawEllipse(QPointF(double(x0) + double(size) * 0.5, double(y0) + double(size) * 0.5), double(radius), double(radius));
    }
  }
  painter.end();
  return finishSurface(surface, source);
}

// ---- edge_feather ---------------------------------------------------------------------------------
EffectResult edgeFeather(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float radius = floatOf(params, "radius");
  if (radius <= 0.01f) return identityCopy(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const Pixels p(source);
  FloatImage mask;
  mask.w = source.width();
  mask.h = source.height();
  mask.d.assign(size_t(mask.w) * size_t(mask.h) * 4, 0.f);
  for (size_t i = 0; i < size_t(mask.w) * size_t(mask.h); ++i)
    if (qAlpha(p.data[i]) > 16) mask.d[i * 4 + 3] = 255.f;  // only alpha is read back
  if (!gaussianBlur255(mask, std::max(0.001f, radius / 3.f), false, context)) return {{}, kCancelled};
  return mapPixels(source, context, [&](int x, int y, QRgb c) {
    const float feather = std::clamp(mask.d[(size_t(y) * size_t(mask.w) + size_t(x)) * 4 + 3], 0.f, 255.f) / 255.f;
    return qRgba(qRed(c), qGreen(c), qBlue(c), toByte(float(qAlpha(c)) * float(toByte(feather * 255.f)) / 255.f));
  });
}

// ---- reflection (changes size) --------------------------------------------------------------------
EffectResult reflection(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int percentage = intOf(params, "percentage"), maxAlpha = intOf(params, "max_alpha"), minAlpha = intOf(params, "min_alpha");
  const int offset = intOf(params, "offset"), skewSize = intOf(params, "skew_size");
  const bool skew = boolOf(params, "skew");
  const int w = source.width(), h = source.height();
  const int rh = int(float(h) * float(percentage) / 100.f);
  const int newHeight = h + offset + rh;
  const bool skewed = skew && skewSize > 0;
  const float skewFactor = float(skewSize) / 100.f;
  const int newWidth = w + (skewed ? int(float(rh) * skewFactor) : 0);
  QImage result(newWidth, newHeight, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  result.fill(Qt::transparent);
  const Pixels p(source);
  for (int y = 0; y < h; ++y) std::memcpy(result.scanLine(y), source.constScanLine(y), size_t(w) * 4);
  const float yScale = rh > 0 ? 1.f / float(rh) : 0.f;
  const bool done = runRows(context, rh, 0, 100, [&](int ry) {
    // Linear gradient white(maxAlpha) -> white(minAlpha) over 0..rh, evaluated at the row centre, applied as DstIn.
    const float t = (float(ry) + 0.5f) * yScale;
    const float gradientAlpha = lerp(float(maxAlpha), float(minAlpha), t) / 255.f;
    const int srcY = h - 1 - ry;
    if (srcY < 0) return;
    auto *row = reinterpret_cast<QRgb *>(result.scanLine(h + offset + ry));
    for (int x = 0; x < newWidth; ++x) {
      int sx = x;
      if (skewed) sx = int(std::floor(float(x) + 0.5f - skewFactor * (float(ry) + 0.5f)));  // nearest, inverse of x' = x + k y
      if (sx < 0 || sx >= w) continue;
      const QRgb c = p.at(sx, srcY);
      row[x] = qRgba(qRed(c), qGreen(c), qBlue(c), toByte(float(qAlpha(c)) * gradientAlpha));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- spin_blur ------------------------------------------------------------------------------------
EffectResult spinBlur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float angle = floatOf(params, "angle");
  const int samples = intOf(params, "samples");
  if (angle <= 0.001f) return identityCopy(source, context);
  const int w = source.width(), h = source.height();
  const float cx = floatOf(params, "center_x") / 100.f * float(w - 1);
  const float cy = floatOf(params, "center_y") / 100.f * float(h - 1);
  const float maxRadians = angle * (kPi / 180.f);
  const float half = float(samples - 1) * 0.5f;
  const float invHalf = half <= 0.0001f ? 1.f : 1.f / half;
  std::vector<float> sinV(size_t(samples), 0.f), cosV(size_t(samples), 0.f), weights(size_t(samples), 0.f);
  for (int i = 0; i < samples; ++i) {
    const float t = (float(i) - half) * invHalf;
    const float theta = t * maxRadians;
    sinV[size_t(i)] = std::sin(theta);
    cosV[size_t(i)] = std::cos(theta);
    weights[size_t(i)] = 1.f - std::fabs(t) * 0.7f;
  }
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb c) {
    const float dx = float(x) - cx, dy = float(y) - cy;
    if (dx * dx + dy * dy < 0.25f) return c;
    float sumR = 0, sumG = 0, sumB = 0, sumA = 0, sumW = 0;
    for (int i = 0; i < samples; ++i) {
      const float s = sinV[size_t(i)], co = cosV[size_t(i)], wt = weights[size_t(i)];
      const QRgb sample = bilinear(p, cx + (dx * co - dy * s), cy + (dx * s + dy * co));
      sumR += float(qRed(sample)) * wt;
      sumG += float(qGreen(sample)) * wt;
      sumB += float(qBlue(sample)) * wt;
      sumA += float(qAlpha(sample)) * wt;
      sumW += wt;
    }
    const float inv = 1.f / std::max(0.0001f, sumW);
    return qRgba(toByte(sumR * inv), toByte(sumG * inv), toByte(sumB * inv), toByte(sumA * inv));
  });
}

// ---- x_ray_scan -----------------------------------------------------------------------------------
EffectResult xRayScan(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float contrast = floatOf(params, "contrast") / 100.f, glow = floatOf(params, "glow") / 100.f;
  const float edgeBoost = floatOf(params, "edge_boost") / 100.f, scanlines = floatOf(params, "scanlines") / 100.f;
  const float noise = floatOf(params, "noise") / 100.f;
  const Pixels p(source);
  const int w = source.width(), h = source.height();
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float lum = luminance01(src);
    const float left = luminance01(p.at(std::max(0, x - 1), y)), right = luminance01(p.at(std::min(w - 1, x + 1), y));
    const float top = luminance01(p.at(x, std::max(0, y - 1))), bottom = luminance01(p.at(x, std::min(h - 1, y + 1)));
    const float edge = clamp01((std::fabs(right - left) + std::fabs(bottom - top)) * (0.85f + edgeBoost * 1.45f));
    float density = 1.f - lum;
    density = std::pow(clamp01(density), 1.85f - contrast * 1.25f);
    const float energy = clamp01(density * 0.48f + edge * 0.92f);
    const float glowMask = clamp01(edge * (0.25f + glow * 0.95f) + energy * glow * 0.18f);
    const float scan = 1.f - scanlines * 0.16f * (0.5f + 0.5f * std::sin(float(y) * kPi));
    const float grain = (hash01(x, y, 8849) * 2.f - 1.f) * noise * 0.08f;
    const float r = clamp01((energy * 0.14f + glowMask * 0.08f + grain * 0.30f) * scan);
    const float g = clamp01((energy * 0.72f + glowMask * 0.16f + grain * 0.40f) * scan);
    const float b = clamp01((energy * 1.00f + glowMask * 0.26f + grain * 0.48f) * scan);
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- double_exposure ------------------------------------------------------------------------------
EffectResult doubleExposure(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float blend = floatOf(params, "blend_amount") / 100.f;
  const int offsetX = intOf(params, "offset_x"), offsetY = intOf(params, "offset_y");
  const float blur = floatOf(params, "ghost_blur");
  const float highlightBias = floatOf(params, "highlight_bias") / 100.f;
  const float contrast = floatOf(params, "contrast") / 100.f;
  if (blend <= 0.0001f && blur <= 0.0001f && offsetX == 0 && offsetY == 0) return identityCopy(source, context);
  QImage ghostImage;
  if (blur > 0.01f) {
    if (!blurredClamp(source, blur, context, ghostImage)) return {{}, kCancelled};
  } else {
    ghostImage = source;
  }
  const Pixels ghosts(ghostImage);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const QRgb ghost = bilinear(ghosts, float(x - offsetX), float(y - offsetY));
    const float sr = float(qRed(src)) / 255.f, sg = float(qGreen(src)) / 255.f, sb = float(qBlue(src)) / 255.f;
    const float ghostLum = luminance01(ghost);
    float gr = lerp(float(qRed(ghost)) / 255.f, ghostLum * (1.12f + highlightBias * 0.48f), 0.68f);
    float gg = lerp(float(qGreen(ghost)) / 255.f, ghostLum * 0.95f, 0.78f);
    float gb = lerp(float(qBlue(ghost)) / 255.f, ghostLum * 0.82f, 0.84f);
    gr = applyContrast(gr, contrast);
    gg = applyContrast(gg, contrast);
    gb = applyContrast(gb, contrast);
    const float localBlend = blend * (0.55f + ghostLum * 0.45f);
    const float r = lerp(sr, screenClamped(sr, gr), localBlend);
    const float g = lerp(sg, screenClamped(sg, gg), localBlend * 0.92f);
    const float b = lerp(sb, screenClamped(sb, gb), localBlend * 0.88f);
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- glare ----------------------------------------------------------------------------------------
EffectResult glare(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float intensity = floatOf(params, "intensity") / 100.f;
  if (intensity <= 0.f) return identityCopy(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const int w = source.width(), h = source.height();
  const float cx = float(w) * floatOf(params, "position_x") / 100.f;
  const float cy = float(h) * floatOf(params, "position_y") / 100.f;
  const float diagonal = std::sqrt(float(w * w + h * h));
  const float radiusPx = diagonal * floatOf(params, "radius") / 200.f;
  const float rayLengthPx = diagonal * floatOf(params, "ray_length") / 200.f;
  const QColor color = colorOf(params, "color");
  QImage surface = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setCompositionMode(QPainter::CompositionMode_Screen);
  const int glareAlpha = int(255.f * intensity);
  auto withAlpha = [&](int a) { return QColor(color.red(), color.green(), color.blue(), a); };
  QRadialGradient glow{QPointF{double(cx), double(cy)}, double(radiusPx)};
  glow.setColorAt(0.0, withAlpha(glareAlpha));
  glow.setColorAt(0.4, withAlpha(int(float(glareAlpha) * 0.4f)));
  glow.setColorAt(1.0, withAlpha(0));
  painter.fillRect(QRectF(0, 0, w, h), QBrush(glow));
  const int rayCount = intOf(params, "ray_count");
  if (rayCount > 0 && rayLengthPx > 1.f) {
    const float angleStep = 360.f / float(rayCount);
    const float rayWidth = radiusPx * 0.15f;
    for (int i = 0; i < rayCount; ++i) {
      if (context.cancelled()) return {{}, kCancelled};
      const float angle = float(i) * angleStep * kPi / 180.f;
      const QPointF from{double(cx), double(cy)};
      const QPointF to{double(cx + std::cos(angle) * rayLengthPx), double(cy + std::sin(angle) * rayLengthPx)};
      QLinearGradient ray(from, to);
      ray.setColorAt(0.0, withAlpha(glareAlpha));
      ray.setColorAt(1.0, withAlpha(0));
      QPen pen(QBrush(ray), double(rayWidth), Qt::SolidLine, Qt::RoundCap);
      painter.setPen(pen);
      painter.drawLine(from, to);
    }
  }
  painter.end();
  QImage out = makeResult(source);
  const bool done = runRows(context, h, 0, 100, [&](int y) {
    const auto *in = reinterpret_cast<const QRgb *>(surface.constScanLine(y));
    auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < w; ++x) dst[x] = qUnpremultiply(in[x]);
  });
  if (!done) return {{}, kCancelled};
  return {out, {}};
}

// ---- anaglyph_3d ----------------------------------------------------------------------------------
EffectResult anaglyph3d(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float ghost = floatOf(params, "ghost_reduction") / 100.f;
  const int sepX = intOf(params, "separation_x"), sepY = intOf(params, "separation_y");
  if (sepX == 0 && sepY == 0) return identityCopy(source, context);
  const QString mode = choiceOf(params, "mode");
  const float halfX = float(sepX) * 0.5f, halfY = float(sepY) * 0.5f;
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const QRgb left = bilinear(p, float(x) - halfX, float(y) - halfY);
    const QRgb right = bilinear(p, float(x) + halfX, float(y) + halfY);
    const float lg = luminance01(left), rg = luminance01(right);
    float r, g, b;
    if (mode == "amber_blue") {
      r = lerp(float(qRed(left)) / 255.f, lg, ghost * 0.85f);
      g = lerp(float(qGreen(left)) / 255.f, lg, ghost * 0.50f);
      b = lerp(float(qBlue(right)) / 255.f, rg, ghost);
    } else if (mode == "green_magenta") {
      r = lerp(float(qRed(right)) / 255.f, rg, ghost * 0.85f);
      g = lerp(float(qGreen(left)) / 255.f, lg, ghost);
      b = lerp(float(qBlue(right)) / 255.f, rg, ghost * 0.85f);
    } else {
      r = lerp(float(qRed(left)) / 255.f, lg, ghost);
      g = lerp(float(qGreen(right)) / 255.f, rg, ghost * 0.90f);
      b = lerp(float(qBlue(right)) / 255.f, rg, ghost);
    }
    return rgbaF(r, g, b, (qAlpha(left) + qAlpha(right)) / 2);
  });
}

// ---- star_filter ----------------------------------------------------------------------------------
EffectResult starFilter(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float threshold = floatOf(params, "threshold") / 100.f;
  const float length = floatOf(params, "length");
  const float strength = floatOf(params, "strength") / 100.f;
  const float warmth = floatOf(params, "warmth") / 100.f;
  if (strength <= 0.0001f) return identityCopy(source, context);
  const int sampleCount = clampInt(int(std::nearbyintf(length / 4.f)), 3, 18);
  const float step = std::max(1.f, length / float(sampleCount));
  const float baseAngle = floatOf(params, "rotation") * (kPi / 180.f);
  const float angles[4]{baseAngle, baseAngle + kPi * 0.5f, baseAngle + kPi * 0.25f, baseAngle + kPi * 0.75f};
  const float weights[4]{1.f, 1.f, 0.68f, 0.68f};
  const float invRange = 1.f / std::max(0.0001f, 1.f - threshold);
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float streak = 0.f;
    for (int d = 0; d < 4; ++d) {
      const float dirX = std::cos(angles[d]), dirY = std::sin(angles[d]);
      for (int i = 1; i <= sampleCount; ++i) {
        const float falloff = 1.f - float(i) / float(sampleCount + 1);
        const float offset = float(i) * step;
        const QRgb positive = bilinear(p, float(x) + dirX * offset, float(y) + dirY * offset);
        const QRgb negative = bilinear(p, float(x) - dirX * offset, float(y) - dirY * offset);
        const float lumA = std::max(0.f, luminance01(positive) - threshold) * invRange;
        const float lumB = std::max(0.f, luminance01(negative) - threshold) * invRange;
        streak += (lumA * lumA + lumB * lumB) * falloff * weights[d];
      }
    }
    const float star = strength * (streak / (float(sampleCount) * 3.6f));
    const float r = screenClamped(float(qRed(src)) / 255.f, star * (0.90f + warmth * 0.18f));
    const float g = screenClamped(float(qGreen(src)) / 255.f, star * (0.74f + warmth * 0.08f));
    const float b = screenClamped(float(qBlue(src)) / 255.f, star * (0.52f - warmth * 0.16f));
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- thermal_vision -------------------------------------------------------------------------------
QRgb thermalLerp(int ar, int ag, int ab, int br, int bg, int bb, float t) {
  return qRgb(toByte(lerp(float(ar), float(br), t)), toByte(lerp(float(ag), float(bg), t)), toByte(lerp(float(ab), float(bb), t)));
}
QRgb thermalGradient(float t) {
  t = std::clamp(t, 0.f, 1.f);
  if (t <= 0.20f) return thermalLerp(0, 0, 52, 0, 34, 170, t / 0.20f);
  if (t <= 0.40f) return thermalLerp(0, 34, 170, 0, 160, 255, (t - 0.20f) / 0.20f);
  if (t <= 0.60f) return thermalLerp(0, 160, 255, 0, 255, 210, (t - 0.40f) / 0.20f);
  if (t <= 0.76f) return thermalLerp(0, 255, 210, 255, 255, 0, (t - 0.60f) / 0.16f);
  if (t <= 0.90f) return thermalLerp(255, 255, 0, 255, 80, 0, (t - 0.76f) / 0.14f);
  return thermalLerp(255, 80, 0, 255, 255, 255, (t - 0.90f) / 0.10f);
}
EffectResult thermalVision(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int levels = intOf(params, "levels");
  const float contrast = floatOf(params, "contrast") / 100.f, glow = floatOf(params, "glow") / 100.f;
  const float blend = floatOf(params, "blend") / 100.f;
  const bool invert = boolOf(params, "invert");
  return mapPixels(source, context, [&](int, int, QRgb src) {
    float lum = luminance01(src);
    lum = std::clamp((lum - 0.5f) * contrast + 0.5f, 0.f, 1.f);
    if (invert) lum = 1.f - lum;
    const float quantized = std::nearbyintf(lum * float(levels - 1)) / std::max(1.f, float(levels - 1));
    const QRgb thermal = thermalGradient(quantized);
    const float glowBoost = glow * std::pow(quantized, 2.2f);
    const float tr = float(qRed(thermal)) + float(qRed(thermal)) * glowBoost * 0.20f;
    const float tg = float(qGreen(thermal)) + float(qGreen(thermal)) * glowBoost * 0.25f;
    const float tb = float(qBlue(thermal)) + float(qBlue(thermal)) * glowBoost * 0.35f;
    return qRgba(toByte(lerp(float(qRed(src)), tr, blend)), toByte(lerp(float(qGreen(src)), tg, blend)),
                 toByte(lerp(float(qBlue(src)), tb, blend)), qAlpha(src));
  });
}

// ---- soft_diffusion -------------------------------------------------------------------------------
EffectResult softDiffusion(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = floatOf(params, "amount") / 100.f, radius = floatOf(params, "radius");
  const float bloom = floatOf(params, "highlight_bloom") / 100.f, softness = floatOf(params, "contrast_softening") / 100.f;
  const float warmth = floatOf(params, "warmth") / 100.f;
  if (amount <= 0.0001f) return identityCopy(source, context);
  QImage blurred;
  if (!blurredClamp(source, std::max(0.1f, radius), context, blurred)) return {{}, kCancelled};
  const Pixels blurs(blurred);
  const float postContrast = 1.f - softness * 0.28f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const QRgb blur = blurs.at(x, y);
    const float sr = float(qRed(src)) / 255.f, sg = float(qGreen(src)) / 255.f, sb = float(qBlue(src)) / 255.f;
    const float br = float(qRed(blur)) / 255.f, bg = float(qGreen(blur)) / 255.f, bb = float(qBlue(blur)) / 255.f;
    const float blurLum = luminance01(blur);
    const float bloomMask = 0.45f + bloom * std::pow(blurLum, 1.7f) * 0.85f;
    float r = lerp(sr, screenClamped(sr, br * bloomMask * (1.02f + warmth * 0.08f)), amount);
    float g = lerp(sg, screenClamped(sg, bg * bloomMask * 0.96f), amount);
    float b = lerp(sb, screenClamped(sb, bb * bloomMask * (0.92f - warmth * 0.06f)), amount);
    r = applyContrast(r, postContrast);
    g = applyContrast(g, postContrast);
    b = applyContrast(b, postContrast);
    if (warmth > 0.0001f) {
      const float warmMask = warmth * std::pow(blurLum, 1.35f) * 0.22f;
      r = screenClamped(r, warmMask);
      g = screenClamped(g, warmMask * 0.55f);
      b = lerp(b, std::max(0.f, b - warmMask * 0.12f), warmMask);
    }
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- cyanotype ------------------------------------------------------------------------------------
EffectResult cyanotype(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float contrast = floatOf(params, "contrast") / 100.f, paperTexture = floatOf(params, "paper_texture") / 100.f;
  const float stain = floatOf(params, "stain") / 100.f, grain = floatOf(params, "grain") / 100.f;
  const float vignette = floatOf(params, "vignette") / 100.f;
  const int seed = 1842 ^ seedOf(context);  // the reference fixes 1842; seed 0 reproduces it
  const float centerX = float(source.width() - 1) * 0.5f, centerY = float(source.height() - 1) * 0.5f;
  const float maxDist = std::max(1.f, std::sqrt(centerX * centerX + centerY * centerY));
  const QRgb paper = qRgb(241, 236, 220), mid = qRgb(87, 136, 170), deep = qRgb(11, 45, 92);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float lum = applyContrast(luminance01(src), contrast);
    const float ink = 1.f - lum;
    const QRgb tone = ink < 0.36f ? lerpColor(paper, mid, ink / 0.36f) : lerpColor(mid, deep, (ink - 0.36f) / 0.64f);
    const float paperNoise = (fractalNoise(float(x) * 0.022f, float(y) * 0.022f, 4, 2.1f, 0.55f, seed) - 0.5f) * paperTexture;
    const float stainNoise = fractalNoise(float(x) * 0.006f, float(y) * 0.006f, 5, 2.0f, 0.5f, seed ^ 901);
    const float stainMask = smoothStep(0.56f, 0.92f, stainNoise) * stain;
    const float grainNoise = (hash01(x, y, seed ^ 2111) * 2.f - 1.f) * grain * 0.08f;
    const float radial = std::sqrt(std::pow(float(x) - centerX, 2.f) + std::pow(float(y) - centerY, 2.f)) / maxDist;
    const float vignetteMask = clamp01(1.f - vignette * std::pow(radial, 1.7f) * 0.24f);
    const float r = clamp01((float(qRed(tone)) / 255.f + paperNoise * 0.08f + stainMask * 0.06f + grainNoise) * vignetteMask);
    const float g = clamp01((float(qGreen(tone)) / 255.f + paperNoise * 0.10f + stainMask * 0.08f + grainNoise) * vignetteMask);
    const float b = clamp01((float(qBlue(tone)) / 255.f + paperNoise * 0.14f + stainMask * 0.10f + grainNoise) * vignetteMask);
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- night_vision ---------------------------------------------------------------------------------
EffectResult nightVision(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float intensity = floatOf(params, "intensity") / 100.f, glow = floatOf(params, "glow") / 100.f;
  const float noise = floatOf(params, "noise") / 100.f, vignette = floatOf(params, "vignette") / 100.f;
  const float scanlines = floatOf(params, "scanlines") / 100.f;
  if (intensity <= 0.0001f) return identityCopy(source, context);
  const int w = source.width(), h = source.height();
  const float centerX = float(w - 1) * 0.5f, centerY = float(h - 1) * 0.5f;
  const float maxDist = std::max(1.f, std::sqrt(centerX * centerX + centerY * centerY));
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float lum = luminance01(src);
    float avg = lum;
    avg += luminance01(p.at(std::max(0, x - 1), y));
    avg += luminance01(p.at(std::min(w - 1, x + 1), y));
    avg += luminance01(p.at(x, std::max(0, y - 1)));
    avg += luminance01(p.at(x, std::min(h - 1, y + 1)));
    avg /= 5.f;
    float phosphor = lerp(lum, avg, glow * 0.45f) + std::pow(lum, 0.82f) * glow * 0.26f;
    phosphor *= 0.72f + intensity * 0.68f;
    const float dx = float(x) - centerX, dy = float(y) - centerY;
    const float dist = std::sqrt(dx * dx + dy * dy) / maxDist;
    const float vignetteMask = clamp01(1.f - vignette * dist * dist) * (1.02f - dist * 0.08f);
    const float scan = 1.f - scanlines * 0.16f * (0.5f + 0.5f * std::sin(float(y) * kPi));
    const float grain = (hash01(x, y, 1229) * 2.f - 1.f) * noise * 0.12f;
    const float r = clamp01((phosphor * 0.12f + grain * 0.35f) * vignetteMask * scan);
    const float g = clamp01((phosphor * 1.08f + grain) * vignetteMask * scan);
    const float b = clamp01((phosphor * 0.18f + grain * 0.28f) * vignetteMask * scan);
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- hologram_scan --------------------------------------------------------------------------------
EffectResult hologramScan(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float scanline = floatOf(params, "scanline_strength") / 100.f, glitch = floatOf(params, "glitch_amount") / 100.f;
  const int chroma = intOf(params, "chroma_shift");
  const float glow = floatOf(params, "glow_amount") / 100.f;
  if (scanline <= 0.f && glitch <= 0.f && chroma <= 0 && glow <= 0.f) return identityCopy(source, context);
  const int seed = 6006 ^ seedOf(context);  // the reference fixes 6006; seed 0 reproduces it
  const Pixels p(source);
  QImage result = makeResult(source);
  if (context.cancelled()) return {{}, kCancelled};
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
    const int band = y / 6;
    const float bandNoise = hash01(band, 13, seed);
    const float bandSign = hash01(band, 31, seed) * 2.f - 1.f;
    const float maxShift = glitch * 18.f;
    const float burst = bandNoise > 0.60f ? (bandNoise - 0.60f) / 0.40f : 0.f;
    const float rowShift = burst * maxShift * bandSign + std::sin(float(y) * 0.11f + float(seed) * 0.001f) * glitch * 2.5f;
    const float lineSine = 0.5f + 0.5f * std::sin(float(y) * kPi);
    const float scanlineFactor = 1.f - scanline * (0.30f + 0.70f * lineSine);
    for (int x = 0; x < source.width(); ++x) {
      const float fx = float(x) + rowShift;
      const QRgb sampleR = bilinear(p, fx + float(chroma), float(y));
      const QRgb sampleG = bilinear(p, fx, float(y));
      const QRgb sampleB = bilinear(p, fx - float(chroma), float(y));
      const float r = float(qRed(sampleR)) / 255.f, g = float(qGreen(sampleG)) / 255.f, b = float(qBlue(sampleB)) / 255.f;
      const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      float hr = lum * 0.18f + r * 0.20f, hg = lum * 0.85f + g * 0.35f, hb = lum * 1.25f + b * 0.45f;
      const float pulse = 0.85f + 0.15f * std::sin(float(y) * 0.45f + float(x) * 0.03f + float(seed) * 0.0003f);
      hr *= scanlineFactor * pulse;
      hg *= scanlineFactor * pulse;
      hb *= scanlineFactor * pulse;
      const float glowBoost = glow * std::pow(lum, 2.2f);
      hr += glowBoost * 0.08f;
      hg += glowBoost * 0.25f;
      hb += glowBoost * 0.45f;
      if (bandNoise > 0.92f && hash01(x, y, seed ^ 99) > 0.97f) {
        hr += 0.35f;
        hg += 0.35f;
        hb += 0.35f;
      }
      out[x] = rgbaF(hr, hg, hb, qAlpha(sampleG));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- pointillism ----------------------------------------------------------------------------------
QRgb boostSaturation(QRgb color, float amount) {
  const float r = float(qRed(color)), g = float(qGreen(color)), b = float(qBlue(color));
  const float gray = (r + g + b) / 3.f, factor = 1.f + amount;
  return qRgba(toByte(gray + (r - gray) * factor), toByte(gray + (g - gray) * factor), toByte(gray + (b - gray) * factor), qAlpha(color));
}
EffectResult pointillism(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int dotSize = intOf(params, "dot_size");
  const float density = floatOf(params, "density") / 100.f, jitter = floatOf(params, "jitter") / 100.f;
  const float boost = floatOf(params, "color_boost") / 100.f, backgroundMix = floatOf(params, "background_mix") / 100.f;
  const int seed = 2026 ^ seedOf(context);  // the reference fixes 2026; seed 0 reproduces it
  const int width = source.width(), height = source.height();
  const Pixels p(source);
  QImage surface(source.size(), QImage::Format_ARGB32_Premultiplied);
  surface.fill(Qt::transparent);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  if (backgroundMix > 0.f) {
    painter.setOpacity(double(toByte(backgroundMix * 255.f)) / 255.0);
    painter.drawImage(0, 0, source);
    painter.setOpacity(1.0);
  }
  painter.setPen(Qt::NoPen);
  const int step = std::max(2, dotSize);
  const float jitterAmount = jitter * float(step) * 0.6f;
  for (int gy = 0; gy < height; gy += step) {
    if (context.cancelled()) return {{}, kCancelled};
    for (int gx = 0; gx < width; gx += step) {
      const int cellX = gx / step, cellY = gy / step;
      if (hash01(cellX, cellY, seed) > density) continue;
      const float jx = (hash01(cellX, cellY, seed ^ 111) * 2.f - 1.f) * jitterAmount;
      const float jy = (hash01(cellX, cellY, seed ^ 257) * 2.f - 1.f) * jitterAmount;
      const float cx = std::clamp(float(gx) + float(step) * 0.5f + jx, 0.f, float(width - 1));
      const float cy = std::clamp(float(gy) + float(step) * 0.5f + jy, 0.f, float(height - 1));
      QRgb sample = bilinear(p, cx, cy);
      if (boost > 0.f) sample = boostSaturation(sample, boost);
      const float radiusJitter = 0.6f + hash01(cellX, cellY, seed ^ 509) * 0.9f;
      const float radius = std::max(1.f, float(dotSize) * 0.36f * radiusJitter);
      painter.setBrush(QColor::fromRgba(sample));
      painter.drawEllipse(QPointF(double(cx), double(cy)), double(radius), double(radius));
    }
  }
  painter.end();
  return finishSurface(surface, source);
}

// ---- chromatic_aberration -------------------------------------------------------------------------
EffectResult chromaticAberration(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = floatOf(params, "amount"), edgeStart = floatOf(params, "edgeStart");
  const float strength = floatOf(params, "strength") / 100.f;
  if (amount <= 0.f || strength <= 0.f) return identityCopy(source, context);
  const int w = source.width(), h = source.height();
  const float cx = float(w - 1) * 0.5f, cy = float(h - 1) * 0.5f;
  const float invCx = cx > 0.f ? 1.f / cx : 0.f, invCy = cy > 0.f ? 1.f / cy : 0.f;
  const float invSqrt2 = 0.70710678f;
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb original) {
    const float dxNorm = (float(x) - cx) * invCx, dyNorm = (float(y) - cy) * invCy;
    const float distance01 = std::sqrt(dxNorm * dxNorm + dyNorm * dyNorm) * invSqrt2;
    const float radialMask = smoothStep(edgeStart, 1.f, distance01);
    if (radialMask <= 0.f) return original;
    const float dirLength = std::sqrt(dxNorm * dxNorm + dyNorm * dyNorm);
    const float dirX = dirLength > 0.0001f ? dxNorm / dirLength : 0.f;
    const float dirY = dirLength > 0.0001f ? dyNorm / dirLength : 0.f;
    const float mix = strength * radialMask;
    if (mix <= 0.0001f) return original;
    const float shift = amount * radialMask;
    const QRgb sampleR = bilinear(p, float(x) + dirX * shift, float(y) + dirY * shift);
    const QRgb sampleB = bilinear(p, float(x) - dirX * shift, float(y) - dirY * shift);
    const float splitContrast = smoothStep(0.01f, 0.30f, std::fabs(luminance01(sampleR) - luminance01(sampleB)));
    const float boost = mix * (0.85f + 1.35f * splitContrast);
    const float oR = float(qRed(original)), oG = float(qGreen(original)), oB = float(qBlue(original));
    const float red = lerp(oR, float(qRed(sampleR)), mix) + (float(qRed(sampleR)) - oR) * boost;
    const float green = lerp(oG, (float(qGreen(sampleR)) + oG + float(qGreen(sampleB))) / 3.f, mix * 0.06f);
    const float blue = lerp(oB, float(qBlue(sampleB)), mix) + (float(qBlue(sampleB)) - oB) * boost;
    const float alpha = lerp(float(qAlpha(original)), float(qAlpha(sampleR) + qAlpha(original) + qAlpha(sampleB)) / 3.f, mix);
    return qRgba(toByte(red), toByte(green), toByte(blue), toByte(alpha));
  });
}

// ---- paper_stencil_mask ---------------------------------------------------------------------------
EffectResult paperStencilMask(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float threshold01 = floatOf(params, "threshold") / 255.f;
  const float featherPx = floatOf(params, "feather_radius");
  const float feather = std::clamp(featherPx, 0.f, 30.f) / 30.f;
  const float featherRange = 0.02f + 0.13f * feather;
  const float edgeStrength = floatOf(params, "edge_strength") / 100.f;
  const float backgroundDim = floatOf(params, "background_dim");
  if (edgeStrength <= 0.f && backgroundDim <= 0.f) return identityCopy(source, context);
  const float dimFactor = clamp01(1.f - backgroundDim / 100.f);
  const bool invert = boolOf(params, "invert_mask");
  const QColor stencil = params.value("stencil_color").value<QColor>();
  const float stencilMixCap = float(stencil.alpha()) / 255.f * edgeStrength;
  const float stencilR = float(stencil.red()) / 255.f, stencilG = float(stencil.green()) / 255.f, stencilB = float(stencil.blue()) / 255.f;
  const int seed = 1337 ^ seedOf(context);  // the reference fixes 1337; seed 0 reproduces it
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float lum01 = luminance01(src);
    const float jitter = (hash01(x, y, seed ^ 0xCAFE) * 2.f - 1.f) * 0.003f;
    const float t0 = threshold01 - featherRange + jitter, t1 = threshold01 + featherRange + jitter;
    float maskAlpha = smoothStep(t0, t1, lum01);
    if (invert) maskAlpha = 1.f - maskAlpha;
    const float mix = clamp01(maskAlpha * stencilMixCap);
    const float outR = lerp(float(qRed(src)) / 255.f * dimFactor, stencilR, mix);
    const float outG = lerp(float(qGreen(src)) / 255.f * dimFactor, stencilG, mix);
    const float outB = lerp(float(qBlue(src)) / 255.f * dimFactor, stencilB, mix);
    return rgbaF(outR, outG, outB, qAlpha(src));
  });
}

// ---- infrared_false_color -------------------------------------------------------------------------

EffectResult infraredFalseColor(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float intensity = floatOf(params, "intensity") / 100.f, foliageShift = floatOf(params, "foliage_shift") / 100.f;
  const float skyDarkening = floatOf(params, "sky_darkening") / 100.f, glow = floatOf(params, "glow") / 100.f;
  const float contrast = floatOf(params, "contrast") / 100.f;
  if (intensity <= 0.0001f) return identityCopy(source, context);
  QImage glowImage = source;
  if (glow > 0.01f && !blurredClamp(source, 6.f + glow * 12.f, context, glowImage)) return {{}, kCancelled};
  const Pixels glows(glowImage);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float h, s, l;
    toHsl(src, h, s, l);
    float nh, ns, nl;
    const bool isFoliage = h >= 45.f && h <= 170.f && s >= 18.f;
    const bool isSky = h >= 170.f && h <= 260.f && s >= 12.f;
    if (isFoliage) {
      nh = lerp(h, 8.f + l * 0.16f, foliageShift * (0.55f + intensity * 0.35f));
      ns = std::min(100.f, s * (1.f + 0.18f * intensity + 0.32f * foliageShift));
      nl = std::min(100.f, l + intensity * 14.f + foliageShift * 10.f);
    } else if (isSky) {
      nh = lerp(h, 196.f, 0.22f + intensity * 0.28f);
      ns = std::min(100.f, s * (1.03f + 0.10f * intensity));
      nl = std::max(0.f, l * (1.f - skyDarkening * 0.46f));
    } else {
      nh = lerp(h, h + 6.f, intensity * 0.10f);
      ns = std::min(100.f, s * (1.f + intensity * 0.06f));
      nl = std::clamp(l + intensity * 3.f, 0.f, 100.f);
    }
    const QRgb mapped = fromHsl(nh, ns, nl, qAlpha(src));
    float r = applyContrast(float(qRed(mapped)) / 255.f, contrast);
    float g = applyContrast(float(qGreen(mapped)) / 255.f, contrast);
    float b = applyContrast(float(qBlue(mapped)) / 255.f, contrast);
    if (glow > 0.0001f) {
      float glowMask = std::max(0.f, (luminance01(glows.at(x, y)) - 0.52f) / 0.48f);
      glowMask = std::pow(glowMask, 1.55f) * glow;
      r = screenClamped(r, glowMask * 0.80f);
      g = screenClamped(g, glowMask * 0.18f);
      b = screenClamped(b, glowMask * 0.42f);
    }
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- anime_speed_lines ----------------------------------------------------------------------------
EffectResult animeSpeedLines(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float density = floatOf(params, "density") / 100.f, strength = floatOf(params, "strength") / 100.f;
  const float focusRadius = floatOf(params, "focus_radius") / 100.f, contrast = floatOf(params, "contrast") / 100.f;
  if (strength <= 0.0001f) return identityCopy(source, context);
  const int w = source.width(), h = source.height();
  const float centerX = floatOf(params, "center_x") / 100.f * float(w - 1);
  const float centerY = floatOf(params, "center_y") / 100.f * float(h - 1);
  const float minDim = float(std::min(w, h));
  const float focusPx = minDim * focusRadius;
  const float featherPx = std::max(8.f, minDim * 0.22f);
  const float freq = 40.f + density * 180.f;
  const int seed = 9911 ^ seedOf(context);  // the reference fixes 9911; seed 0 reproduces it
  const float baseDesat = 0.25f + contrast * 0.45f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)), g = float(qGreen(src)), b = float(qBlue(src));
    const float a = float(qAlpha(src));
    const float gray = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    r = lerp(r, gray, baseDesat);
    g = lerp(g, gray, baseDesat);
    b = lerp(b, gray, baseDesat);
    const float dx = float(x) - centerX, dy = float(y) - centerY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float radialMask = smoothStep(focusPx, focusPx + featherPx, dist);
    const float theta = std::atan2(dy, dx);
    const float bandPhase = dist * 0.018f + hash01(int(dist * 0.12f), int(theta * 160.f), seed) * 0.85f;
    const float stripe = 0.5f + 0.5f * std::sin(theta * freq + bandPhase);
    const float brightLine = std::pow(stripe, 36.f), darkLine = std::pow(1.f - stripe, 30.f);
    const float lineMask = radialMask * strength;
    const float brightMix = brightLine * lineMask * 0.92f, darkMix = darkLine * lineMask * 0.58f;
    r = lerp(r, 255.f, brightMix);
    g = lerp(g, 255.f, brightMix);
    b = lerp(b, 255.f, brightMix);
    r = lerp(r, 0.f, darkMix);
    g = lerp(g, 0.f, darkMix);
    b = lerp(b, 0.f, darkMix);
    return qRgba(toByte(r), toByte(g), toByte(b), toByte(a));
  });
}

// ---- border-effect helpers ------------------------------------------------------------------------
// Blurs a premultiplied ARGB32 layer in place (transparent edges), the equivalent of a Skia blur mask filter.
bool blurPremulLayer(QImage &layer, float sigma, const EffectContext &context) {
  if (sigma <= 0.0001f) return true;
  FloatImage f;
  f.w = layer.width();
  f.h = layer.height();
  f.d.resize(size_t(f.w) * size_t(f.h) * 4);
  for (int y = 0; y < f.h; ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(layer.constScanLine(y));
    for (int x = 0; x < f.w; ++x) {
      float *v = &f.d[(size_t(y) * size_t(f.w) + size_t(x)) * 4];
      v[0] = float(qRed(line[x]));
      v[1] = float(qGreen(line[x]));
      v[2] = float(qBlue(line[x]));
      v[3] = float(qAlpha(line[x]));
    }
  }
  if (!gaussianBlur255(f, sigma, false, context)) return false;
  for (int y = 0; y < f.h; ++y) {
    auto *line = reinterpret_cast<QRgb *>(layer.scanLine(y));
    for (int x = 0; x < f.w; ++x) {
      const float *v = &f.d[(size_t(y) * size_t(f.w) + size_t(x)) * 4];
      const int a = toByte(v[3]);
      line[x] = qRgba(std::min(toByte(v[0]), a), std::min(toByte(v[1]), a), std::min(toByte(v[2]), a), a);
    }
  }
  return true;
}
// Strokes `rect` with `color` (alpha in the colour) on its own layer, optionally blurred, and composites it over `surface`.
bool drawStrokedRect(QImage &surface, const QRectF &rect, double width, const QColor &color, float blurSigma,
                     const EffectContext &context) {
  if (context.cancelled()) return false;
  QImage layer(surface.size(), QImage::Format_ARGB32_Premultiplied);
  layer.fill(Qt::transparent);
  {
    QPainter painter(&layer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, width);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
  }
  if (!blurPremulLayer(layer, blurSigma, context)) return false;
  QPainter painter(&surface);
  painter.drawImage(0, 0, layer);
  return true;
}
QColor withAlpha(const QColor &color, int alpha) { return QColor(color.red(), color.green(), color.blue(), clampInt(alpha, 0, 255)); }
// A premultiplied canvas of `size` cleared to `fill` with `source` drawn at (dx, dy).
QImage canvasWithSource(QSize size, QRgb fill, const QImage &source, int dx, int dy) {
  QImage canvas = paintSurface(size, fill);
  QPainter painter(&canvas);
  painter.drawImage(dx, dy, source);
  return canvas;
}

// ---- neon_glow_border (changes size) --------------------------------------------------------------
EffectResult neonGlowBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const QColor neon = colorOf(params, "neon_color"), bg = colorOf(params, "bg_color");
  const float glow = floatOf(params, "glow_intensity") / 100.f;
  const float tube = floatOf(params, "tube_width");
  const bool doubleTube = boolOf(params, "double_tube");
  const int newW = source.width() + border * 2, newH = source.height() + border * 2;
  QImage canvas = canvasWithSource(QSize(newW, newH), bg.rgba(), source, border, border);
  const float tubeOffset = float(border) * 0.5f;
  const QRectF tubeRect(double(tubeOffset), double(tubeOffset), double(newW) - 2.0 * double(tubeOffset), double(newH) - 2.0 * double(tubeOffset));
  const float glowSigma = float(border) * 0.35f * glow;
  if (!drawStrokedRect(canvas, tubeRect, double(tube * 5.f), withAlpha(neon, int(120.f * glow)), glowSigma, context) ||
      !drawStrokedRect(canvas, tubeRect, double(tube * 3.f), withAlpha(neon, int(180.f * glow)), glowSigma * 0.35f, context) ||
      !drawStrokedRect(canvas, tubeRect, double(tube), withAlpha(Qt::white, int(220.f * glow)), 0.f, context) ||
      !drawStrokedRect(canvas, tubeRect, double(tube * 1.4f), withAlpha(neon, int(240.f * glow)), 0.f, context))
    return {{}, kCancelled};
  if (doubleTube) {
    const float inner = float(border) * 0.78f;
    const QRectF innerRect(double(inner), double(inner), double(newW) - 2.0 * double(inner), double(newH) - 2.0 * double(inner));
    if (!drawStrokedRect(canvas, innerRect, double(tube * 2.5f), withAlpha(neon, int(100.f * glow)), glowSigma * 0.25f, context) ||
        !drawStrokedRect(canvas, innerRect, double(tube * 0.8f), withAlpha(neon, int(200.f * glow)), 0.f, context))
      return {{}, kCancelled};
  }
  return finishSurface(canvas, source);
}

// ---- film_strip_frame (changes size) --------------------------------------------------------------
EffectResult filmStripFrame(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const float holeFraction = floatOf(params, "hole_size") / 100.f;
  const QColor frame = colorOf(params, "frame_color");
  const bool sideStrips = boolOf(params, "side_strips"), scratches = boolOf(params, "scratches");
  const int sw = source.width(), sh = source.height();
  const int newW = sw + border * 2, newH = sh + border * 2;
  QImage canvas = paintSurface(QSize(newW, newH), 0);
  QPainter painter(&canvas);
  painter.setPen(Qt::NoPen);
  painter.setBrush(frame);
  painter.drawRect(QRect(0, 0, newW, border));
  painter.drawRect(QRect(0, newH - border, newW, border));
  painter.drawRect(QRect(0, border, border, sh));
  painter.drawRect(QRect(newW - border, border, border, sh));
  painter.drawImage(border, border, source);
  if (scratches) {
    uint32_t seed = 7391;  // C# int arithmetic wraps; the reference reads the seed as uint
    for (int i = 0; i < 10; ++i) {
      if (context.cancelled()) return {{}, kCancelled};
      seed = seed * 1664525u + 1013904223u;
      const float x = float(seed % uint32_t(newW)) / float(newW) * float(newW);
      seed = seed * 1664525u + 1013904223u;
      const float alpha = float(seed % 100u) / 100.f * 0.35f + 0.07f;
      QPen pen(QColor(140, 140, 140, int(alpha * 255.f)), 0);  // cosmetic 1 px line, not antialiased
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setPen(pen);
      painter.drawLine(QPointF(double(x), 0.0), QPointF(double(x), double(newH)));
    }
  }
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  painter.setCompositionMode(QPainter::CompositionMode_Clear);
  const float holeH = float(border) * holeFraction, holeW = holeH;
  const float holeRadius = holeH * 0.15f;
  const float holeMarginV = (float(border) - holeH) / 2.f;
  const float step = holeW * 1.75f;
  for (float x = step * 0.4f; x + holeW <= float(newW) - step * 0.2f; x += step) {
    if (context.cancelled()) return {{}, kCancelled};
    painter.drawRoundedRect(QRectF(double(x), double(holeMarginV), double(holeW), double(holeH)), double(holeRadius), double(holeRadius));
    painter.drawRoundedRect(QRectF(double(x), double(float(newH - border) + holeMarginV), double(holeW), double(holeH)), double(holeRadius), double(holeRadius));
  }
  if (sideStrips) {
    const float sideMargin = (float(border) - holeW) / 2.f;
    for (float y = float(border) + step * 0.4f; y + holeH <= float(newH - border) - step * 0.2f; y += step) {
      if (context.cancelled()) return {{}, kCancelled};
      painter.drawRoundedRect(QRectF(double(sideMargin), double(y), double(holeW), double(holeH)), double(holeRadius), double(holeRadius));
      painter.drawRoundedRect(QRectF(double(float(newW - border) + sideMargin), double(y), double(holeW), double(holeH)), double(holeRadius), double(holeRadius));
    }
  }
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  QPen linePen(QColor(60, 60, 60), 1.2);
  linePen.setJoinStyle(Qt::MiterJoin);
  painter.setPen(linePen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRectF(double(border) - 0.6, double(border) - 0.6, double(sw) + 1.2, double(sh) + 1.2));
  painter.end();
  return finishSurface(canvas, source);
}

// ---- gradient_fade_border (changes size) ----------------------------------------------------------
QRgb lerpStraight(QRgb a, QRgb b, float t) {
  return qRgba(toByte(float(qRed(a)) + float(qRed(b) - qRed(a)) * t), toByte(float(qGreen(a)) + float(qGreen(b) - qGreen(a)) * t),
               toByte(float(qBlue(a)) + float(qBlue(b) - qBlue(a)) * t), toByte(float(qAlpha(a)) + float(qAlpha(b) - qAlpha(a)) * t));
}
EffectResult gradientFadeBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const QRgb outer = colorOf(params, "outer_color").rgba(), mid = colorOf(params, "mid_color").rgba(), inner = colorOf(params, "inner_color").rgba();
  const float midT = floatOf(params, "mid_position") / 100.f;
  const bool glowEdge = boolOf(params, "glow_edge");
  const int sw = source.width(), sh = source.height();
  const int newW = sw + border * 2, newH = sh + border * 2;
  QImage frame(newW, newH, QImage::Format_ARGB32);
  frame.fill(Qt::transparent);
  const bool done = runRows(context, newH, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
    const bool topBand = y < border, bottomBand = y >= border + sh;
    for (int x = 0; x < newW; ++x) {
      const bool leftBand = x < border, rightBand = x >= border + sw;
      if (!topBand && !bottomBand && !leftBand && !rightBand) continue;
      const int distLeft = leftBand ? border - x : 0, distRight = rightBand ? x - border - sw + 1 : 0;
      const int distTop = topBand ? border - y : 0, distBottom = bottomBand ? y - border - sh + 1 : 0;
      const int dist = std::max(std::max(distLeft, distRight), std::max(distTop, distBottom));
      const float t = std::clamp(float(dist) / float(border), 0.f, 1.f);
      if (t < midT) line[x] = lerpStraight(inner, mid, smoothStep(0.f, 1.f, t / midT));
      else line[x] = lerpStraight(mid, outer, smoothStep(0.f, 1.f, (t - midT) / (1.f - midT)));
    }
  });
  if (!done) return {{}, kCancelled};
  QImage canvas = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  {
    QPainter painter(&canvas);
    painter.drawImage(border, border, source);
  }
  if (glowEdge &&
      !drawStrokedRect(canvas, QRectF(double(border), double(border), double(sw), double(sh)), 3.0,
                       QColor(qRed(inner), qGreen(inner), qBlue(inner), 120), 4.f, context))
    return {{}, kCancelled};
  return finishSurface(canvas, source);
}

// ---- engraving ------------------------------------------------------------------------------------
EffectResult engraving(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const float spacing = floatOf(params, "line_spacing"), maxWidth = floatOf(params, "line_width_max");
  const float angleRad = floatOf(params, "angle") * kPi / 180.f;
  const QColor lineColor = colorOf(params, "line_color"), paperColor = colorOf(params, "paper_color");
  std::vector<float> lum(size_t(width) * size_t(height));
  const Pixels p(source);
  for (size_t i = 0; i < lum.size(); ++i) {
    const QRgb c = p.data[i];
    lum[i] = (0.299f * float(qRed(c)) + 0.587f * float(qGreen(c)) + 0.114f * float(qBlue(c))) / 255.f;
  }
  QImage surface = paintSurface(source.size(), paperColor.rgba());
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const float cosA = std::cos(angleRad), sinA = std::sin(angleRad);
  const float diagonal = std::sqrt(float(width * width + height * height));
  const int lineCount = int(std::ceil(diagonal / spacing)) + 1;
  const float cx = float(width) * 0.5f, cy = float(height) * 0.5f;
  QPen pen(lineColor);
  pen.setCapStyle(Qt::RoundCap);
  for (int i = -lineCount; i <= lineCount; ++i) {
    if (context.cancelled()) return {{}, kCancelled};
    const float offset = float(i) * spacing;
    const float basePx = cx + cosA * offset, basePy = cy + sinA * offset;
    const float x0 = basePx - sinA * diagonal, y0 = basePy + cosA * diagonal;
    const float x1 = basePx + sinA * diagonal, y1 = basePy - cosA * diagonal;
    const int steps = int(diagonal * 2.f / spacing) + 1;
    float prevX = x0, prevY = y0;
    for (int s = 1; s <= steps; ++s) {
      const float t = float(s) / float(steps);
      const float px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
      const int sx = int(std::nearbyintf(px)), sy = int(std::nearbyintf(py));
      if (sx >= 0 && sx < width && sy >= 0 && sy < height) {
        const float strokeWidth = (1.f - lum[size_t(sy) * size_t(width) + size_t(sx)]) * maxWidth;
        if (strokeWidth > 0.05f) {
          pen.setWidthF(double(strokeWidth));
          painter.setPen(pen);
          painter.drawLine(QPointF(double(prevX), double(prevY)), QPointF(double(px), double(py)));
        }
      }
      prevX = px;
      prevY = py;
    }
  }
  painter.end();
  return finishSurface(surface, source);
}

// ---- vhs_tape_damage ------------------------------------------------------------------------------
EffectResult vhsTapeDamage(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float distortion = floatOf(params, "distortion") / 100.f, noise = floatOf(params, "noise") / 100.f;
  const float colorBleed = floatOf(params, "color_bleed") / 100.f, tracking = floatOf(params, "tracking") / 100.f;
  const float scanlines = floatOf(params, "scanlines") / 100.f;
  if (distortion <= 0.0001f && noise <= 0.0001f && colorBleed <= 0.0001f && tracking <= 0.0001f && scanlines <= 0.0001f)
    return identityCopy(source, context);
  const Pixels p(source);
  QImage result = makeResult(source);
  if (context.cancelled()) return {{}, kCancelled};
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
    const float rowNoise = hash01(y, 0, 1703) * 2.f - 1.f;
    const float shift = (std::sin(float(y) * 0.085f + 0.7f) * 0.55f + std::cos(float(y) * 0.024f - 1.6f) * 0.45f + rowNoise * 0.65f) * distortion * 10.f;
    float dropoutMask = 0.f;
    if (hash01(y / 3, 4, 4201) > 0.987f) dropoutMask = 0.35f + tracking * 0.45f;
    const float trackingWave = std::pow(std::max(0.f, std::sin(float(y) * 0.022f + 1.1f)), 10.f) * tracking;
    const float scanMask = 1.f - scanlines * 0.18f * (0.5f + 0.5f * std::sin(float(y) * kPi));
    for (int x = 0; x < source.width(); ++x) {
      const float bleedPx = 0.5f + colorBleed * 4.5f;
      const float sx = float(x) + shift;
      const QRgb rS = bilinear(p, sx + bleedPx, float(y)), gS = bilinear(p, sx, float(y)), bS = bilinear(p, sx - bleedPx, float(y));
      float r = float(qRed(rS)), g = float(qGreen(gS)), b = float(qBlue(bS));
      const float a = float(qAlpha(gS));
      const float lineNoise = (hash01(x, y, 6113) * 2.f - 1.f) * noise * 28.f;
      r += lineNoise * 1.05f;
      g += lineNoise;
      b += lineNoise * 0.95f;
      if (trackingWave > 0.0001f) {
        const float lift = trackingWave * 42.f;
        r = lerp(r, 255.f, trackingWave * 0.18f);
        g += lift * 0.22f;
        b += lift * 0.30f;
      }
      if (dropoutMask > 0.0001f && hash01(x / 4, y, 7001) > 0.35f) {
        r *= 1.f - dropoutMask;
        g *= 1.f - dropoutMask * 0.95f;
        b *= 1.f - dropoutMask * 0.90f;
      }
      r *= scanMask;
      g *= scanMask;
      b *= scanMask;
      out[x] = qRgba(toByte(r), toByte(g), toByte(b), toByte(a));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- wet_plate_collodion --------------------------------------------------------------------------
EffectResult wetPlateCollodion(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float contrast = floatOf(params, "contrast") / 100.f, stains = floatOf(params, "plate_stains") / 100.f;
  const float scratches = floatOf(params, "scratches") / 100.f, silvering = floatOf(params, "silvering") / 100.f;
  const float vignette = floatOf(params, "vignette") / 100.f;
  const int seed = 1855 ^ seedOf(context);  // the reference fixes 1855; seed 0 reproduces it
  const int width = source.width(), height = source.height();
  const float centerX = float(width - 1) * 0.5f, centerY = float(height - 1) * 0.5f;
  const float maxDist = std::max(1.f, std::sqrt(centerX * centerX + centerY * centerY));
  const QRgb shadow = qRgb(20, 16, 18), highlight = qRgb(220, 213, 202);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float lum = applyContrast(luminance01(src), contrast);
    const QRgb tone = lerpColor(shadow, highlight, std::pow(lum, 0.88f));
    const float chemNoise = fractalNoise(float(x) * 0.008f, float(y) * 0.008f, 5, 2.05f, 0.52f, seed);
    const float streakNoise = fractalNoise(float(x) * 0.024f, float(y) * 0.0026f, 4, 2.2f, 0.48f, seed ^ 337);
    float stainMask = smoothStep(0.54f, 0.92f, chemNoise) * stains;
    stainMask += std::max(0.f, streakNoise - 0.72f) * stains * 0.45f;
    const float edgeDistance = float(std::min(std::min(x, width - 1 - x), std::min(y, height - 1 - y)));
    const float edge01 = 1.f - std::clamp(edgeDistance / std::max(1.f, float(std::min(width, height)) * 0.16f), 0.f, 1.f);
    const float silverMask = silvering * edge01 * std::pow(std::max(lum, 0.02f), 0.55f);
    const float radial = std::sqrt(std::pow(float(x) - centerX, 2.f) + std::pow(float(y) - centerY, 2.f)) / maxDist;
    const float vignetteMask = clamp01(1.f - vignette * std::pow(radial, 1.95f) * 0.78f);
    float scratchMask = 0.f;
    if (hash01(x / 3, y, seed ^ 173) > 0.9975f) scratchMask += scratches * 0.55f;
    if (hash01(x, y / 5, seed ^ 811) > 0.9984f) scratchMask += scratches * 0.35f;
    float r = float(qRed(tone)) / 255.f, g = float(qGreen(tone)) / 255.f, b = float(qBlue(tone)) / 255.f;
    r = lerp(r, 0.88f, silverMask * 0.34f);
    g = lerp(g, 0.84f, silverMask * 0.30f);
    b = lerp(b, 0.92f, silverMask * 0.38f);
    r = clamp01((r - stainMask * 0.10f + scratchMask) * vignetteMask);
    g = clamp01((g - stainMask * 0.08f + scratchMask) * vignetteMask);
    b = clamp01((b - stainMask * 0.06f + scratchMask) * vignetteMask);
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- smoke_overlay --------------------------------------------------------------------------------
EffectResult smokeOverlay(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float density = floatOf(params, "density") / 100.f, scale = floatOf(params, "scale") / 100.f;
  const float drift = floatOf(params, "drift") / 100.f, softness = floatOf(params, "softness") / 100.f;
  const float contrast = floatOf(params, "contrast") / 100.f;
  if (density <= 0.0001f) return identityCopy(source, context);
  const float coarseFreq = 0.012f + (1.f - scale) * 0.035f;
  const float mediumFreq = coarseFreq * 2.2f, fineFreq = coarseFreq * 4.4f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float sr = float(qRed(src)) / 255.f, sg = float(qGreen(src)) / 255.f, sb = float(qBlue(src)) / 255.f;
    const float driftOffset = drift * float(y) * 0.014f;
    const float noiseX = (float(x) + driftOffset) * coarseFreq, noiseY = float(y) * coarseFreq;
    const float warpX = (fractalNoise(noiseX * 0.85f + 7.2f, noiseY * 0.85f - 3.1f, 3, 2.05f, 0.56f, 911) * 2.f - 1.f) * (0.75f + scale * 1.45f);
    const float warpY = (fractalNoise(noiseX * 0.90f - 5.4f, noiseY * 0.90f + 11.8f, 3, 2.00f, 0.56f, 1201) * 2.f - 1.f) * (0.60f + scale * 1.15f);
    const float sampleX = noiseX + warpX, sampleY = noiseY + warpY;
    const float coarse = fractalNoise(sampleX, sampleY, 4, 2.00f, 0.55f, 1559);
    const float medium = fractalNoise(sampleX * (mediumFreq / coarseFreq) + 13.1f, sampleY * (mediumFreq / coarseFreq) - 7.6f, 3, 2.10f, 0.52f, 2081);
    const float fine = fractalNoise(sampleX * (fineFreq / coarseFreq) - 9.4f, sampleY * (fineFreq / coarseFreq) + 4.7f, 2, 2.20f, 0.58f, 2593);
    const float flowA = std::sin(float(x) * coarseFreq * 16.f + float(y) * coarseFreq * 6.f + 0.7f);
    const float flowB = std::cos(float(x) * coarseFreq * 9.f - float(y) * coarseFreq * 13.f - 1.4f);
    const float wisps = 1.f - std::fabs(medium * 2.f - 1.f);
    float smoke = coarse * 0.58f + wisps * 0.24f + fine * 0.18f;
    smoke = clamp01(smoke + flowA * 0.08f + flowB * 0.06f);
    smoke = clamp01((smoke - 0.5f) * (0.92f + contrast * 1.5f) + 0.5f);
    const float threshold = 0.44f - density * 0.16f;
    float alpha = smoothStep(threshold, 0.96f, smoke);
    alpha = std::pow(alpha, 1.55f - softness * 1.10f) * density * 0.68f;
    const float outR = lerp(sr, 0.82f + smoke * 0.10f, alpha);
    const float outG = lerp(sg, 0.84f + smoke * 0.10f, alpha);
    const float outB = lerp(sb, 0.86f + smoke * 0.12f, alpha);
    return rgbaF(outR, outG, outB, qAlpha(src));
  });
}

// ---- block_glitch ---------------------------------------------------------------------------------
EffectResult blockGlitch(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const int blockCount = intOf(params, "block_count");
  const int minW = intOf(params, "min_block_width"), maxW = std::clamp(intOf(params, "max_block_width"), minW, 900);
  const int minH = intOf(params, "min_block_height"), maxH = std::clamp(intOf(params, "max_block_height"), minH, 500);
  const int maxShift = intOf(params, "max_displacement"), channelShift = intOf(params, "channel_shift");
  const float noise = floatOf(params, "noise_amount") / 100.f;
  const Pixels p(source);
  QImage result = copyOf(source);
  auto *dst = reinterpret_cast<QRgb *>(result.bits());
  SplitMix64 random(context.seed);
  auto next = [&](int lo, int hiExclusive) { return random.range(lo, hiExclusive); };
  for (int i = 0; i < blockCount; ++i) {
    if (context.cancelled()) return {{}, kCancelled};
    const int bw = next(minW, maxW + 1), bh = next(minH, maxH + 1);
    const int x = next(0, std::max(1, width - 1)), y = next(0, std::max(1, height - 1));
    const int right = std::min(width, x + bw), bottom = std::min(height, y + bh);
    if (right <= x || bottom <= y) continue;
    const int dx = next(-maxShift, maxShift + 1);
    const int dy = next(-std::max(1, maxShift / 6), std::max(2, maxShift / 6 + 1));
    const int redShift = next(-channelShift, channelShift + 1), blueShift = next(-channelShift, channelShift + 1);
    for (int py = y; py < bottom; ++py) {
      if (context.cancelled()) return {{}, kCancelled};
      for (int px = x; px < right; ++px) {
        const int sx = clampInt(px + dx, 0, width - 1), sy = clampInt(py + dy, 0, height - 1);
        const QRgb baseColor = p.at(sx, sy);
        const QRgb rColor = p.at(clampInt(sx + redShift, 0, width - 1), sy);
        const QRgb bColor = p.at(clampInt(sx - blueShift, 0, width - 1), sy);
        const float nr = noise > 0.f ? (random.unit() * 2.f - 1.f) * noise * 48.f : 0.f;
        const float ng = noise > 0.f ? (random.unit() * 2.f - 1.f) * noise * 48.f : 0.f;
        const float nb = noise > 0.f ? (random.unit() * 2.f - 1.f) * noise * 48.f : 0.f;
        dst[size_t(py) * size_t(width) + size_t(px)] =
            qRgba(toByte(float(qRed(rColor)) + nr), toByte(float(qGreen(baseColor)) + ng), toByte(float(qBlue(bColor)) + nb), qAlpha(baseColor));
      }
    }
  }
  return {result, {}};
}

// ---- polaroid_frame (changes size) ----------------------------------------------------------------
QFont captionFont(float pixelSize) {
  QFont font = QGuiApplication::font();
  const QStringList families = QFontDatabase::families();
  for (const QString &family : {QStringLiteral("Segoe Script"), QStringLiteral("Comic Sans MS")})
    if (families.contains(family)) {
      font.setFamily(family);
      break;
    }
  font.setPixelSize(std::max(1, int(std::lround(pixelSize))));
  return font;
}
EffectResult polaroidFrame(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size"), bottom = intOf(params, "bottom_size"), shadowSize = intOf(params, "shadow_size");
  const float rotation = floatOf(params, "rotation");
  const QColor frameColor = colorOf(params, "frame_color");
  const QString caption = choiceOf(params, "caption");
  const bool hasCaption = !caption.trimmed().isEmpty();
  if (hasCaption && !QGuiApplication::instance()) return {{}, kUnavailable};  // text needs a QGuiApplication for fonts
  const int sw = source.width(), sh = source.height();
  const int pw = sw + border * 2, ph = sh + border + bottom;
  const float radians = std::fabs(rotation) * kPi / 180.f;
  const float cs = std::cos(radians), sn = std::sin(radians);
  const int rotatedW = int(std::ceil(float(pw) * cs + float(ph) * sn)), rotatedH = int(std::ceil(float(pw) * sn + float(ph) * cs));
  const int canvasW = std::max(rotatedW, pw) + shadowSize * 2 + 20, canvasH = std::max(rotatedH, ph) + shadowSize * 2 + 20;
  QImage canvas = paintSurface(QSize(canvasW, canvasH), 0);
  QTransform transform;
  transform.translate(double(canvasW) / 2.0, double(canvasH) / 2.0);
  transform.rotate(double(rotation));
  transform.translate(-double(pw) / 2.0, -double(ph) / 2.0);
  if (shadowSize > 0) {
    QImage layer(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
      QPainter painter(&layer);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setTransform(transform);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0, 0, 0, 50));
      painter.drawRect(QRectF(0, 0, pw, ph));
    }
    if (!blurPremulLayer(layer, float(shadowSize) / 2.f, context)) return {{}, kCancelled};
    QPainter painter(&canvas);
    painter.drawImage(0, 0, layer);
  }
  if (context.cancelled()) return {{}, kCancelled};
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setTransform(transform);
  painter.setPen(Qt::NoPen);
  painter.setBrush(frameColor);
  painter.drawRect(QRectF(0, 0, pw, ph));
  QPen edge(QColor(200, 198, 190), 1.0);
  edge.setJoinStyle(Qt::MiterJoin);
  painter.setPen(edge);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRectF(0, 0, pw, ph));
  painter.drawImage(border, border, source);
  QPen inner(QColor(180, 175, 165, 100), 1.0);
  inner.setJoinStyle(Qt::MiterJoin);
  painter.setPen(inner);
  painter.drawRect(QRectF(border, border, sw, sh));
  if (hasCaption) {
    const float fontSize = std::max(12.f, float(bottom) * 0.3f);
    const QFont font = captionFont(fontSize);
    painter.setFont(font);
    painter.setPen(QColor(60, 55, 50));
    const double textX = double(pw) / 2.0, textY = double(sh + border) + double(bottom) / 2.0 + double(fontSize) / 3.0;
    const double textWidth = QFontMetricsF(font).horizontalAdvance(caption);
    painter.drawText(QPointF(textX - textWidth / 2.0, textY), caption);
  }
  painter.end();
  return finishSurface(canvas, source);
}

// ---- embroidery -----------------------------------------------------------------------------------
int quantizeChannel(int value, int levels) {
  const float step = 255.f / float(levels - 1);
  const int quantized = int(std::nearbyintf(float(value) / step));
  return clampInt(int(std::nearbyintf(float(quantized) * step)), 0, 255);
}
EffectResult embroidery(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const int size = intOf(params, "stitch_size"), colorCount = intOf(params, "color_count");
  const bool showGrid = boolOf(params, "show_grid");
  const QColor fabric = colorOf(params, "fabric_color");
  const int levels = std::max(2, int(std::ceil(std::pow(float(colorCount), 1.f / 3.f))));
  const Pixels p(source);
  QImage surface = paintSurface(source.size(), fabric.rgba());
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const int cols = (width + size - 1) / size, rows = (height + size - 1) / size;
  QPen pen;
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  for (int gy = 0; gy < rows; ++gy) {
    if (context.cancelled()) return {{}, kCancelled};
    for (int gx = 0; gx < cols; ++gx) {
      const int x0 = gx * size, y0 = gy * size, x1 = std::min(x0 + size, width), y1 = std::min(y0 + size, height);
      qint64 sumR = 0, sumG = 0, sumB = 0;
      int count = 0;
      for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
          const QRgb c = p.at(px, py);
          sumR += qRed(c);
          sumG += qGreen(c);
          sumB += qBlue(c);
          ++count;
        }
      if (count == 0) continue;
      const int avgR = quantizeChannel(int(sumR / count), levels), avgG = quantizeChannel(int(sumG / count), levels),
                avgB = quantizeChannel(int(sumB / count), levels);
      const float margin = float(size) * 0.1f;
      const double sx = double(float(x0) + margin), sy = double(float(y0) + margin);
      const double ex = double(float(x1) - margin), ey = double(float(y1) - margin);
      pen.setColor(QColor(avgR, avgG, avgB));
      pen.setWidthF(double(std::max(1.f, float(size) * 0.15f)));
      painter.setPen(pen);
      painter.drawLine(QPointF(sx, sy), QPointF(ex, ey));
      painter.drawLine(QPointF(sx, ey), QPointF(ex, sy));
    }
  }
  if (showGrid) {
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(clampInt(fabric.red() - 30, 0, 255), clampInt(fabric.green() - 30, 0, 255), clampInt(fabric.blue() - 30, 0, 255), 80), 0));
    for (int gx = 0; gx <= cols; ++gx) {
      const double x = std::min(gx * size, width - 1);
      painter.drawLine(QPointF(x, 0), QPointF(x, double(height)));
    }
    for (int gy = 0; gy <= rows; ++gy) {
      const double y = std::min(gy * size, height - 1);
      painter.drawLine(QPointF(0, y), QPointF(double(width), y));
    }
  }
  painter.end();
  return finishSurface(surface, source);
}

// ---- wave_edge ------------------------------------------------------------------------------------
EffectResult waveEdge(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int depth = intOf(params, "depth"), range = intOf(params, "range");
  const bool top = boolOf(params, "top"), right = boolOf(params, "right"), bottom = boolOf(params, "bottom"), left = boolOf(params, "left");
  if (!top && !right && !bottom && !left) return identityCopy(source, context);
  if (context.cancelled()) return {{}, kCancelled};
  const int w = source.width(), h = source.height();
  std::vector<QPointF> points;
  const int horizontalWaveCount = std::max(2, (w / range + 1) / 2 * 2) - 1;
  const int verticalWaveCount = std::max(2, (h / range + 1) / 2 * 2) - 1;
  const int horizontalWaveRange = std::max(1, w / horizontalWaveCount), verticalWaveRange = std::max(1, h / verticalWaveCount);
  const int step = std::min(std::max(1, range / depth), 10);
  auto wave = [&](int t, int max) { return double(float((1.0 - std::cos(double(t) * 3.14159265358979323846 / double(max))) * double(depth) / 2.0)); };
  if (top) {
    const int startX = left ? depth : 0, endX = right ? w - depth : w;
    for (int x = startX; x < endX; x += step) points.emplace_back(x, wave(x, horizontalWaveRange));
    points.emplace_back(endX, wave(endX, horizontalWaveRange));
  } else {
    points.emplace_back(0, 0);
  }
  if (right) {
    const int startY = top ? depth : 0, endY = bottom ? h - depth : h;
    for (int y = startY; y < endY; y += step) points.emplace_back(double(w - depth) + wave(y, verticalWaveRange), y);
    points.emplace_back(double(w - depth) + wave(endY, verticalWaveRange), endY);
  } else {
    points.emplace_back(w, points.back().y());
  }
  if (bottom) {
    const int startX = right ? w - depth : w, endX = left ? depth : 0;
    for (int x = startX; x >= endX; x -= step) points.emplace_back(x, double(h - depth) + wave(x, horizontalWaveRange));
    points.emplace_back(endX, double(h - depth) + wave(endX, horizontalWaveRange));
  } else {
    points.emplace_back(points.back().x(), h);
  }
  if (left) {
    const int startY = bottom ? h - depth : h, endY = top ? depth : 0;
    for (int y = startY; y >= endY; y -= step) points.emplace_back(wave(y, verticalWaveRange), y);
    points.emplace_back(wave(endY, verticalWaveRange), endY);
  } else {
    points.emplace_back(0, points.back().y());
  }
  if (!top) points[0] = QPointF(points.back().x(), 0);
  if (points.size() <= 2) return identityCopy(source, context);
  QPolygonF polygon;
  for (const QPointF &point : points) polygon << point;
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  path.addPolygon(polygon);
  path.closeSubpath();
  QImage surface = paintSurface(source.size(), 0);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QBrush(source));
  painter.drawPath(path);
  painter.end();
  if (context.cancelled()) return {{}, kCancelled};
  return finishSurface(surface, source);
}

// ---- crystal_prism --------------------------------------------------------------------------------
EffectResult crystalPrism(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int facet = intOf(params, "facetSize");
  const float refraction = floatOf(params, "refraction"), dispersion = floatOf(params, "dispersion");
  const float sparkle = floatOf(params, "sparkle") / 100.f;
  if (refraction <= 0.f && dispersion <= 0.f && sparkle <= 0.f) return identityCopy(source, context);
  const int seed = 4771 ^ seedOf(context);  // the reference fixes 4771; seed 0 reproduces it
  const float twoPi = kPi * 2.f;
  const float blendStrength = 0.58f + 0.25f * (refraction / 30.f);
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb original) {
    const int cellX = x / facet, cellY = y / facet;
    const float h0 = hash01(cellX, cellY, seed), h1 = hash01(cellX, cellY, seed ^ 149);
    const float h2 = hash01(cellX, cellY, seed ^ 313), h3 = hash01(cellX, cellY, seed ^ 341);
    const float centerX = (float(cellX) + 0.20f + 0.60f * h0) * float(facet);
    const float centerY = (float(cellY) + 0.20f + 0.60f * h1) * float(facet);
    const float dx = float(x) - centerX, dy = float(y) - centerY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float radial = 1.f - clamp01(dist / (float(facet) * 0.85f));
    const float angle = h2 * twoPi;
    const float dirX = std::cos(angle), dirY = std::sin(angle);
    const float offsetMag = refraction * (0.30f + 0.70f * radial);
    const float sampleX = float(x) + dirX * offsetMag, sampleY = float(y) + dirY * offsetMag;
    const float disp = dispersion * (0.25f + 0.75f * radial);
    const QRgb cR = bilinear(p, sampleX + dirY * disp, sampleY - dirX * disp);
    const QRgb cG = bilinear(p, sampleX, sampleY);
    const QRgb cB = bilinear(p, sampleX - dirY * disp, sampleY + dirX * disp);
    const float shade = 0.88f + (h3 - 0.5f) * 0.30f;
    float pr = float(qRed(cR)) / 255.f * shade, pg = float(qGreen(cG)) / 255.f * shade, pb = float(qBlue(cB)) / 255.f * shade;
    const float fx = float(x % facet) / float(facet), fy = float(y % facet) / float(facet);
    const float border = std::min(std::min(fx, 1.f - fx), std::min(fy, 1.f - fy));
    const float edge = 1.f - smoothStep(0.04f, 0.28f, border);
    const float sparkleMask = edge * edge * sparkle;
    pr += sparkleMask * 0.32f;
    pg += sparkleMask * 0.34f;
    pb += sparkleMask * 0.38f;
    const float r = lerp(float(qRed(original)) / 255.f, pr, blendStrength);
    const float g = lerp(float(qGreen(original)) / 255.f, pg, blendStrength);
    const float b = lerp(float(qBlue(original)) / 255.f, pb, blendStrength);
    return rgbaF(r, g, b, qAlpha(original));
  });
}

// ---- luminance_contour_lines ----------------------------------------------------------------------
EffectResult luminanceContourLines(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int levels = intOf(params, "levels");
  const float backgroundStrength = floatOf(params, "background_strength") / 100.f, lineStrength = floatOf(params, "line_strength") / 100.f;
  if (lineStrength <= 0.f && backgroundStrength <= 0.f) return identityCopy(source, context);
  const float thresholdBias = floatOf(params, "threshold") / 255.f;
  const float lineWidth01 = floatOf(params, "line_width") / 200.f;
  const float feather = 0.04f + 0.22f * lineWidth01;
  const bool invert = boolOf(params, "invert");
  const QColor lineColor = params.value("line_color").value<QColor>();
  const float lineA = float(lineColor.alpha()) / 255.f;
  const float lineR = float(lineColor.red()) / 255.f, lineG = float(lineColor.green()) / 255.f, lineB = float(lineColor.blue()) / 255.f;
  return mapPixels(source, context, [&](int, int, QRgb src) {
    const float lum = clamp01(luminance01(src) + thresholdBias);
    const float scaled = lum * float(levels);
    const float qIndex = std::floor(scaled);
    const float frac = scaled - qIndex;
    const float qLum = clamp01((qIndex + 0.5f) / float(levels));
    const float distToBoundary = std::min(frac, 1.f - frac);
    const float t = smoothStep(0.f, feather, distToBoundary);
    float lineMask = 1.f - t;
    lineMask = lineMask * lineMask;
    if (invert) lineMask = 1.f - lineMask;
    const float bgLum = clamp01(lerp(lum, qLum, backgroundStrength));
    const float lineMix = lineMask * lineStrength * lineA;
    return rgbaF(lerp(bgLum, lineR, lineMix), lerp(bgLum, lineG, lineMix), lerp(bgLum, lineB, lineMix), qAlpha(src));
  });
}

// ---- disposable_camera ----------------------------------------------------------------------------
EffectResult disposableCamera(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float flash = floatOf(params, "flash") / 100.f, softness = floatOf(params, "softness");
  const float grain = floatOf(params, "grain") / 100.f, vignette = floatOf(params, "vignette") / 100.f;
  const float warmth = floatOf(params, "warmth") / 100.f;
  if (flash <= 0.0001f && softness <= 0.0001f && grain <= 0.0001f && vignette <= 0.0001f && warmth <= 0.0001f)
    return identityCopy(source, context);
  const int seed = 1996 ^ seedOf(context);  // the reference fixes 1996; seed 0 reproduces it
  const int w = source.width(), h = source.height();
  const float centerX = float(w - 1) * 0.56f, centerY = float(h - 1) * 0.44f;
  const float maxDist = std::max(1.f, std::sqrt(centerX * centerX + centerY * centerY));
  QImage blurred = source;
  if (softness > 0.01f && !blurredClamp(source, softness, context, blurred)) return {{}, kCancelled};
  const Pixels blurs(blurred);
  const float sigmaInv = 1.f / std::max(0.0001f, 2.f * std::pow(0.22f + flash * 0.20f, 2.f));
  const float softnessMix = softness / 24.f * 0.34f;
  const float contrast = 1.08f + flash * 0.16f + warmth * 0.08f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const QRgb soft = blurs.at(x, y);
    float r = lerp(float(qRed(src)) / 255.f, float(qRed(soft)) / 255.f, softnessMix);
    float g = lerp(float(qGreen(src)) / 255.f, float(qGreen(soft)) / 255.f, softnessMix);
    float b = lerp(float(qBlue(src)) / 255.f, float(qBlue(soft)) / 255.f, softnessMix);
    r = applyContrast(r, contrast);
    g = applyContrast(g, contrast);
    b = applyContrast(b, contrast);
    const float dx = (float(x) - centerX) / float(std::max(1, w - 1)), dy = (float(y) - centerY) / float(std::max(1, h - 1));
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float flashMask = flash * std::exp(-(dist * dist) * sigmaInv);
    r = screenClamped(r, flashMask * (0.22f + warmth * 0.10f));
    g = screenClamped(g, flashMask * 0.18f);
    b = screenClamped(b, flashMask * (0.11f - warmth * 0.02f));
    if (warmth > 0.0001f) {
      const float warmMix = warmth * (0.14f + flashMask * 0.30f);
      r = lerp(r, std::min(1.f, r + 0.22f), warmMix);
      g = lerp(g, std::min(1.f, g + 0.10f), warmMix * 0.75f);
      b = lerp(b, std::max(0.f, b - 0.05f), warmMix * 0.70f);
    }
    const float radial = std::sqrt(std::pow(float(x) - float(w - 1) * 0.5f, 2.f) + std::pow(float(y) - float(h - 1) * 0.5f, 2.f)) / maxDist;
    const float vignetteMask = clamp01(1.f - vignette * std::pow(radial, 1.85f) * 0.62f);
    const float noise = (hash01(x, y, seed) * 2.f - 1.f) * grain * 0.11f;
    r = clamp01(r * vignetteMask + noise * 1.02f);
    g = clamp01(g * vignetteMask + noise);
    b = clamp01(b * vignetteMask + noise * 0.92f);
    return rgbaF(r, g, b, qAlpha(src));
  });
}

// ---- lens_blur ------------------------------------------------------------------------------------
EffectResult lensBlur(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int radius = intOf(params, "radius");
  const float threshold = floatOf(params, "highlight_threshold") / 100.f, boost = floatOf(params, "highlight_boost") / 100.f;
  const bool useBoost = boost > 0.001f;
  const float thresholdInv = 1.f / std::max(0.0001f, 1.f - threshold);
  if (radius <= 1 && boost <= 0.01f) return identityCopy(source, context);
  struct Offset { int x, y; float weight; };
  std::vector<Offset> kernel;
  const int radiusSq = radius * radius;
  for (int ky = -radius; ky <= radius; ++ky)
    for (int kx = -radius; kx <= radius; ++kx) {
      const int distSq = kx * kx + ky * ky;
      if (distSq > radiusSq) continue;
      kernel.push_back({kx, ky, 1.f - std::sqrt(float(distSq)) * (1.f / float(radius)) * 0.35f});
    }
  const Pixels p(source);
  std::vector<float> lum;
  if (useBoost) {
    lum.resize(size_t(source.width()) * size_t(source.height()));
    for (size_t i = 0; i < lum.size(); ++i) lum[i] = luminance01(p.data[i]);
  }
  return mapPixels(source, context, [&](int x, int y, QRgb self) {
    float sumR = 0, sumG = 0, sumB = 0, sumA = 0, sumW = 0;
    for (const Offset &k : kernel) {
      const int sx = clampInt(x + k.x, 0, p.w - 1), sy = clampInt(y + k.y, 0, p.h - 1);
      const QRgb c = p.at(sx, sy);
      float highlight = 1.f;
      if (useBoost) {
        const float l = lum[size_t(sy) * size_t(p.w) + size_t(sx)];
        if (l > threshold) highlight += (l - threshold) * thresholdInv * boost;
      }
      const float wt = k.weight * highlight;
      sumR += float(qRed(c)) * wt;
      sumG += float(qGreen(c)) * wt;
      sumB += float(qBlue(c)) * wt;
      sumA += float(qAlpha(c)) * wt;
      sumW += wt;
    }
    if (sumW <= 0.0001f) return self;
    const float inv = 1.f / sumW;
    return qRgba(toByte(sumR * inv), toByte(sumG * inv), toByte(sumB * inv), toByte(sumA * inv));
  });
}

// ---- chalk_drawing --------------------------------------------------------------------------------
EffectResult chalkDrawing(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const float detail = floatOf(params, "detail") / 100.f, roughness = floatOf(params, "roughness") / 100.f;
  const QColor chalk = colorOf(params, "chalk_color"), board = colorOf(params, "board_color");
  const Pixels p(source);
  const size_t count = size_t(width) * size_t(height);
  std::vector<float> lum(count), edge(count, 0.f), noise(count);
  for (size_t i = 0; i < count; ++i) {
    const QRgb c = p.data[i];
    lum[i] = (0.299f * float(qRed(c)) + 0.587f * float(qGreen(c)) + 0.114f * float(qBlue(c))) / 255.f;
  }
  auto L = [&](int x, int y) { return lum[size_t(y) * size_t(width) + size_t(x)]; };
  float maxEdge = 0.f;
  const bool sobelDone = runRows(context, height, 0, 50, [&](int y) {
    if (y < 1 || y > height - 2) return;
    for (int x = 1; x < width - 1; ++x) {
      const float gx = -L(x - 1, y - 1) + L(x + 1, y - 1) - 2.f * L(x - 1, y) + 2.f * L(x + 1, y) - L(x - 1, y + 1) + L(x + 1, y + 1);
      const float gy = -L(x - 1, y - 1) - 2.f * L(x, y - 1) - L(x + 1, y - 1) + L(x - 1, y + 1) + 2.f * L(x, y + 1) + L(x + 1, y + 1);
      const float magnitude = std::sqrt(gx * gx + gy * gy);
      edge[size_t(y) * size_t(width) + size_t(x)] = magnitude;
      if (magnitude > maxEdge) maxEdge = magnitude;
    }
  });
  if (!sobelDone) return {{}, kCancelled};
  if (maxEdge > 0.f) {
    const float invMax = 1.f / maxEdge;
    for (float &e : edge) e *= invMax;
  }
  int32_t rng = 12345;  // C# int xorshift: signed, arithmetic right shift
  for (size_t i = 0; i < count; ++i) {
    rng = xorshift(rng);
    noise[i] = float(rng & 0x7FFFFFFF) / 2147483647.f;
  }
  const float edgeWeight = 0.3f + detail * 0.7f, lumWeight = 1.f - edgeWeight * 0.5f;
  const float boardR = float(board.red()) / 255.f, boardG = float(board.green()) / 255.f, boardB = float(board.blue()) / 255.f;
  const float chalkR = float(chalk.red()) / 255.f, chalkG = float(chalk.green()) / 255.f, chalkB = float(chalk.blue()) / 255.f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const size_t i = size_t(y) * size_t(width) + size_t(x);
    float intensity = std::clamp(edge[i] * edgeWeight + lum[i] * lumWeight, 0.f, 1.f);
    intensity *= 1.f - roughness * (1.f - noise[i]) * 0.6f;
    intensity *= std::pow(lum[i], 0.5f);
    intensity = std::clamp(intensity, 0.f, 1.f);
    return rgbaF(boardR + (chalkR - boardR) * intensity, boardG + (chalkG - boardG) * intensity,
                 boardB + (chalkB - boardB) * intensity, qAlpha(src));
  });
}

// ---- zigzag_border (changes size) -----------------------------------------------------------------
QPainterPath zigzagInnerPath(int canvasW, int canvasH, int border, float teethW, float teethH) {
  const float imgL = float(border), imgT = float(border), imgR = float(canvasW - border), imgB = float(canvasH - border);
  QPolygonF poly;
  auto line = [&](float x, float y) { poly << QPointF(double(x), double(y)); };
  float x = imgL;
  line(imgL, imgT);
  while (x < imgR) {
    const float nextX = std::min(x + teethW, imgR), midX = x + teethW * 0.5f;
    if (midX < imgR) {
      line(midX, imgT + teethH);
      line(nextX, imgT);
    } else {
      line(nextX, imgT);
    }
    x = nextX;
  }
  float y = imgT;
  while (y < imgB) {
    const float nextY = std::min(y + teethW, imgB), midY = y + teethW * 0.5f;
    if (midY < imgB) {
      line(imgR - teethH, midY);
      line(imgR, nextY);
    } else {
      line(imgR, nextY);
    }
    y = nextY;
  }
  x = imgR;
  while (x > imgL) {
    const float nextX = std::max(x - teethW, imgL), midX = x - teethW * 0.5f;
    if (midX > imgL) {
      line(midX, imgB - teethH);
      line(nextX, imgB);
    } else {
      line(nextX, imgB);
    }
    x = nextX;
  }
  y = imgB;
  while (y > imgT) {
    const float nextY = std::max(y - teethW, imgT), midY = y - teethW * 0.5f;
    if (midY > imgT) {
      line(imgL + teethH, midY);
      line(imgL, nextY);
    } else {
      line(imgL, nextY);
    }
    y = nextY;
  }
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  path.addPolygon(poly);
  path.closeSubpath();
  return path;
}
EffectResult zigzagBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const float teethH = floatOf(params, "teeth_height"), teethW = floatOf(params, "teeth_width");
  const QColor frame = colorOf(params, "frame_color"), accent = colorOf(params, "accent_color");
  const bool stripe = boolOf(params, "accent_stripe");
  const int newW = source.width() + border * 2, newH = source.height() + border * 2;
  const QPainterPath path = zigzagInnerPath(newW, newH, border, teethW, teethH);
  // The source clipped to the zigzag shape: the antialiased path coverage is a mask the source is drawn through (SourceIn).
  QImage layer(newW, newH, QImage::Format_ARGB32_Premultiplied);
  layer.fill(Qt::transparent);
  {
    QPainter painter(&layer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(path);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.drawImage(border, border, source);
  }
  if (context.cancelled()) return {{}, kCancelled};
  QImage canvas = paintSurface(QSize(newW, newH), frame.rgba());
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.drawImage(0, 0, layer);
  if (stripe) {
    QPen pen(accent, double(std::max(2.f, float(border) * 0.1f)));
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }
  painter.end();
  return finishSurface(canvas, source);
}

// ---- mosaic_tile_border (changes size) ------------------------------------------------------------
// SKColor.FromHsv (h 0..360, s and v 0..100), truncating to bytes like SkiaSharp.

EffectResult mosaicTileBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const float tileSz = floatOf(params, "tile_size"), groutW = floatOf(params, "grout_width");
  const float variety = floatOf(params, "color_variety") / 100.f, sat = floatOf(params, "saturation");
  const QRgb grout = colorOf(params, "grout_color").rgba();
  const int seed = 5923;  // a constant in the reference, not a settable seed
  const int sw = source.width(), sh = source.height();
  const int newW = sw + border * 2, newH = sh + border * 2;
  const float groutHalf = groutW * 0.5f;
  QImage frame(newW, newH, QImage::Format_ARGB32);
  frame.fill(Qt::transparent);
  const bool done = runRows(context, newH, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
    const bool topBand = y < border, bottomBand = y >= border + sh;
    for (int x = 0; x < newW; ++x) {
      const bool leftBand = x < border, rightBand = x >= border + sw;
      if (!topBand && !bottomBand && !leftBand && !rightBand) continue;
      const int tileCol = int(std::floor(float(x) / tileSz)), tileRow = int(std::floor(float(y) / tileSz));
      float wx = std::fmod(float(x), tileSz), wy = std::fmod(float(y), tileSz);
      if (wx < 0.f) wx += tileSz;
      if (wy < 0.f) wy += tileSz;
      if (wx < groutHalf || wx > tileSz - groutHalf || wy < groutHalf || wy > tileSz - groutHalf) {
        line[x] = grout;
        continue;
      }
      const float hue = hash01(tileCol, tileRow, seed) * 360.f * variety;
      const float brightness = 55.f + hash01(tileCol ^ 0x55, tileRow ^ 0xAA, seed ^ 0x17) * 40.f;
      const QRgb tile = fromHsv(hue, sat, brightness);
      const float distFromEdge = std::min(std::min(wx - groutHalf, tileSz - groutHalf - wx), std::min(wy - groutHalf, tileSz - groutHalf - wy));
      const float bevelShade = smoothStep(tileSz * 0.15f, 0.f, distFromEdge) * 0.2f;
      const float noise = (hash01(x, y, seed ^ 0x99) - 0.5f) * 0.06f;
      line[x] = rgbaF(float(qRed(tile)) / 255.f - bevelShade + noise, float(qGreen(tile)) / 255.f - bevelShade + noise,
                      float(qBlue(tile)) / 255.f - bevelShade + noise, 255);
    }
  });
  if (!done) return {{}, kCancelled};
  QPainter(&frame).drawImage(border, border, source);
  // Inner grout line: a non-antialiased stroke of the rectangle (border - w/2, border - w/2, sw + w, sh + w), i.e. the
  // ring between (border - w) and border around the picture; a pixel belongs to it when its centre lies inside.
  const float outerL = float(border) - groutW, outerT = outerL, outerR = float(border + sw) + groutW, outerB = float(border + sh) + groutW;
  for (int y = std::max(0, int(std::floor(outerT)) - 1); y < std::min(newH, int(std::ceil(outerB)) + 1); ++y) {
    auto *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
    const float cy = float(y) + 0.5f;
    for (int x = std::max(0, int(std::floor(outerL)) - 1); x < std::min(newW, int(std::ceil(outerR)) + 1); ++x) {
      const float cx = float(x) + 0.5f;
      const bool inOuter = cx >= outerL && cx < outerR && cy >= outerT && cy < outerB;
      const bool inInner = cx >= float(border) && cx < float(border + sw) && cy >= float(border) && cy < float(border + sh);
      if (inOuter && !inInner) line[x] = grout;
    }
  }
  frame.setColorSpace(source.colorSpace());
  return {frame, {}};
}

}  // namespace

void registerFilterEffects3(EffectRegistry &registry) {
  add(registry, "crosshatch", "Crosshatch", "Crosshatch",
      {intParam("lineSpacing", "Line spacing", 8, 4, 24), floatParam("lineThickness", "Line thickness", 1.2, 0.5, 4),
       floatParam("contrast", "Contrast", 120, 50, 200), intParam("layerCount", "Layer count", 4, 1, 6)},
      crosshatch);
  add(registry, "surface_blur", "Surface blur", "SurfaceBlur",
      {intParam("radius", "Radius", 3, 1, 8), intParam("threshold", "Threshold", 24, 1, 100)}, surfaceBlur);
  add(registry, "rgb_split", "RGB split", "RGBSplit",
      {intParam("offset_red_x", "Offset red X", -5, -100, 100), intParam("offset_red_y", "Offset red Y", 0, -100, 100),
       intParam("offset_green_x", "Offset green X", 0, -100, 100), intParam("offset_green_y", "Offset green Y", 0, -100, 100),
       intParam("offset_blue_x", "Offset blue X", 5, -100, 100), intParam("offset_blue_y", "Offset blue Y", 0, -100, 100)},
      rgbSplit);
  add(registry, "dot_matrix", "Dot Matrix", "DotMatrix",
      {intParam("cell_size", "Cell Size", 8, 3, 30), floatParam("dot_scale", "Dot Scale", 0.85, 0.1, 1),
       boolParam("color_mode", "Color", false), colorParam("dot_color", "Dot Color", QColor(20, 20, 20)),
       colorParam("paper_color", "Paper Color", QColor(250, 245, 235))},
      dotMatrix);
  add(registry, "edge_feather", "Edge feather", "EdgeFeather", {floatParam("radius", "Radius", 12, 0, 100)}, edgeFeather);
  add(registry, "reflection", "Reflection", "Reflection",
      {intParam("percentage", "Percentage", 20, 1, 100), intParam("max_alpha", "Max Alpha", 255, 0, 255),
       intParam("min_alpha", "Min Alpha", 0, 0, 255), intParam("offset", "Offset", 0, 0, 100), boolParam("skew", "Skew", false),
       intParam("skew_size", "Skew Size", 25, 1, 100)},
      reflection, false, true);
  add(registry, "spin_blur", "Spin blur", "SpinBlur",
      {floatParam("angle", "Angle", 20, 0, 180), intParam("samples", "Samples", 24, 4, 64),
       floatParam("center_x", "Center X", 50, 0, 100), floatParam("center_y", "Center Y", 50, 0, 100)},
      spinBlur);
  add(registry, "x_ray_scan", "X-ray scan", "XRayScan",
      {floatParam("contrast", "Contrast", 70, 0, 100), floatParam("glow", "Glow", 60, 0, 100),
       floatParam("edge_boost", "Edge boost", 68, 0, 100), floatParam("scanlines", "Scanlines", 40, 0, 100),
       floatParam("noise", "Noise", 14, 0, 100)},
      xRayScan);
  add(registry, "double_exposure", "Double exposure", "DoubleExposure",
      {floatParam("blend_amount", "Blend amount", 62, 0, 100), intParam("offset_x", "Offset X", 22, -500, 500),
       intParam("offset_y", "Offset Y", -14, -500, 500), floatParam("ghost_blur", "Ghost blur", 8, 0, 30),
       floatParam("highlight_bias", "Highlight bias", 55, 0, 100), floatParam("contrast", "Contrast", 112, 50, 200)},
      doubleExposure);
  add(registry, "glare", "Glare", "Glare",
      {floatParam("position_x", "Position X (%)", 50, 0, 100), floatParam("position_y", "Position Y (%)", 30, 0, 100),
       floatParam("radius", "Radius", 30, 5, 100), floatParam("intensity", "Intensity", 80, 0, 100),
       intParam("ray_count", "Ray count", 6, 0, 12), floatParam("ray_length", "Ray length", 50, 0, 100),
       colorParam("color", "Color", QColor(255, 255, 255))},
      glare);
  add(registry, "anaglyph_3d", "Anaglyph 3D", "Anaglyph3D",
      {enumParam("mode", "Mode", "red_cyan", {"red_cyan", "amber_blue", "green_magenta"}),
       intParam("separation_x", "Separation X", 10, -100, 100), intParam("separation_y", "Separation Y", 0, -100, 100),
       floatParam("ghost_reduction", "Ghost reduction", 45, 0, 100)},
      anaglyph3d);
  add(registry, "star_filter", "Star filter", "StarFilter",
      {floatParam("threshold", "Threshold", 74, 0, 100), floatParam("length", "Length", 40, 4, 80),
       floatParam("strength", "Strength", 58, 0, 100), floatParam("rotation", "Rotation", 0, 0, 360),
       floatParam("warmth", "Warmth", 48, 0, 100)},
      starFilter);
  add(registry, "thermal_vision", "Thermal vision", "ThermalVision",
      {intParam("levels", "Levels", 8, 3, 16), floatParam("contrast", "Contrast", 135, 50, 200),
       floatParam("glow", "Glow", 28, 0, 100), floatParam("blend", "Blend", 100, 0, 100), boolParam("invert", "Invert", false)},
      thermalVision);
  add(registry, "soft_diffusion", "Soft diffusion", "SoftDiffusion",
      {floatParam("amount", "Amount", 52, 0, 100), floatParam("radius", "Radius", 12, 0, 30),
       floatParam("highlight_bloom", "Highlight bloom", 40, 0, 100),
       floatParam("contrast_softening", "Contrast softening", 28, 0, 100), floatParam("warmth", "Warmth", 22, 0, 100)},
      softDiffusion);
  add(registry, "cyanotype", "Cyanotype", "Cyanotype",
      {floatParam("contrast", "Contrast", 118, 50, 200), floatParam("paper_texture", "Paper texture", 34, 0, 100),
       floatParam("stain", "Stain", 28, 0, 100), floatParam("grain", "Grain", 12, 0, 100),
       floatParam("vignette", "Vignette", 20, 0, 100)},
      cyanotype, true);
  add(registry, "night_vision", "Night vision", "NightVision",
      {floatParam("intensity", "Intensity", 78, 0, 100), floatParam("glow", "Glow", 42, 0, 100),
       floatParam("noise", "Noise", 18, 0, 100), floatParam("vignette", "Vignette", 45, 0, 100),
       floatParam("scanlines", "Scanlines", 35, 0, 100)},
      nightVision);
  add(registry, "hologram_scan", "Hologram scan", "HologramScan",
      {floatParam("scanline_strength", "Scanline strength", 45, 0, 100), floatParam("glitch_amount", "Glitch amount", 25, 0, 100),
       intParam("chroma_shift", "Chroma shift", 2, 0, 12), floatParam("glow_amount", "Glow amount", 30, 0, 100)},
      hologramScan, true);
  add(registry, "pointillism", "Pointillism", "Pointillism",
      {intParam("dot_size", "Dot size", 7, 2, 24), floatParam("density", "Density", 72, 10, 100),
       floatParam("jitter", "Jitter", 65, 0, 100), floatParam("color_boost", "Color boost", 20, 0, 100),
       floatParam("background_mix", "Background mix", 20, 0, 100)},
      pointillism, true);
  add(registry, "chromatic_aberration", "Chromatic aberration", "ChromaticAberration",
      {floatParam("amount", "Amount", 8, 0, 40), floatParam("edgeStart", "Edge start", 0.20, 0, 0.95),
       floatParam("strength", "Strength", 75, 0, 100)},
      chromaticAberration);
  add(registry, "paper_stencil_mask", "Paper stencil mask", "PaperStencilMask",
      {floatParam("threshold", "Threshold", 140, 0, 255), floatParam("feather_radius", "Feather radius", 8, 0, 200),
       floatParam("edge_strength", "Edge strength", 70, 0, 100), floatParam("background_dim", "Background dim", 35, 0, 100),
       boolParam("invert_mask", "Invert mask", false), colorParam("stencil_color", "Stencil color", QColor(0, 0, 0, 220))},
      paperStencilMask, true);
  add(registry, "infrared_false_color", "Infrared false color", "InfraredFalseColor",
      {floatParam("intensity", "Intensity", 68, 0, 100), floatParam("foliage_shift", "Foliage shift", 78, 0, 100),
       floatParam("sky_darkening", "Sky darkening", 44, 0, 100), floatParam("glow", "Glow", 20, 0, 100),
       floatParam("contrast", "Contrast", 118, 50, 200)},
      infraredFalseColor);
  add(registry, "anime_speed_lines", "Anime speed lines", "AnimeSpeedLines",
      {floatParam("density", "Density", 70, 10, 100), floatParam("strength", "Strength", 65, 0, 100),
       floatParam("focus_radius", "Focus radius", 18, 0, 80), floatParam("center_x", "Center X", 50, 0, 100),
       floatParam("center_y", "Center Y", 50, 0, 100), floatParam("contrast", "Contrast", 35, 0, 100)},
      animeSpeedLines, true);
  add(registry, "neon_glow_border", "Neon glow border", "NeonGlowBorder",
      {intParam("border_size", "Border size", 50, 10, 200), colorParam("neon_color", "Neon color", QColor(0, 255, 200)),
       floatParam("glow_intensity", "Glow intensity", 75, 10, 100), floatParam("tube_width", "Tube width", 4, 1, 20),
       colorParam("bg_color", "Background color", QColor(10, 10, 18)), boolParam("double_tube", "Double tube", false)},
      neonGlowBorder, false, true);
  add(registry, "film_strip_frame", "Film strip frame", "FilmStripFrame",
      {intParam("border_size", "Border size", 40, 10, 120), floatParam("hole_size", "Hole size", 55, 20, 80),
       colorParam("frame_color", "Frame color", QColor(15, 15, 15)), boolParam("side_strips", "Side strips", false),
       boolParam("scratches", "Scratches", true)},
      filmStripFrame, false, true);
  add(registry, "gradient_fade_border", "Gradient fade border", "GradientFadeBorder",
      {intParam("border_size", "Border size", 50, 10, 300), colorParam("outer_color", "Outer color", QColor(10, 10, 35)),
       colorParam("mid_color", "Mid color", QColor(60, 20, 120)), colorParam("inner_color", "Inner color", QColor(180, 80, 200)),
       floatParam("mid_position", "Mid-color position %", 50, 10, 90), boolParam("glow_edge", "Inner glow edge", true)},
      gradientFadeBorder, false, true);
  add(registry, "engraving", "Engraving", "Engraving",
      {floatParam("line_spacing", "Line Spacing", 5, 2, 20), floatParam("angle", "Angle", 45, 0, 180),
       floatParam("line_width_max", "Max Line Width", 2.5, 0.5, 8), colorParam("line_color", "Line Color", QColor(20, 20, 40)),
       colorParam("paper_color", "Paper Color", QColor(245, 240, 230))},
      engraving);
  add(registry, "vhs_tape_damage", "VHS tape damage", "VhsTapeDamage",
      {floatParam("distortion", "Distortion", 55, 0, 100), floatParam("noise", "Noise", 28, 0, 100),
       floatParam("color_bleed", "Color bleed", 30, 0, 100), floatParam("tracking", "Tracking", 24, 0, 100),
       floatParam("scanlines", "Scanlines", 48, 0, 100)},
      vhsTapeDamage);
  add(registry, "wet_plate_collodion", "Wet plate collodion", "WetPlateCollodion",
      {floatParam("contrast", "Contrast", 132, 50, 200), floatParam("plate_stains", "Plate stains", 34, 0, 100),
       floatParam("scratches", "Scratches", 24, 0, 100), floatParam("silvering", "Silvering", 36, 0, 100),
       floatParam("vignette", "Vignette", 58, 0, 100)},
      wetPlateCollodion, true);
  add(registry, "smoke_overlay", "Smoke overlay", "SmokeOverlay",
      {floatParam("density", "Density", 42, 0, 100), floatParam("scale", "Scale", 68, 0, 100),
       floatParam("drift", "Drift", 18, -100, 100), floatParam("softness", "Softness", 55, 0, 100),
       floatParam("contrast", "Contrast", 48, 0, 100)},
      smokeOverlay);
  add(registry, "block_glitch", "Block glitch / Databending", "BlockGlitch",
      {intParam("block_count", "Block count", 36, 1, 240), intParam("min_block_width", "Min block width", 24, 4, 400),
       intParam("max_block_width", "Max block width", 200, 4, 900), intParam("min_block_height", "Min block height", 6, 2, 200),
       intParam("max_block_height", "Max block height", 50, 2, 500), intParam("max_displacement", "Max displacement", 50, 0, 500),
       intParam("channel_shift", "Channel shift", 4, 0, 64), floatParam("noise_amount", "Noise amount", 10, 0, 100)},
      blockGlitch, true);
  add(registry, "polaroid_frame", "Polaroid frame", "PolaroidFrame",
      {intParam("border_size", "Border size", 30, 5, 100), intParam("bottom_size", "Bottom size", 80, 20, 200),
       floatParam("rotation", "Rotation", 0, -30, 30), colorParam("frame_color", "Frame color", QColor(250, 249, 245)),
       textParam("caption", "Caption", QString()), intParam("shadow_size", "Shadow size", 15, 0, 60)},
      polaroidFrame, false, true);
  add(registry, "embroidery", "Embroidery", "Embroidery",
      {intParam("stitch_size", "Stitch Size", 10, 4, 40), intParam("color_count", "Color Count", 16, 2, 64),
       boolParam("show_grid", "Show Grid", true), colorParam("fabric_color", "Fabric Color", QColor(240, 235, 220))},
      embroidery);
  add(registry, "wave_edge", "Wave edge", "WaveEdge",
      {intParam("depth", "Depth", 15, 1, 100), intParam("range", "Range", 20, 1, 100), boolParam("top", "Top", true),
       boolParam("right", "Right", true), boolParam("bottom", "Bottom", true), boolParam("left", "Left", true)},
      waveEdge);
  add(registry, "crystal_prism", "Crystal prism", "CrystalPrism",
      {intParam("facetSize", "Facet size", 24, 6, 96), floatParam("refraction", "Refraction", 8, 0, 30),
       floatParam("dispersion", "Dispersion", 4, 0, 20), floatParam("sparkle", "Sparkle", 25, 0, 100)},
      crystalPrism, true);
  add(registry, "luminance_contour_lines", "Luminance contour lines", "LuminanceContourLines",
      {intParam("levels", "Levels", 12, 2, 64), floatParam("line_width", "Line width", 6, 0, 200),
       floatParam("line_strength", "Line strength", 65, 0, 100), floatParam("background_strength", "Background strength", 20, 0, 100),
       floatParam("threshold", "Threshold", 0, 0, 255), boolParam("invert", "Invert", false),
       colorParam("line_color", "Line color", QColor(0, 0, 0))},
      luminanceContourLines);
  add(registry, "disposable_camera", "Disposable camera", "DisposableCamera",
      {floatParam("flash", "Flash", 55, 0, 100), floatParam("softness", "Softness", 8, 0, 24), floatParam("grain", "Grain", 24, 0, 100),
       floatParam("vignette", "Vignette", 34, 0, 100), floatParam("warmth", "Warmth", 42, 0, 100)},
      disposableCamera, true);
  add(registry, "lens_blur", "Lens blur (bokeh)", "LensBlur",
      {intParam("radius", "Radius", 8, 1, 15), floatParam("highlight_threshold", "Highlight threshold", 70, 0, 100),
       floatParam("highlight_boost", "Highlight boost", 85, 0, 200)},
      lensBlur);
  add(registry, "chalk_drawing", "Chalk Drawing", "ChalkDrawing",
      {floatParam("detail", "Detail", 50, 0, 100), floatParam("roughness", "Roughness", 40, 0, 100),
       colorParam("chalk_color", "Chalk Color", QColor(255, 255, 255)), colorParam("board_color", "Board Color", QColor(30, 50, 40))},
      chalkDrawing);
  add(registry, "zigzag_border", "Zigzag border", "ZigZagBorder",
      {intParam("border_size", "Border size", 36, 8, 200), floatParam("teeth_height", "Teeth height", 40, 5, 80),
       floatParam("teeth_width", "Teeth width", 24, 4, 80), colorParam("frame_color", "Frame color", QColor(40, 40, 50)),
       colorParam("accent_color", "Accent color", QColor(220, 180, 60)), boolParam("accent_stripe", "Accent stripe", true)},
      zigzagBorder, false, true);
  add(registry, "mosaic_tile_border", "Mosaic tile border", "MosaicTileBorder",
      {intParam("border_size", "Border size", 48, 12, 300), floatParam("tile_size", "Tile size", 14, 4, 60),
       colorParam("grout_color", "Grout color", QColor(210, 205, 195)), floatParam("grout_width", "Grout width", 2, 0.5, 8),
       floatParam("color_variety", "Color variety", 70, 0, 100), floatParam("saturation", "Saturation", 85, 0, 100)},
      mosaicTileBorder, false, true);
}
}  // namespace xerahs::effects
