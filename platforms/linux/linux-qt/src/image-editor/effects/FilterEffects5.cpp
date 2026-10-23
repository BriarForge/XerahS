// EDITOR-EFFECTS-001: the Filters category, batch 5. EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census source
// path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Skia blurs (SKImageFilter.CreateBlur) are a true separable Gaussian on premultiplied float pixels
//    with the same sigma; Skia switches to box-blur passes for large sigma. Edge handling follows the
//    reference (decal = transparent outside, or clamp where the reference pads with a clamp shader).
//  - Skia vector drawing (paths, lines, gradients, text) is QPainter with antialiasing; pixel coverage
//    at shape edges can differ by a few levels. Text uses the application default font.
//  - Where the reference has a settable Seed property (heat_haze_refraction, datamosh_smear, and the
//    others noted at their effect) the hash seed is that default XOR the low/high halves of
//    EffectContext::seed, so seed 0 reproduces the reference and other seeds vary it (usesSeed).
//    Hash constants fixed in the reference stay fixed and those effects do not use the seed.
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
#include <functional>
#include <limits>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;
const QString kReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");
constexpr float kPi = 3.14159265358979f;

// ---- plumbing -------------------------------------------------------------------------------------

// Converts a painted premultiplied surface back to straight ARGB32, polling cancellation per row.
EffectResult finishSurfaceRows(const QImage &surface, const QImage &like, const EffectContext &context) {
  QImage out(surface.size(), QImage::Format_ARGB32);
  out.setColorSpace(like.colorSpace());
  if (out.isNull()) return {{}, kFailed};
  const bool done = runRows(context, surface.height(), 0, 100, [&](int y) {
    const auto *in = reinterpret_cast<const QRgb *>(surface.constScanLine(y));
    auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < surface.width(); ++x) dst[x] = qUnpremultiply(in[x]);
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
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

// Skia stroke defaults: butt caps and mitre joins (limit 4).
QPen strokePen(const QColor &color, qreal width) {
  QPen pen(color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
  pen.setMiterLimit(4);
  return pen;
}
// A filled circle with an SKMaskFilter Normal blur: drawn on a small patch, Gaussian blurred, then composited.
bool drawBlurredCircle(QPainter &painter, QPointF centre, double radius, const QColor &color, float sigma, const EffectContext &context) {
  const int pad = int(std::ceil(radius + sigma * 3.f)) + 1;
  const int side = pad * 2 + 1;
  const int ox = int(std::floor(centre.x())) - pad, oy = int(std::floor(centre.y())) - pad;
  QImage patch(side, side, QImage::Format_ARGB32_Premultiplied);
  patch.fill(Qt::transparent);
  {
    QPainter pp(&patch);
    pp.setRenderHint(QPainter::Antialiasing, true);
    pp.setPen(Qt::NoPen);
    pp.setBrush(color);
    pp.drawEllipse(QPointF(centre.x() - ox, centre.y() - oy), radius, radius);
  }
  const QImage straight = patch.convertToFormat(QImage::Format_ARGB32);
  FloatImage f = toPremul(straight);
  if (!gaussianBlur255(f, sigma, false, context)) return false;
  painter.drawImage(ox, oy, fromPremul(f, straight));
  return true;
}

// Straight copy-in of the source into a bordered canvas (reference: DrawBitmap onto the new bitmap).

// Straight-alpha source-over of `src` onto `dst`.

// DrawBitmap(source, border, border): source-over, which is a plain copy wherever the canvas is empty or the pixel opaque.

// Runs a per-pixel border painter over the frame band only, lets `post` finish the frame (it only

// ---- rope_border ----------------------------------------------------------------------------------
EffectResult ropeBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int border = intOf(params, "border_size");
  const float twistFreq = floatOf(params, "twist_density") / 1000.f;
  const float fiber = floatOf(params, "fiber_detail") / 100.f;
  const QColor rope = colorOf(params, "rope_color"), bg = colorOf(params, "bg_color");
  const float rR = float(rope.red()) / 255.f, rG = float(rope.green()) / 255.f, rB = float(rope.blue()) / 255.f;
  const int nw = source.width() + border * 2, nh = source.height() + border * 2;
  const float ropeRadius = float(border) * 0.42f;
  const int seed = 8271;
  return paintBorder(source, border, context, [&](int x, int y) {
    const bool topBand = y < border, bottomBand = y >= border + source.height(), leftBand = x < border;
    float along, across;
    const float centerCross = float(border) * 0.5f;
    int boardSeed;
    if (topBand) {
      along = float(x);
      across = float(y) - centerCross;
      boardSeed = seed ^ 0x1A;
    } else if (bottomBand) {
      along = float(nw - x);
      across = -((float(y - (nh - border))) - (float(border) - centerCross));
      boardSeed = seed ^ 0x2B;
    } else if (leftBand) {
      along = float(nh - y);
      across = float(x) - centerCross;
      boardSeed = seed ^ 0x3C;
    } else {
      along = float(y);
      across = -((float(x - (nw - border))) - (float(border) - centerCross));
      boardSeed = seed ^ 0x4D;
    }
    float bestDist = std::numeric_limits<float>::max(), bestShade = 0.f;
    const float strandRadius = ropeRadius * 0.38f;
    for (int s = 0; s < 3; ++s) {
      const float phase = float(s) * (kPi * 2.f / 3.f);
      const float offset = std::sin(along * twistFreq * kPi * 2.f + phase) * ropeRadius * 0.45f;
      const float dist = std::fabs(across - offset);
      if (dist < strandRadius && dist < bestDist) {
        bestDist = dist;
        const float n = dist / strandRadius;
        bestShade = 1.f - n * n;
      }
    }
    if (bestDist < ropeRadius * 0.38f) {
      const float fiberNoise =
          fiber > 0.f ? (hash01(int(along * 1.5f), int(across * 3.f), boardSeed) * 2.f - 1.f) * fiber * 0.18f : 0.f;
      const float twistHighlight = std::cos(along * twistFreq * kPi * 2.f) * 0.08f;
      const float shade = bestShade * 0.35f + twistHighlight + fiberNoise;
      return qRgba(toByte((rR + shade) * 255.f), toByte((rG + shade * 0.8f) * 255.f), toByte((rB + shade * 0.5f) * 255.f), 255);
    }
    return bg.rgba();
  });
}

// ---- heat_haze_refraction -------------------------------------------------------------------------
EffectResult heatHaze(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float strength01 = floatOf(params, "strength") / 100.f;
  if (strength01 <= 0.f) return identityCopy(source, context);
  const float frequency01 = floatOf(params, "frequency") / 100.f;
  const float blurPx = std::min(floatOf(params, "blur_radius"), 30.f);  // reference clamps the radius to 30 internally
  const float maxOffsetPx = floatOf(params, "offset") * (0.5f + 1.2f * frequency01) * strength01;
  const float lumInfluence = floatOf(params, "luminance_influence") / 100.f;
  const int seed = 2026 ^ seedOf(context);
  const int w = source.width(), h = source.height();
  QImage blurred;
  const bool useBlur = blurPx > 0.01f && strength01 > 0.01f;
  if (useBlur) {
    FloatImage f = toPremul(source);
    if (!gaussianBlur255(f, blurPx, false, context)) return {{}, kCancelled};
    blurred = fromPremul(f, source);
  }
  const Pixels src(source);
  const float invW = 1.f / float(std::max(1, w - 1)), invH = 1.f / float(std::max(1, h - 1));
  const float phase = float(seed % 10000) * 0.017f;
  const Pixels bl(useBlur ? blurred : source);
  return mapPixels(source, context, [&](int x, int y, QRgb s) {
    const float lum01 = (0.2126f * float(qRed(s)) + 0.7152f * float(qGreen(s)) + 0.0722f * float(qBlue(s))) / 255.f;
    const float lumWeight = lerp(1.f, lum01, lumInfluence);
    const float u = float(x) * invW, v = float(y) * invH;
    const float w1 = std::sin((u * 10.f + v * 6.f) * (0.6f + frequency01 * 1.8f) + phase);
    const float w2 = std::cos((u * 7.f - v * 12.f) * (0.4f + frequency01 * 1.7f) - phase * 1.23f);
    const float dx = (w1 * 0.60f + w2 * 0.40f) * maxOffsetPx * lumWeight;
    const float dy = (w2 * 0.55f - w1 * 0.45f) * maxOffsetPx * lumWeight;
    const QRgb warped = bilinear(src, float(x) + dx, float(y) + dy);
    float wr = float(qRed(warped)) / 255.f, wg = float(qGreen(warped)) / 255.f, wb = float(qBlue(warped)) / 255.f;
    if (useBlur) {
      const QRgb b = bl.at(x, y);
      const float mix = strength01 * (0.25f + 0.75f * (blurPx / 30.f));
      wr = lerp(wr, float(qRed(b)) / 255.f, mix);
      wg = lerp(wg, float(qGreen(b)) / 255.f, mix);
      wb = lerp(wb, float(qBlue(b)) / 255.f, mix);
    }
    return qRgba(toByte(lerp(float(qRed(s)) / 255.f, wr, strength01) * 255.f), toByte(lerp(float(qGreen(s)) / 255.f, wg, strength01) * 255.f),
                 toByte(lerp(float(qBlue(s)) / 255.f, wb, strength01) * 255.f), qAlpha(s));
  });
}

// ---- old_camera_flash_burn ------------------------------------------------------------------------
EffectResult oldCameraFlashBurn(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float flashStrength = floatOf(params, "flash_strength") / 100.f;
  const float flashRadius = floatOf(params, "flash_radius") / 100.f;
  const float edgeBurn = floatOf(params, "edge_burn") / 100.f;
  const float warmth = floatOf(params, "warmth");
  const float grain = floatOf(params, "grain") / 100.f;
  if (flashStrength <= 0.0001f && edgeBurn <= 0.0001f && grain <= 0.0001f) return identityCopy(source, context);
  const int right = std::max(1, source.width() - 1), bottom = std::max(1, source.height() - 1);
  const float cx = floatOf(params, "center_x") / 100.f * float(right), cy = floatOf(params, "center_y") / 100.f * float(bottom);
  const float invW = 1.f / float(right), invH = 1.f / float(bottom);
  const float sigma = 0.06f + flashRadius * 0.38f;
  const float sigmaInv = 1.f / std::max(0.0001f, 2.f * sigma * sigma);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)), g = float(qGreen(src)), b = float(qBlue(src));
    const float a = float(qAlpha(src));
    const float dx = (float(x) - cx) * invW, dy = (float(y) - cy) * invH;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float flash = flashStrength * std::exp(-(dist * dist) * sigmaInv);
    if (flash > 0.0001f) {
      const float shoulder = std::pow(flash, 0.72f);
      r = lerp(r, 255.f, shoulder);
      g = lerp(g, 242.f + warmth * 0.10f, shoulder * 0.94f);
      b = lerp(b, 218.f - warmth * 0.08f, shoulder * 0.82f);
    }
    if (edgeBurn > 0.0001f) {
      const float ring = smoothStep(0.54f, 1.05f, dist);
      if (ring > 0.0001f) {
        const float nA = hash01(int(float(x) * 0.12f), int(float(y) * 0.12f), 611);
        const float nB = hash01(int(float(x) * 0.032f), int(float(y) * 0.032f), 1201);
        const float burn = ring * edgeBurn * (0.68f + 0.32f * (nA * 0.6f + nB * 0.4f));
        r = lerp(r, 220.f + warmth * 0.22f, burn * 0.44f);
        g = lerp(g, 98.f + warmth * 0.18f, burn * 0.38f);
        b = lerp(b, 36.f + warmth * 0.12f, burn * 0.30f);
        const float dark = burn * 0.45f;
        r *= 1.f - dark * 0.60f;
        g *= 1.f - dark * 0.68f;
        b *= 1.f - dark * 0.78f;
      }
    }
    if (grain > 0.0001f) {
      const float noise = (hash01(x, y, 1703) * 2.f - 1.f) * grain * 18.f;
      r += noise * 1.05f;
      g += noise;
      b += noise * 0.9f;
    }
    return qRgba(toByte(r), toByte(g), toByte(b), toByte(a));
  });
}

// ---- datamosh_smear -------------------------------------------------------------------------------
EffectResult datamoshSmear(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float smear = floatOf(params, "smear_amount") / 100.f, corruption = floatOf(params, "corruption") / 100.f;
  const int blockSize = intOf(params, "block_size");
  const float drift = floatOf(params, "drift"), channelSplit = floatOf(params, "channel_split") / 100.f * 7.f;
  if (smear <= 0.0001f && corruption <= 0.0001f && std::fabs(drift) <= 0.0001f) return identityCopy(source, context);
  const bool horizontal = choiceOf(params, "direction") == QLatin1String("horizontal");
  const int seed = 9011 ^ seedOf(context);
  const int w = source.width(), h = source.height();
  const float smearDistance = smear * float(blockSize) * 5.5f;
  const Pixels p(source);
  QImage out = makeResult(source);
  if (out.isNull()) return {{}, kFailed};
  auto *dst = reinterpret_cast<QRgb *>(out.bits());
  const int lines = horizontal ? h : w;
  const bool done = runRows(context, lines, 0, 100, [&](int line) {
    float carry = 0.f;
    if (horizontal) {
      const int y = line;
      const float rowNoise = hash01(y / blockSize, 7, seed) * 2.f - 1.f;
      const float baseShift = (std::sin(float(y) * 0.09f + 0.4f) + rowNoise * 1.15f) * drift * 0.34f;
      for (int blockX = 0; blockX < w; blockX += blockSize) {
        const int blockIndex = blockX / blockSize;
        const float trigger = hash01(blockIndex, y / blockSize, seed ^ 313);
        if (trigger > 1.f - corruption * 0.78f) {
          const float sign = hash01(blockIndex, y, seed ^ 743) * 2.f - 1.f;
          carry += sign * smearDistance * (0.35f + trigger);
        } else {
          carry *= 0.82f;
        }
        for (int x = blockX; x < std::min(w, blockX + blockSize); ++x) {
          const float fx = float(x) - carry - baseShift;
          const QRgb sr = bilinear(p, fx + channelSplit, float(y)), sg = bilinear(p, fx, float(y)), sb = bilinear(p, fx - channelSplit, float(y));
          dst[size_t(y) * size_t(w) + size_t(x)] = qRgba(qRed(sr), qGreen(sg), qBlue(sb), qAlpha(sg));
        }
      }
    } else {
      const int x = line;
      const float colNoise = hash01(x / blockSize, 11, seed) * 2.f - 1.f;
      const float baseShift = (std::cos(float(x) * 0.07f + 1.1f) + colNoise * 1.20f) * drift * 0.34f;
      for (int blockY = 0; blockY < h; blockY += blockSize) {
        const int blockIndex = blockY / blockSize;
        const float trigger = hash01(x / blockSize, blockIndex, seed ^ 919);
        if (trigger > 1.f - corruption * 0.78f) {
          const float sign = hash01(x, blockIndex, seed ^ 1597) * 2.f - 1.f;
          carry += sign * smearDistance * (0.35f + trigger);
        } else {
          carry *= 0.82f;
        }
        for (int y = blockY; y < std::min(h, blockY + blockSize); ++y) {
          const float fy = float(y) - carry - baseShift;
          const QRgb sr = bilinear(p, float(x), fy + channelSplit), sg = bilinear(p, float(x), fy), sb = bilinear(p, float(x), fy - channelSplit);
          dst[size_t(y) * size_t(w) + size_t(x)] = qRgba(qRed(sr), qGreen(sg), qBlue(sb), qAlpha(sg));
        }
      }
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {out, {}};
}

// ---- tilt_shift -----------------------------------------------------------------------------------
EffectResult tiltShift(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int w = source.width(), h = source.height();
  const float blur = floatOf(params, "blur_radius");
  const float focusSize = floatOf(params, "focus_size") / 100.f;
  const float focusX = floatOf(params, "focus_position_x") / 100.f, focusY = floatOf(params, "focus_position_y") / 100.f;
  const float falloff = floatOf(params, "falloff") / 100.f, saturation = floatOf(params, "saturation_boost") / 100.f;
  const bool radial = choiceOf(params, "mode") == QLatin1String("radial");
  QImage blurred = source;
  if (blur > 0.01f) {
    FloatImage f = toPremul(source);
    if (!gaussianBlur255(f, blur, false, context)) return {{}, kCancelled};
    blurred = fromPremul(f, source);
  }
  const Pixels bl(blurred);
  const float cx = focusX * float(w - 1), cy = focusY * float(h - 1);
  const float minDim = float(std::min(w, h));
  const float focusHalfLinear = float(h) * focusSize * 0.5f, focusRadiusRadial = minDim * focusSize * 0.5f;
  const float falloffLinear = std::max(1.f, float(h) * falloff), falloffRadial = std::max(1.f, minDim * falloff);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float mask;
    if (radial) {
      const float dx = float(x) - cx, dy = float(y) - cy;
      mask = smoothStep(focusRadiusRadial, focusRadiusRadial + falloffRadial, std::sqrt(dx * dx + dy * dy));
    } else {
      mask = smoothStep(focusHalfLinear, focusHalfLinear + falloffLinear, std::fabs(float(y) - cy));
    }
    const QRgb b = bl.at(x, y);
    float r = lerp(float(qRed(src)), float(qRed(b)), mask), g = lerp(float(qGreen(src)), float(qGreen(b)), mask),
          bb = lerp(float(qBlue(src)), float(qBlue(b)), mask);
    const float amount = saturation * (1.f - mask * 0.7f);
    if (amount > 0.f) {
      const float gray = (r + g + bb) / 3.f, factor = 1.f + amount;
      r = gray + (r - gray) * factor;
      g = gray + (g - gray) * factor;
      bb = gray + (bb - gray) * factor;
    }
    return qRgba(toByte(r), toByte(g), toByte(bb), qAlpha(src));
  });
}

// ---- oil_slick_interference -----------------------------------------------------------------------

EffectResult oilSlickInterference(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float intensity = floatOf(params, "intensity") / 100.f;
  const float scale = 24.f + floatOf(params, "scale") / 100.f * 180.f;
  const float invScale = 1.f / scale;
  const float darkness = floatOf(params, "darkness") / 100.f, gloss = floatOf(params, "gloss") / 100.f;
  const float shift = floatOf(params, "shift") / 360.f;
  if (intensity <= 0.0001f) return identityCopy(source, context);
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float sr = float(qRed(src)) / 255.f, sg = float(qGreen(src)) / 255.f, sb = float(qBlue(src)) / 255.f;
    const float lum = 0.2126f * sr + 0.7152f * sg + 0.0722f * sb;
    const float lumLeft = luminance01(p.at(std::max(0, x - 1), y)), lumRight = luminance01(p.at(std::min(p.w - 1, x + 1), y));
    const float lumTop = luminance01(p.at(x, std::max(0, y - 1))), lumBottom = luminance01(p.at(x, std::min(p.h - 1, y + 1)));
    const float edge = clamp01((std::fabs(lumRight - lumLeft) + std::fabs(lumBottom - lumTop)) * 1.2f);
    const float u = float(x) * invScale, v = float(y) * invScale;
    const float waveA = std::sin(u * 2.7f + v * 1.6f + 0.7f), waveB = std::cos(u * 4.4f - v * 2.8f - 1.1f);
    const float waveC = std::sin((u + v) * 3.6f + 1.9f);
    float bands = waveA * 0.40f + waveB * 0.34f + waveC * 0.26f;
    bands = bands * 0.5f + 0.5f;
    float hue = shift + bands * 0.32f + u * 0.06f + v * 0.03f;
    hue -= std::floor(hue);
    const float slick = clamp01(intensity * ((1.f - lum) * 0.45f + edge * 0.30f + bands * 0.25f));
    float ir, ig, ib;
    hsvToRgb(hue, 0.84f, 0.92f + gloss * 0.08f, ir, ig, ib);
    const float darkR = sr * (1.f - darkness * 0.58f), darkG = sg * (1.f - darkness * 0.60f), darkB = sb * (1.f - darkness * 0.66f);
    const float glossMask = gloss * smoothStep(0.58f, 0.96f, bands);
    auto scr = [](float s, float o) { o = clamp01(o); return 1.f - (1.f - s) * (1.f - o); };
    return qRgba(toByte(scr(darkR, ir * slick + glossMask * 0.24f) * 255.f), toByte(scr(darkG, ig * slick + glossMask * 0.20f) * 255.f),
                 toByte(scr(darkB, ib * slick + glossMask * 0.28f) * 255.f), qAlpha(src));
  });
}

// ---- frosted_glass_ice_edges ----------------------------------------------------------------------
EffectResult frostedGlassIceEdges(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float distortion = floatOf(params, "distortion"), blurMix = floatOf(params, "blur") / 100.f;
  const int edgeThreshold = intOf(params, "edge_threshold");
  const float iceStrength = floatOf(params, "ice_strength") / 100.f, tint = floatOf(params, "tint") / 100.f;
  const int seed = 1337 ^ seedOf(context);
  const int w = source.width(), h = source.height();
  const Pixels p(source);
  std::vector<float> lum(size_t(w) * size_t(h));
  for (size_t i = 0; i < lum.size(); ++i) lum[i] = luminance01(p.data[i]);
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const float coarseX = hash01(x / 9, y / 9, seed) * 2.f - 1.f, coarseY = hash01(x / 9, y / 9, seed ^ 977) * 2.f - 1.f;
    const float fine = hash01(x, y, seed ^ 4099) * 2.f - 1.f;
    const float sx = float(x) + (coarseX * 0.75f + fine * 0.25f) * distortion;
    const float sy = float(y) + (coarseY * 0.75f - fine * 0.25f) * distortion;
    const QRgb sample = bilinear(p, sx, sy);
    float r = float(qRed(sample)), g = float(qGreen(sample)), b = float(qBlue(sample)), a = float(qAlpha(sample));
    if (blurMix > 0.001f) {
      const QRgb s1 = bilinear(p, sx - 1.f, sy), s2 = bilinear(p, sx + 1.f, sy), s3 = bilinear(p, sx, sy - 1.f), s4 = bilinear(p, sx, sy + 1.f);
      r = lerp(r, float(qRed(s1) + qRed(s2) + qRed(s3) + qRed(s4)) * 0.25f, blurMix * 0.85f);
      g = lerp(g, float(qGreen(s1) + qGreen(s2) + qGreen(s3) + qGreen(s4)) * 0.25f, blurMix * 0.85f);
      b = lerp(b, float(qBlue(s1) + qBlue(s2) + qBlue(s3) + qBlue(s4)) * 0.25f, blurMix * 0.85f);
      a = lerp(a, float(qAlpha(s1) + qAlpha(s2) + qAlpha(s3) + qAlpha(s4)) * 0.25f, blurMix * 0.6f);
    }
    const int xl = x > 0 ? x - 1 : 0, xr = x < w - 1 ? x + 1 : w - 1, yu = y > 0 ? y - 1 : 0, yd = y < h - 1 ? y + 1 : h - 1;
    const float gradX = std::fabs(lum[size_t(y) * size_t(w) + size_t(xr)] - lum[size_t(y) * size_t(w) + size_t(xl)]);
    const float gradY = std::fabs(lum[size_t(yd) * size_t(w) + size_t(x)] - lum[size_t(yu) * size_t(w) + size_t(x)]);
    float edge = ((gradX + gradY) * 255.f - float(edgeThreshold)) / std::max(1.f, 255.f - float(edgeThreshold));
    edge = clamp01(edge) * iceStrength;
    const float cool = tint * 0.45f + edge * 0.55f;
    r = lerp(r, r * 0.82f + 16.f, cool);
    g = lerp(g, g * 0.95f + 24.f, cool);
    b = lerp(b, b * 1.08f + 56.f, cool);
    if (edge > 0.001f) {
      const float spec = std::pow(hash01(x, y, seed ^ 12345), 18.f) * edge;
      const float glow = edge * (0.55f + spec * 0.8f);
      r += 120.f * glow;
      g += 142.f * glow;
      b += 175.f * glow;
    }
    return qRgba(toByte(r), toByte(g), toByte(b), toByte(a));
  });
}

// ---- liquid_mercury -------------------------------------------------------------------------------
float mercuryEnvironment(float u, float v) {
  const float a = std::sin(u * 11.5f + v * 2.1f + 0.7f), b = std::cos(u * 18.5f - v * 8.2f - 1.1f);
  const float c = std::sin((u * 0.82f + v * 1.28f) * 12.f + 2.6f);
  return (a * 0.42f + b * 0.36f + c * 0.22f) * 0.5f + 0.5f;
}
EffectResult liquidMercury(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float reflection = floatOf(params, "reflection") / 100.f, ripple = floatOf(params, "ripple") / 100.f;
  const float shine = floatOf(params, "shine") / 100.f, fluidity = floatOf(params, "fluidity") / 100.f, depth = floatOf(params, "depth") / 100.f;
  const Pixels p(source);
  const float invW = 1.f / float(std::max(1, p.w - 1)), invH = 1.f / float(std::max(1, p.h - 1));
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float lum = luminance01(src);
    const float lumLeft = luminance01(p.at(std::max(0, x - 1), y)), lumRight = luminance01(p.at(std::min(p.w - 1, x + 1), y));
    const float lumTop = luminance01(p.at(x, std::max(0, y - 1))), lumBottom = luminance01(p.at(x, std::min(p.h - 1, y + 1)));
    const float gradX = lumRight - lumLeft, gradY = lumBottom - lumTop;
    const float u = float(x) * invW, v = float(y) * invH;
    const float waveA = std::sin(u * 12.f + v * 6.5f + 0.8f), waveB = std::cos(u * 7.5f - v * 14.f - 1.4f);
    const float rippleField = (waveA * 0.6f + waveB * 0.4f) * ripple;
    float nx = -gradX * (1.35f + depth * 3.8f) + rippleField * 0.45f;
    float ny = -gradY * (1.35f + depth * 3.8f) + rippleField * 0.22f;
    float nz = 1.f;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 0.0001f) {
      nx = ny = 0.f;
      nz = 1.f;
    } else {
      nx /= len;
      ny /= len;
      nz /= len;
    }
    const float reflectU = u + nx * (0.55f + fluidity * 0.55f) + waveB * fluidity * 0.05f;
    const float reflectV = v - ny * (0.85f + fluidity * 0.45f) + waveA * fluidity * 0.03f;
    const float env = mercuryEnvironment(reflectU, reflectV);
    const float diffuse = clamp01(nz * 0.65f + 0.15f);
    const float fresnel = std::pow(1.f - clamp01(nz), 2.7f);
    const float specular = std::pow(clamp01(nx * -0.22f + ny * -0.32f + nz * 0.92f), 12.f + shine * 42.f);
    float baseMetal = clamp01(lum * 0.20f + diffuse * 0.26f + env * (0.32f + reflection * 0.42f));
    baseMetal = clamp01((baseMetal - 0.5f) * (1.15f + depth * 1.55f) + 0.5f);
    float silver = lerp(0.06f, 0.94f, baseMetal);
    silver = lerp(silver, 1.f, specular * (0.34f + shine * 0.46f));
    silver = lerp(silver, 1.f, fresnel * (0.20f + reflection * 0.22f));
    const float coolShift = 0.01f + env * 0.03f;
    return qRgba(toByte(clamp01(silver - coolShift) * 255.f), toByte(clamp01(silver) * 255.f), toByte(clamp01(silver + coolShift * 1.25f) * 255.f), qAlpha(src));
  });
}

// ---- marble_border --------------------------------------------------------------------------------
QRgb shadeColor(QRgb c, float amount) {
  return qRgba(toByte((float(qRed(c)) / 255.f + amount) * 255.f), toByte((float(qGreen(c)) / 255.f + amount) * 255.f),
               toByte((float(qBlue(c)) / 255.f + amount) * 255.f), qAlpha(c));
}
EffectResult marbleBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int border = intOf(params, "border_size");
  const float veinStr = floatOf(params, "vein_strength") / 100.f, scale = floatOf(params, "vein_scale") / 1000.f;
  const QColor base = colorOf(params, "base_color"), vein = colorOf(params, "vein_color");
  const bool bevel = boolOf(params, "inner_bevel");
  const int sw = source.width(), sh = source.height();
  return paintBorder(
      source, border, context,
      [&](int x, int y) {
        const float turbulence = fractalNoise(float(x) * scale, float(y) * scale, 6, 2.0f, 0.5f, 42);
        float marble = std::sin(float(x + y) * scale * 2.f + veinStr * turbulence * 20.f);
        marble = (marble + 1.f) * 0.5f;
        marble = std::pow(marble, 1.5f);
        QRgb color = qRgba(toByte(lerp(float(base.red()), float(vein.red()), marble)), toByte(lerp(float(base.green()), float(vein.green()), marble)),
                           toByte(lerp(float(base.blue()), float(vein.blue()), marble)), 255);
        if (bevel) {
          const float zone = float(border) * 0.4f;
          const float innerL = float(border), innerT = float(border), innerR = float(border + sw), innerB = float(border + sh);
          float d = std::numeric_limits<float>::max();
          if (float(x) < innerL) d = std::min(d, innerL - float(x));
          if (float(x) >= innerR) d = std::min(d, float(x) - (innerR - 1.f));
          if (float(y) < innerT) d = std::min(d, innerT - float(y));
          if (float(y) >= innerB) d = std::min(d, float(y) - (innerB - 1.f));
          const float depth = smoothStep(0.f, 1.f, 1.f - std::clamp(d / zone, 0.f, 1.f));
          if (depth > 0.f) color = shadeColor(color, -depth * 0.25f);
        }
        return color;
      },
      [&](QImage &out) {
        // Hairline round the picture: 1.5 px stroke centred 0.75 px outside the interior.
        QImage surface = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&surface);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(strokePen(QColor::fromRgba(shadeColor(vein.rgba(), -0.3f)), 1.5));
        painter.drawRect(QRectF(float(border) - 0.75f, float(border) - 0.75f, float(sw) + 1.5f, float(sh) + 1.5f));
        painter.end();
        out = surface.convertToFormat(QImage::Format_ARGB32);
        out.setColorSpace(source.colorSpace());
      });
}

// ---- pencil_sketch --------------------------------------------------------------------------------
std::vector<float> boxBlurFloat(const std::vector<float> &src, int w, int h, int radius) {
  const int window = radius * 2 + 1;
  std::vector<float> horizontal(src.size()), vertical(src.size());
  for (int y = 0; y < h; ++y) {
    const size_t row = size_t(y) * size_t(w);
    float acc = 0.f;
    for (int k = -radius; k <= radius; ++k) acc += src[row + size_t(clampInt(k, 0, w - 1))];
    for (int x = 0; x < w; ++x) {
      horizontal[row + size_t(x)] = acc / float(window);
      acc += src[row + size_t(clampInt(x + radius + 1, 0, w - 1))] - src[row + size_t(clampInt(x - radius, 0, w - 1))];
    }
  }
  for (int x = 0; x < w; ++x) {
    float acc = 0.f;
    for (int k = -radius; k <= radius; ++k) acc += horizontal[size_t(clampInt(k, 0, h - 1)) * size_t(w) + size_t(x)];
    for (int y = 0; y < h; ++y) {
      vertical[size_t(y) * size_t(w) + size_t(x)] = acc / float(window);
      acc += horizontal[size_t(clampInt(y + radius + 1, 0, h - 1)) * size_t(w) + size_t(x)] -
             horizontal[size_t(clampInt(y - radius, 0, h - 1)) * size_t(w) + size_t(x)];
    }
  }
  return vertical;
}
EffectResult pencilSketch(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int w = source.width(), h = source.height();
  const int radius = intOf(params, "blur_radius");
  const float edgeStrength = floatOf(params, "edge_strength") / 100.f, darkness = floatOf(params, "pencil_darkness") / 100.f;
  const float paper = floatOf(params, "paper_brightness") / 100.f;
  const Pixels p(source);
  std::vector<float> gray(size_t(w) * size_t(h)), inverse(gray.size());
  for (size_t i = 0; i < gray.size(); ++i) {
    gray[i] = luminance01(p.data[i]);
    inverse[i] = 1.f - gray[i];
  }
  if (context.cancelled()) return {{}, kCancelled};
  const std::vector<float> blurredInverse = boxBlurFloat(inverse, w, h, radius);
  const float gamma = 1.f + darkness * 1.75f;
  const float paperMix = std::clamp(paper - 1.f, 0.f, 0.6f);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const int y0 = y > 0 ? y - 1 : 0, y2 = y < h - 1 ? y + 1 : h - 1, x0 = x > 0 ? x - 1 : 0, x2 = x < w - 1 ? x + 1 : w - 1;
    auto g = [&](int xx, int yy) { return gray[size_t(yy) * size_t(w) + size_t(xx)]; };
    const float tl = g(x0, y0), tc = g(x, y0), tr = g(x2, y0), ml = g(x0, y), mr = g(x2, y), bl = g(x0, y2), bc = g(x, y2), br = g(x2, y2);
    const float gx = (-tl + tr) + (-2.f * ml + 2.f * mr) + (-bl + br);
    const float gy = (-tl - 2.f * tc - tr) + (bl + 2.f * bc + br);
    const float edge = std::clamp(std::sqrt(gx * gx + gy * gy) / 4.f, 0.f, 1.f);
    const size_t i = size_t(y) * size_t(w) + size_t(x);
    const float dodge = gray[i] / std::max(1.f - blurredInverse[i], 0.03f);
    float value = std::clamp(std::clamp(dodge, 0.f, 1.f) - edge * edgeStrength * 0.85f, 0.f, 1.f);
    value = std::pow(value, gamma);
    value = lerp(value, 1.f, paperMix);
    const int v = toByte(value * 255.f);
    return qRgba(v, v, v, qAlpha(src));
  });
}

// ---- bloom ----------------------------------------------------------------------------------------
EffectResult bloom(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float threshold = floatOf(params, "threshold") / 100.f, softKnee = floatOf(params, "soft_knee") / 100.f;
  const float radius = floatOf(params, "radius"), intensity = floatOf(params, "intensity") / 100.f;
  if (intensity <= 0.f) return identityCopy(source, context);
  const float halfKnee = softKnee * 0.5f;
  const float low = std::clamp(threshold - halfKnee, 0.f, 1.f), high = std::clamp(threshold + halfKnee, 0.f, 1.f);
  auto bright = mapPixels(source, context, [&](int, int, QRgb c) {
    const float lum = luminance01(c);
    const float mask = softKnee > 0.f ? smoothStep(low, high, lum) : (lum >= threshold ? 1.f : 0.f);
    if (mask <= 0.f) return QRgb(0);
    const float gain = mask * (0.35f + 0.65f * lum);
    return qRgba(toByte(float(qRed(c)) * gain), toByte(float(qGreen(c)) * gain), toByte(float(qBlue(c)) * gain), toByte(float(qAlpha(c)) * gain));
  });
  if (!bright.ok()) return bright;
  QImage blurred;
  if (!blurredClamp(bright.image, radius, context, blurred)) return {{}, kCancelled};
  const Pixels bp(blurred);
  auto channel = [&](int s, int b) {
    const float sc = float(s) / 255.f, bc = float(b) / 255.f * intensity;
    float screened = 1.f - (1.f - sc) * (1.f - std::clamp(bc, 0.f, 1.f));
    if (bc > 1.f) screened += (bc - 1.f) * 0.40f;
    return toByte(screened * 255.f);
  };
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const QRgb b = bp.at(x, y);
    return qRgba(channel(qRed(src), qRed(b)), channel(qGreen(src), qGreen(b)), channel(qBlue(src), qBlue(b)), qAlpha(src));
  });
}

// ---- etched_glass ---------------------------------------------------------------------------------
EffectResult etchedGlass(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float frost = floatOf(params, "frost") / 100.f, engrave = floatOf(params, "engrave") / 100.f;
  const float refraction = floatOf(params, "refraction") / 100.f, highlight = floatOf(params, "highlight") / 100.f;
  const float fade = floatOf(params, "background_fade") / 100.f;
  const float refractionPx = 0.6f + refraction * 5.4f;
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float nX = (hash01(x / 3, y / 3, 2011) * 2.f - 1.f) * refractionPx, nY = (hash01(x / 3, y / 3, 3001) * 2.f - 1.f) * refractionPx;
    const QRgb refracted = bilinear(p, float(x) + nX, float(y) + nY);
    const int left = std::max(0, x - 2), right = std::min(p.w - 1, x + 2), top = std::max(0, y - 2), bottom = std::min(p.h - 1, y + 2);
    const QRgb c[5] = {src, p.at(left, y), p.at(right, y), p.at(x, top), p.at(x, bottom)};
    float br = 0.f, bg = 0.f, bb = 0.f;
    for (const QRgb q : c) {
      br += float(qRed(q));
      bg += float(qGreen(q));
      bb += float(qBlue(q));
    }
    // The reference rounds the blurred sample to bytes before use.
    const float blurR = float(toByte(br / 5.f)) / 255.f, blurG = float(toByte(bg / 5.f)) / 255.f, blurB = float(toByte(bb / 5.f)) / 255.f;
    const float lum = luminance(refracted) / 255.f;
    const float lumLeft = luminance01(p.at(std::max(0, x - 1), y)), lumRight = luminance01(p.at(std::min(p.w - 1, x + 1), y));
    const float lumTop = luminance01(p.at(x, std::max(0, y - 1))), lumBottom = luminance01(p.at(x, std::min(p.h - 1, y + 1)));
    const float edge = clamp01((std::fabs(lumRight - lumLeft) + std::fabs(lumBottom - lumTop)) * 1.25f);
    const float baseR = lerp(float(qRed(src)) / 255.f, blurR, fade * 0.72f), baseG = lerp(float(qGreen(src)) / 255.f, blurG, fade * 0.72f);
    const float baseB = lerp(float(qBlue(src)) / 255.f, blurB, fade * 0.72f);
    const float gray = lum * 0.88f + 0.07f;
    float gr = lerp(baseR, gray + 0.07f, frost * 0.58f), gg = lerp(baseG, gray + 0.08f, frost * 0.60f), gb = lerp(baseB, gray + 0.10f, frost * 0.64f);
    const float engraving = edge * engrave;
    gr *= 1.f - engraving * 0.36f;
    gg *= 1.f - engraving * 0.38f;
    gb *= 1.f - engraving * 0.40f;
    const float spec = edge * highlight;
    gr = lerp(gr, 1.f, spec * 0.16f);
    gg = lerp(gg, 1.f, spec * 0.18f);
    gb = lerp(gb, 1.f, spec * 0.22f);
    return qRgba(toByte(gr * 255.f), toByte(gg * 255.f), toByte(gb * 255.f), qAlpha(src));
  });
}

// ---- halftone -------------------------------------------------------------------------------------
float fract(float v) { return v - std::floor(v); }
float dotCoverage(int x, int y, int cell, float cosA, float sinA, float intensity, float softness01) {
  if (intensity <= 0.0001f) return 0.f;
  const float u = (float(x) * cosA + float(y) * sinA) / float(cell);
  const float v = (float(-x) * sinA + float(y) * cosA) / float(cell);
  const float fu = fract(u) - 0.5f, fv = fract(v) - 0.5f;
  const float distance = std::sqrt(fu * fu + fv * fv);
  const float radius = 0.5f * std::sqrt(std::clamp(intensity, 0.f, 1.f));
  const float softness = 0.01f + softness01 * 0.14f;
  return 1.f - smoothStep(std::max(0.f, radius - softness), radius + softness, distance);
}
EffectResult halftone(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int cell = intOf(params, "cell_size");
  const float softness = floatOf(params, "dot_softness") / 100.f, inkStrength = floatOf(params, "ink_strength") / 100.f;
  const float sourceBlend = floatOf(params, "source_blend") / 100.f;
  const float offset = floatOf(params, "angle_offset") * (kPi / 180.f);
  const float cCos = std::cos(15.f * (kPi / 180.f) + offset), cSin = std::sin(15.f * (kPi / 180.f) + offset);
  const float mCos = std::cos(75.f * (kPi / 180.f) + offset), mSin = std::sin(75.f * (kPi / 180.f) + offset);
  const float yCos = std::cos(offset), ySin = std::sin(offset);
  const float kCos = std::cos(45.f * (kPi / 180.f) + offset), kSin = std::sin(45.f * (kPi / 180.f) + offset);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float r = float(qRed(src)) / 255.f, g = float(qGreen(src)) / 255.f, b = float(qBlue(src)) / 255.f;
    float k = 1.f - std::max(r, std::max(g, b)), c = 0.f, m = 0.f, yv = 0.f;
    const float denom = 1.f - k;
    if (denom > 0.0001f) {
      c = std::clamp((1.f - r - k) / denom, 0.f, 1.f);
      m = std::clamp((1.f - g - k) / denom, 0.f, 1.f);
      yv = std::clamp((1.f - b - k) / denom, 0.f, 1.f);
      k = std::clamp(k, 0.f, 1.f);
    }
    float out[3] = {1.f, 1.f, 1.f};
    auto ink = [&](float ir, float ig, float ib, float alpha) {
      const float t = std::clamp(alpha, 0.f, 1.f);
      out[0] = lerp(out[0], ir, t);
      out[1] = lerp(out[1], ig, t);
      out[2] = lerp(out[2], ib, t);
    };
    ink(0.f, 1.f, 1.f, dotCoverage(x, y, cell, cCos, cSin, c, softness) * inkStrength);
    ink(1.f, 0.f, 1.f, dotCoverage(x, y, cell, mCos, mSin, m, softness) * inkStrength);
    ink(1.f, 1.f, 0.f, dotCoverage(x, y, cell, yCos, ySin, yv, softness) * inkStrength);
    ink(0.f, 0.f, 0.f, dotCoverage(x, y, cell, kCos, kSin, k, softness) * inkStrength * 1.1f);
    return qRgba(toByte(lerp(out[0], r, sourceBlend) * 255.f), toByte(lerp(out[1], g, sourceBlend) * 255.f),
                 toByte(lerp(out[2], b, sourceBlend) * 255.f), qAlpha(src));
  });
}
// ---- chain_link_border ----------------------------------------------------------------------------
QRgb lightenColor(const QColor &c, float amount) {
  return qRgba(toByte(float(c.red()) + float(255 - c.red()) * amount), toByte(float(c.green()) + float(255 - c.green()) * amount),
               toByte(float(c.blue()) + float(255 - c.blue()) * amount), c.alpha());
}
QRgb darkenColor(const QColor &c, float amount) {
  return qRgba(toByte(float(c.red()) * (1.f - amount)), toByte(float(c.green()) * (1.f - amount)), toByte(float(c.blue()) * (1.f - amount)), c.alpha());
}
EffectResult chainLinkBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const float cell = floatOf(params, "cell_size"), wireW = floatOf(params, "wire_width");
  const QColor wire = colorOf(params, "wire_color"), bg = colorOf(params, "bg_color");
  const bool highlight = boolOf(params, "highlight");
  const int sw = source.width(), sh = source.height();
  const int nw = sw + border * 2, nh = sh + border * 2;
  QImage surface(nw, nh, QImage::Format_ARGB32_Premultiplied);
  if (surface.isNull()) return {{}, kFailed};
  surface.fill(bg);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen wirePen(wire, wireW, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin);
  QPen highlightPen(QColor::fromRgba(lightenColor(wire, 0.4f)), wireW * 0.5f, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin);
  wirePen.setMiterLimit(4);
  highlightPen.setMiterLimit(4);
  const float half = cell * 0.5f;
  bool cancelled = false;
  auto band = [&](float bx, float by, float bw, float bh) {
    if (cancelled) return;
    painter.save();
    painter.setClipRect(QRectF(bx, by, bw, bh));
    const float endX = bx + bw + cell * 2.f, endY = by + bh + cell * 2.f;
    for (float y = by - cell * 2.f; y <= endY; y += cell) {
      if (context.cancelled()) {
        cancelled = true;
        break;
      }
      for (float x = bx - cell * 2.f; x <= endX; x += cell) {
        QPainterPath path;
        path.moveTo(x, y - half);
        path.lineTo(x + half, y);
        path.lineTo(x, y + half);
        path.lineTo(x - half, y);
        path.lineTo(x, y - half);
        painter.setPen(wirePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        if (highlight) {
          QPainterPath hl;
          hl.moveTo(x - half, y);
          hl.lineTo(x, y - half);
          hl.lineTo(x + half, y);
          painter.setPen(highlightPen);
          painter.drawPath(hl);
        }
      }
    }
    painter.restore();
  };
  band(0, 0, float(nw), float(border));                         // top
  band(0, float(border + sh), float(nw), float(border));         // bottom
  band(0, float(border), float(border), float(sh));              // left
  band(float(border + sw), float(border), float(border), float(sh));  // right
  if (cancelled || context.cancelled()) return {{}, kCancelled};
  painter.setPen(strokePen(QColor::fromRgba(darkenColor(wire, 0.3f)), wireW * 1.5f));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRectF(border - 1, border - 1, sw + 2, sh + 2));
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.drawImage(border, border, source);
  painter.end();
  return finishSurfaceRows(surface, source, context);
}

// ---- claymation_texture ---------------------------------------------------------------------------
EffectResult claymationTexture(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int chunk = intOf(params, "chunkSize");
  const float smoothness = floatOf(params, "smoothness") / 100.f, relief = floatOf(params, "relief") / 100.f;
  const float saturation = floatOf(params, "saturation") / 100.f, grain = floatOf(params, "textureGrain") / 100.f;
  // The reference seeds from Random.Shared.Next(1, int.MaxValue); here it comes from EffectContext::seed.
  const int seed = SplitMix64(context.seed).range(1, std::numeric_limits<int>::max());
  const Pixels p(source);
  const int right = p.w - 1, bottom = p.h - 1;
  const int blurRadius = std::clamp(chunk / 4, 1, 4);
  const float smoothMix = smoothness * 0.9f, satFactor = 1.f + saturation, cellSize = float(chunk) * 1.85f;
  const int levels = std::clamp(20 - chunk, 5, 18);
  const float quantStep = 255.f / float(levels - 1);
  auto quantize = [&](float v) { return std::nearbyintf(v / quantStep) * quantStep; };
  return mapPixels(source, context, [&](int x, int y, QRgb center) {
    const int yUp = std::max(0, y - blurRadius), yDown = std::min(bottom, y + blurRadius);
    const int xLeft = std::max(0, x - blurRadius), xRight = std::min(right, x + blurRadius);
    const QRgb north = p.at(x, yUp), south = p.at(x, yDown), west = p.at(xLeft, y), east = p.at(xRight, y), nw = p.at(xLeft, yUp), se = p.at(xRight, yDown);
    auto avg = [&](int (*f)(QRgb)) {
      return (float(f(center)) * 3.f + float(f(north)) * 1.5f + float(f(south)) * 1.5f + float(f(west)) * 1.5f + float(f(east)) * 1.5f +
              float(f(nw)) + float(f(se))) / 10.f;
    };
    float r = quantize(lerp(float(qRed(center)), avg(qRed), smoothMix));
    float g = quantize(lerp(float(qGreen(center)), avg(qGreen), smoothMix));
    float b = quantize(lerp(float(qBlue(center)), avg(qBlue), smoothMix));
    const float fx = float(x) / cellSize, fy = float(y) / cellSize;
    const int cx = int(std::floor(fx)), cy = int(std::floor(fy));
    const float tx = fx - float(cx), ty = fy - float(cy);
    const float coarse = lerp(lerp(hash01(cx, cy, seed), hash01(cx + 1, cy, seed), tx), lerp(hash01(cx, cy + 1, seed), hash01(cx + 1, cy + 1, seed), tx), ty);
    const float ridgeA = std::sin(((float(x) + coarse * 13.f) / cellSize) * kPi * 2.f);
    const float ridgeB = std::cos(((float(y) - coarse * 11.f) / cellSize) * kPi * 2.f);
    const float ridge = ridgeA * ridgeB;
    const float fine = (hash01(x, y, seed ^ 919) * 2.f - 1.f) * grain;
    const float reliefOffset = ((coarse - 0.5f) * 2.f + ridge * 0.65f) * relief * 23.f;
    const float offset = reliefOffset + fine * (4.f + relief * 7.f);
    r += offset + relief * 2.f;
    g += offset;
    b += offset - relief * 2.5f;
    const float gray = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    r = gray + (r - gray) * satFactor;
    g = gray + (g - gray) * satFactor;
    b = gray + (b - gray) * satFactor;
    return qRgba(toByte(r), toByte(g), toByte(b), qAlpha(center));
  });
}

// ---- macos_window ---------------------------------------------------------------------------------
void trafficButton(QPainter &painter, float x, float y, const QColor &color) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawEllipse(QPointF(x, y), 6.0, 6.0);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(int(float(color.red()) * 0.8f), int(float(color.green()) * 0.8f), int(float(color.blue()) * 0.8f), 200), 0.5));
  painter.drawEllipse(QPointF(x, y), 6.0, 6.0);
}
EffectResult macosWindow(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  constexpr int kTitleBar = 38, kButtonRadius = 6, kButtonSpacing = 20, kButtonLeft = 14;
  const QString title = choiceOf(params, "title");
  const int cornerRadius = intOf(params, "corner_radius"), shadowSize = intOf(params, "shadow_size"), padding = intOf(params, "padding");
  const bool dark = boolOf(params, "dark_mode");
  const int windowW = source.width(), windowH = kTitleBar + source.height();
  const int totalW = windowW + padding * 2 + shadowSize * 2, totalH = windowH + padding * 2 + shadowSize * 2;
  const float wx = float(padding + shadowSize), wy = float(padding + shadowSize);
  const QRectF windowRect(wx, wy, windowW, windowH);
  auto addWindowPath = [&](QPainterPath &path) {
    if (cornerRadius > 0) path.addRoundedRect(windowRect, cornerRadius, cornerRadius);
    else path.addRect(windowRect);
  };
  QImage result(totalW, totalH, QImage::Format_ARGB32_Premultiplied);
  if (result.isNull()) return {{}, kFailed};
  result.fill(Qt::transparent);
  QPainter out(&result);
  out.setRenderHint(QPainter::Antialiasing, true);
  if (shadowSize > 0) {
    // SKMaskFilter.CreateBlur(Normal, shadowSize / 2): the rounded rect (black, alpha 60) blurred with that sigma.
    QImage shape(totalW, totalH, QImage::Format_ARGB32_Premultiplied);
    shape.fill(Qt::transparent);
    QPainter sp(&shape);
    sp.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    addWindowPath(path);
    sp.fillPath(path, QColor(0, 0, 0, 60));
    sp.end();
    const QImage shapeStraight = shape.convertToFormat(QImage::Format_ARGB32);
    FloatImage f = toPremul(shapeStraight);
    if (!gaussianBlur255(f, float(shadowSize) / 2.f, false, context)) return {{}, kCancelled};
    out.drawImage(0, 0, fromPremul(f, shapeStraight));
  }
  // Window contents are composed on a layer, then masked by the antialiased rounded rect.
  QImage layer(totalW, totalH, QImage::Format_ARGB32_Premultiplied);
  layer.fill(Qt::transparent);
  {
    QPainter lp(&layer);
    lp.setRenderHint(QPainter::Antialiasing, true);
    const QColor titleBarColor = dark ? QColor(56, 55, 57) : QColor(232, 232, 232);
    const QColor titleBorder = dark ? QColor(45, 44, 46) : QColor(208, 208, 208);
    lp.fillRect(QRectF(wx, wy, windowW, kTitleBar), titleBarColor);
    lp.setPen(QPen(titleBorder, 1));
    lp.drawLine(QPointF(wx, wy + kTitleBar), QPointF(wx + windowW, wy + kTitleBar));
    const float by = wy + float(kTitleBar) / 2.f, bx = wx + float(kButtonLeft + kButtonRadius);
    trafficButton(lp, bx, by, QColor(255, 95, 87));
    trafficButton(lp, bx + float(kButtonSpacing), by, QColor(255, 189, 46));
    trafficButton(lp, bx + float(kButtonSpacing * 2), by, QColor(39, 201, 63));
    if (!title.trimmed().isEmpty()) {
      QFont font = QGuiApplication::font();
      font.setPixelSize(13);
      font.setWeight(QFont::Medium);
      lp.setFont(font);
      lp.setPen(dark ? QColor(220, 220, 220) : QColor(74, 74, 74));
      const QFontMetricsF metrics(font);
      lp.drawText(QPointF(wx + float(windowW) / 2.f - metrics.horizontalAdvance(title) / 2.0, wy + float(kTitleBar) / 2.f + 13.f / 3.f), title);
    }
    lp.setRenderHint(QPainter::Antialiasing, false);
    lp.drawImage(int(wx), int(wy) + kTitleBar, source);
    lp.setRenderHint(QPainter::Antialiasing, true);
    lp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    QPainterPath mask;
    addWindowPath(mask);
    lp.fillPath(mask, Qt::black);
  }
  out.drawImage(0, 0, layer);
  out.setPen(strokePen(dark ? QColor(70, 70, 70, 180) : QColor(180, 180, 180, 180), 1));
  out.setBrush(Qt::NoBrush);
  QPainterPath border;
  addWindowPath(border);
  out.drawPath(border);
  out.end();
  return finishSurfaceRows(result, source, context);
}

// ---- torn_edge ------------------------------------------------------------------------------------
EffectResult tornEdge(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int depth = intOf(params, "depth"), range = intOf(params, "range");
  const bool top = boolOf(params, "top"), right = boolOf(params, "right"), bottom = boolOf(params, "bottom"), left = boolOf(params, "left");
  const bool curved = boolOf(params, "curved");
  if (!top && !right && !bottom && !left) return identityCopy(source, context);
  const int w = source.width(), h = source.height();
  const int horizontal = w / range, vertical = h / range;
  if (horizontal < 2 && vertical < 2) return identityCopy(source, context);
  SplitMix64 rand(context.seed);
  auto next = [&](int maxInclusive) { return rand.range(0, maxInclusive + 1); };
  std::vector<QPointF> points;
  if (top && horizontal > 1) {
    const int startX = (left && vertical > 1) ? depth : 0, endX = (right && vertical > 1) ? w - depth : w;
    for (int x = startX; x < endX; x += range) points.emplace_back(x, next(depth));
  } else {
    points.emplace_back(0, 0);
    points.emplace_back(w, 0);
  }
  if (right && vertical > 1) {
    const int startY = (top && horizontal > 1) ? depth : 0, endY = (bottom && horizontal > 1) ? h - depth : h;
    for (int y = startY; y < endY; y += range) points.emplace_back(w - depth + next(depth), y);
  } else {
    points.emplace_back(w, 0);
    points.emplace_back(w, h);
  }
  if (bottom && horizontal > 1) {
    const int startX = (right && vertical > 1) ? w - depth : w, endX = (left && vertical > 1) ? depth : 0;
    for (int x = startX; x >= endX; x -= range) points.emplace_back(x, h - depth + next(depth));
  } else {
    points.emplace_back(w, h);
    points.emplace_back(0, h);
  }
  if (left && vertical > 1) {
    const int startY = (bottom && horizontal > 1) ? h - depth : h, endY = (top && horizontal > 1) ? depth : 0;
    for (int y = startY; y >= endY; y -= range) points.emplace_back(next(depth), y);
  } else {
    points.emplace_back(0, h);
    points.emplace_back(0, 0);
  }
  std::vector<QPointF> pts;
  if (!points.empty()) {
    pts.push_back(points[0]);
    for (size_t i = 1; i < points.size(); ++i)
      if (points[i] != points[i - 1]) pts.push_back(points[i]);
    if (pts.size() > 1 && pts.back() == pts.front()) pts.pop_back();
  }
  QImage surface(w, h, QImage::Format_ARGB32_Premultiplied);
  surface.fill(Qt::transparent);
  if (pts.size() > 2) {
    QPainterPath path;
    if (curved) {
      const QPointF lastPoint = pts.back(), firstPoint = pts.front();
      path.moveTo((lastPoint + firstPoint) / 2.0);
      for (size_t i = 0; i < pts.size(); ++i) {
        const QPointF current = pts[i], nextPoint = pts[(i + 1) % pts.size()];
        path.quadTo(current, (current + nextPoint) / 2.0);
      }
      path.closeSubpath();
    } else {
      path.moveTo(pts[0]);
      for (size_t i = 1; i < pts.size(); ++i) path.lineTo(pts[i]);
      path.closeSubpath();
    }
    QPainter painter(&surface);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(source.convertToFormat(QImage::Format_ARGB32_Premultiplied)));
    painter.drawPath(path);
    painter.end();
  }
  if (context.cancelled()) return {{}, kCancelled};
  return finishSurfaceRows(surface, source, context);
}

// ---- ascii_art ------------------------------------------------------------------------------------
QString normalizeCharacterSet(const QString &input) {
  const QString fallback = QStringLiteral("@%#*+=-:. ");
  if (input.isEmpty()) return fallback;
  QString chars;
  for (const QChar c : input)
    if (!chars.contains(c)) chars.append(c);
  if (chars.isEmpty()) return fallback;
  if (chars.size() == 1) chars.append(QLatin1Char(' '));
  return chars;
}
EffectResult asciiArt(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int cell = intOf(params, "cell_size");
  const float contrast = floatOf(params, "contrast") / 100.f;
  const QString charset = normalizeCharacterSet(choiceOf(params, "character_set"));
  const bool invert = boolOf(params, "invert"), darkBackground = boolOf(params, "dark_background"), sourceColor = boolOf(params, "use_source_color");
  const int w = source.width(), h = source.height();
  QImage surface(w, h, QImage::Format_ARGB32_Premultiplied);
  if (surface.isNull()) return {{}, kFailed};
  surface.fill(darkBackground ? QColor(12, 12, 12) : QColor(255, 255, 255));
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPixelSize(std::max(1, int(std::lround(float(cell) * 1.02f))));
  painter.setFont(font);
  const Pixels p(source);
  const int columns = int(std::ceil(float(w) / float(cell))), rows = int(std::ceil(float(h) / float(cell)));
  const float baseline = float(cell) * 0.90f;
  for (int row = 0; row < rows; ++row) {
    if (context.cancelled()) return {{}, kCancelled};
    const int y0 = row * cell, y1 = std::min(h, y0 + cell);
    for (int col = 0; col < columns; ++col) {
      const int x0 = col * cell, x1 = std::min(w, x0 + cell);
      if (x0 >= x1 || y0 >= y1) continue;
      float sumR = 0, sumG = 0, sumB = 0, sumA = 0;
      int count = 0;
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          const QRgb c = p.at(x, y);
          sumR += float(qRed(c));
          sumG += float(qGreen(c));
          sumB += float(qBlue(c));
          sumA += float(qAlpha(c));
          ++count;
        }
      const float inv = 1.f / float(count);
      const float avgR = sumR * inv, avgG = sumG * inv, avgB = sumB * inv;
      const int avgA = toByte(sumA * inv);
      float lum = (0.2126f * avgR + 0.7152f * avgG + 0.0722f * avgB) / 255.f;
      lum = std::clamp((lum - 0.5f) * contrast + 0.5f, 0.f, 1.f);
      if (invert) lum = 1.f - lum;
      const int index = std::clamp(int(std::nearbyintf(lum * float(charset.size() - 1))), 0, int(charset.size()) - 1);
      const QChar ch = charset[index];
      if (ch.isSpace()) continue;
      QColor color;
      if (sourceColor) {
        color = QColor(toByte(avgR), toByte(avgG), toByte(avgB), avgA);
      } else {
        const int v = darkBackground ? toByte((0.80f + 0.20f * lum) * 255.f) : toByte((0.10f + 0.55f * (1.f - lum)) * 255.f);
        color = QColor(v, v, v, avgA);
      }
      painter.setPen(color);
      painter.drawText(QPointF(x0, float(y0) + baseline), QString(ch));
    }
  }
  painter.end();
  return finishSurfaceRows(surface, source, context);
}

// ---- watercolor_border ----------------------------------------------------------------------------
EffectResult watercolorBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = intOf(params, "border_size");
  const float roughness = floatOf(params, "edge_roughness") / 100.f, bleed = floatOf(params, "bleed_amount") / 100.f;
  const float opacity = floatOf(params, "opacity") / 100.f;
  const int bleedPx = int(float(border) * bleed * 0.5f);
  const int sw = source.width(), sh = source.height();
  const int nw = sw + border * 2, nh = sh + border * 2;
  const QColor wash = colorOf(params, "wash_color");
  const float wr = float(wash.red()) / 255.f, wg = float(wash.green()) / 255.f, wb = float(wash.blue()) / 255.f;
  QImage out(nw, nh, QImage::Format_ARGB32);
  out.setColorSpace(source.colorSpace());
  if (out.isNull()) return {{}, kFailed};
  const Pixels p(source);
  const bool done = runRows(context, nh, 0, 100, [&](int y) {
    auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < nw; ++x) {
      const int srcX = x - border, srcY = y - border;
      const float distL = float(border - x + bleedPx), distT = float(border - y + bleedPx);
      const float distR = float(x - (border + sw - 1 - bleedPx)), distB = float(y - (border + sh - 1 - bleedPx));
      const float maxDist = std::max(std::max(distL, distT), std::max(distR, distB));
      if (maxDist <= 0.f) {
        dst[x] = p.at(srcX, srcY);
        continue;
      }
      const float crossRatio = std::clamp(maxDist / float(border + bleedPx), 0.f, 1.f);
      const float noise = fractalNoise(float(x) * 0.015f, float(y) * 0.015f, 4, 2.0f, 0.55f, 37);
      const float roughOffset = roughness * (noise * 2.f - 1.f) * 0.35f;
      const float t = std::clamp(crossRatio + roughOffset, 0.f, 1.f);
      float washAlpha = smoothStep(0.f, 0.7f, t) * opacity;
      const float paperNoise = fractalNoise(float(x) * 0.04f, float(y) * 0.04f, 3, 2.0f, 0.4f, 99);
      washAlpha *= 0.85f + paperNoise * 0.3f;
      washAlpha = std::clamp(washAlpha, 0.f, 1.f);
      if (washAlpha >= 0.999f) {
        const float shade = paperNoise * 0.12f - 0.06f;
        dst[x] = qRgba(toByte((wr + shade) * 255.f), toByte((wg + shade) * 255.f), toByte((wb + shade) * 255.f), 255);
      } else if (srcX >= 0 && srcX < sw && srcY >= 0 && srcY < sh) {
        const QRgb c = p.at(srcX, srcY);
        const float srcAlpha = float(qAlpha(c)) / 255.f * (1.f - washAlpha);
        const float shade = paperNoise * 0.10f - 0.05f;
        dst[x] = qRgba(toByte(lerp(float(qRed(c)) / 255.f, wr + shade, washAlpha) * 255.f), toByte(lerp(float(qGreen(c)) / 255.f, wg + shade, washAlpha) * 255.f),
                       toByte(lerp(float(qBlue(c)) / 255.f, wb + shade, washAlpha) * 255.f), toByte((srcAlpha + washAlpha) * 255.f));
      } else {
        const float shade = paperNoise * 0.12f - 0.06f;
        dst[x] = qRgba(toByte((wr + shade) * 255.f), toByte((wg + shade) * 255.f), toByte((wb + shade) * 255.f), toByte(washAlpha * 255.f));
      }
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {out, {}};
}

// ---- halation -------------------------------------------------------------------------------------
EffectResult halation(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float threshold = floatOf(params, "threshold") / 100.f, radius = floatOf(params, "radius");
  const float strength = floatOf(params, "strength") / 100.f, warmth = floatOf(params, "warmth") / 100.f;
  if (strength <= 0.f) return identityCopy(source, context);
  const float low = std::clamp(threshold - 0.18f, 0.f, 1.f), high = std::clamp(threshold + 0.12f, 0.f, 1.f);
  const float redBoost = lerp(1.20f, 1.85f, warmth), greenBoost = lerp(0.55f, 0.85f, warmth), blueBoost = lerp(0.35f, 0.15f, warmth);
  auto halo = mapPixels(source, context, [&](int, int, QRgb c) {
    const float lum = luminance01(c);
    const float mask = smoothStep(low, high, lum);
    if (mask <= 0.f) return QRgb(0);
    const float gain = mask * (0.40f + 0.60f * std::pow(lum, 1.4f));
    return qRgba(toByte(float(qRed(c)) * gain * redBoost), toByte(float(qGreen(c)) * gain * greenBoost), toByte(float(qBlue(c)) * gain * blueBoost),
                 toByte(float(qAlpha(c)) * mask));
  });
  if (!halo.ok()) return halo;
  QImage blurred;
  if (!blurredClamp(halo.image, radius, context, blurred)) return {{}, kCancelled};
  const Pixels bp(blurred);
  auto blend = [](int s, float h) {
    const float sc = float(s) / 255.f;
    float screen = 1.f - (1.f - sc) * (1.f - std::clamp(h, 0.f, 1.f));
    if (h > 1.f) screen += (h - 1.f) * 0.35f;
    return toByte(screen * 255.f);
  };
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const QRgb h = bp.at(x, y);
    return qRgba(blend(qRed(src), float(qRed(h)) / 255.f * strength * (1.f + warmth * 0.5f)),
                 blend(qGreen(src), float(qGreen(h)) / 255.f * strength * (0.85f + warmth * 0.15f)),
                 blend(qBlue(src), float(qBlue(h)) / 255.f * strength * (0.65f - warmth * 0.25f)), qAlpha(src));
  });
}

// ---- brick_wall_border ----------------------------------------------------------------------------
EffectResult brickWallBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int border = intOf(params, "border_size");
  const float mortarT = floatOf(params, "mortar_thickness"), colorVar = floatOf(params, "color_variation") / 100.f;
  const float bevel = floatOf(params, "bevel_strength") / 100.f;
  const QColor brick = colorOf(params, "brick_color"), mortar = colorOf(params, "mortar_color");
  const int sw = source.width(), sh = source.height();
  const int nw = sw + border * 2, nh = sh + border * 2;
  const int seed = 3271;
  float brickH = float(border) * 0.45f;
  brickH = std::max(brickH, mortarT * 2.f + 4.f);
  const float brickW = brickH * 2.4f;
  return paintBorder(
      source, border, context,
      [&](int px, int py) {
        const bool topBand = py < border, bottomBand = py >= border + sh, leftBand = px < border;
        float localX, localY;
        if (topBand) {
          localX = float(px);
          localY = float(py);
        } else if (bottomBand) {
          localX = float(nw - 1 - px);
          localY = float(nh - 1 - py);
        } else if (leftBand) {
          localX = float(py);
          localY = float(px);
        } else {
          localX = float(nh - 1 - py);
          localY = float(nw - 1 - px);
        }
        const int row = int(localY / brickH);
        const float rowFrac = std::fmod(localY, brickH) / brickH;
        const float offset = (row % 2 == 0) ? 0.f : brickW * 0.5f;
        const int col = int((localX + offset) / brickW);
        const float colFrac = std::fmod(localX + offset, brickW) / brickW;
        const float mortarFracH = mortarT / brickH, mortarFracW = mortarT / brickW;
        if (rowFrac < mortarFracH || colFrac < mortarFracW) return mortar.rgba();
        const float varHash = hash01(col, row ^ (row * 31 + seed), seed);
        const float varHash2 = hash01(col ^ (col * 13), row, seed ^ 0x5A);
        const float redVar = (varHash - 0.5f) * colorVar * 0.5f, greenVar = (varHash2 - 0.5f) * colorVar * 0.25f;
        const float bevelFrac = std::min(std::min(rowFrac - mortarFracH, 1.f - rowFrac), std::min(colFrac - mortarFracW, 1.f - colFrac));
        const float bevelShade = bevel > 0.f ? (1.f - smoothStep(0.f, 0.18f, bevelFrac)) * bevel * 0.35f : 0.f;
        const float surfaceNoise = hash01(int(localX * 2.f), int(localY * 2.f), seed ^ 0x99) * 0.08f - 0.04f;
        const float r = float(brick.red()) / 255.f + redVar + surfaceNoise - bevelShade;
        const float g = float(brick.green()) / 255.f + greenVar * 0.5f + surfaceNoise * 0.4f - bevelShade;
        const float b = float(brick.blue()) / 255.f - redVar * 0.2f - bevelShade;
        return qRgba(toByte(r * 255.f), toByte(g * 255.f), toByte(b * 255.f), 255);
      },
      [&](QImage &out) {
        // Non-antialiased mortar frame: a stroke of width mortarT centred mortarT/2 outside the picture, so it fills
        // every pixel whose centre lies in [border - mortarT, border + size + mortarT] but not inside the picture.
        const float x0 = float(border) - mortarT, y0 = x0, x1 = float(border + sw) + mortarT, y1 = float(border + sh) + mortarT;
        for (int y = 0; y < nh; ++y) {
          const float cy = float(y) + 0.5f;
          if (cy < y0 || cy > y1) continue;
          auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
          for (int x = 0; x < nw; ++x) {
            const float cx = float(x) + 0.5f;
            if (cx < x0 || cx > x1) continue;
            if (x >= border && x < border + sw && y >= border && y < border + sh) continue;
            dst[x] = sourceOverBorder(dst[x], mortar.rgba());
          }
        }
      });
}

// ---- voronoi_mosaic -------------------------------------------------------------------------------

EffectResult voronoiMosaic(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int w = source.width(), h = source.height();
  const int cellCount = intOf(params, "cell_count");
  const bool drawEdges = boolOf(params, "draw_edges");
  const float edgeThickness = floatOf(params, "edge_thickness");
  const QColor edgeColor = colorOf(params, "edge_color");
  struct Seed2 { float x, y; };
  std::vector<Seed2> seeds(static_cast<size_t>(cellCount));
  int state = 42;
  for (auto &sd : seeds) {
    state = xorshift(state);
    sd.x = float(state & 0x7FFFFFFF) / float(std::numeric_limits<int>::max()) * float(w);
    state = xorshift(state);
    sd.y = float(state & 0x7FFFFFFF) / float(std::numeric_limits<int>::max()) * float(h);
  }
  const Pixels p(source);
  std::vector<int> assignment(size_t(w) * size_t(h));
  const size_t cells = static_cast<size_t>(cellCount);
  std::vector<qint64> sumR(cells), sumG(cells), sumB(cells);
  std::vector<int> counts(cells);
  const float cellSize = std::sqrt(float(w * h) / float(cellCount)) * 2.f;
  const int gridCols = std::max(1, int(std::ceil(float(w) / cellSize))), gridRows = std::max(1, int(std::ceil(float(h) / cellSize)));
  std::vector<std::vector<int>> grid(size_t(gridCols) * size_t(gridRows));
  for (int i = 0; i < cellCount; ++i) {
    const int gc = std::clamp(int(seeds[size_t(i)].x / cellSize), 0, gridCols - 1), gr = std::clamp(int(seeds[size_t(i)].y / cellSize), 0, gridRows - 1);
    grid[size_t(gr) * size_t(gridCols) + size_t(gc)].push_back(i);
  }
  bool done = runRows(context, h, 0, 70, [&](int y) {
    const int gr = std::clamp(int(float(y) / cellSize), 0, gridRows - 1);
    for (int x = 0; x < w; ++x) {
      const int gc = std::clamp(int(float(x) / cellSize), 0, gridCols - 1);
      float bestDist = std::numeric_limits<float>::max();
      int bestSeed = 0;
      for (int r = std::max(0, gr - 2); r <= std::min(gridRows - 1, gr + 2); ++r)
        for (int c = std::max(0, gc - 2); c <= std::min(gridCols - 1, gc + 2); ++c)
          for (const int si : grid[size_t(r) * size_t(gridCols) + size_t(c)]) {
            const float dx = float(x) - seeds[size_t(si)].x, dy = float(y) - seeds[size_t(si)].y;
            const float dist = dx * dx + dy * dy;
            if (dist < bestDist) {
              bestDist = dist;
              bestSeed = si;
            }
          }
      const size_t idx = size_t(y) * size_t(w) + size_t(x);
      assignment[idx] = bestSeed;
      const QRgb px = p.data[idx];
      sumR[size_t(bestSeed)] += qRed(px);
      sumG[size_t(bestSeed)] += qGreen(px);
      sumB[size_t(bestSeed)] += qBlue(px);
      ++counts[size_t(bestSeed)];
    }
  });
  if (!done) return {{}, kCancelled};
  std::vector<QRgb> cellColors(cells, QRgb(0));
  for (size_t i = 0; i < size_t(cellCount); ++i)
    if (counts[i] > 0) cellColors[i] = qRgba(int(sumR[i] / counts[i]), int(sumG[i] / counts[i]), int(sumB[i] / counts[i]), 255);
  QImage out = makeResult(source);
  if (out.isNull()) return {{}, kFailed};
  const int edgeDist = std::max(1, int(std::ceil(edgeThickness)));
  done = runRows(context, h, 70, 100, [&](int y) {
    auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const size_t idx = size_t(y) * size_t(w) + size_t(x);
      const QRgb c = cellColors[size_t(assignment[idx])];
      dst[x] = qRgba(qRed(c), qGreen(c), qBlue(c), qAlpha(p.data[idx]));
    }
  });
  if (!done) return {{}, kCancelled};
  if (drawEdges) {
    // Edge detection reads the untouched assignment map, so it can run in place on the result.
    for (int y = 0; y < h; ++y) {
      if (context.cancelled()) return {{}, kCancelled};
      auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
      for (int x = 0; x < w; ++x) {
        const size_t idx = size_t(y) * size_t(w) + size_t(x);
        const int mine = assignment[idx];
        bool isEdge = false;
        for (int d = 1; d <= edgeDist && !isEdge; ++d) {
          if (x + d < w && assignment[idx + size_t(d)] != mine) isEdge = true;
          if (y + d < h && assignment[idx + size_t(d) * size_t(w)] != mine) isEdge = true;
        }
        if (isEdge) dst[x] = edgeColor.rgba();
      }
    }
  }
  return {out, {}};
}

// ---- riso_print -----------------------------------------------------------------------------------
float risoDotMask(int x, int y, int cell, float coverage, float inkNoise01, int seed, int inkNoiseSeed) {
  const int cx = (x / cell) * cell + cell / 2, cy = (y / cell) * cell + cell / 2;
  const float dx = float(x - cx), dy = float(y - cy);
  const float dist = std::sqrt(dx * dx + dy * dy);
  const float maxR = float(cell) * 0.45f;
  float r = maxR * std::sqrt(std::max(0.f, coverage));
  const float h = hash01(x, y, seed ^ inkNoiseSeed);
  r *= 1.f + 0.25f * ((h * 2.f - 1.f) * inkNoise01);
  const float edge = 1.0f + 0.8f * inkNoise01;
  return 1.f - smoothStep(r, r + edge, dist);
}
EffectResult risoPrint(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float inkStrength = floatOf(params, "ink_strength") / 100.f;
  if (inkStrength <= 0.f) return identityCopy(source, context);
  const float paperFade = floatOf(params, "paper_fade") / 100.f, dotScale = floatOf(params, "dot_scale") / 100.f;
  const float inkNoise = floatOf(params, "ink_noise") / 100.f, misreg = floatOf(params, "offset") * 0.30f;
  const int seed = 2026 ^ seedOf(context);
  const QColor inkA = colorOf(params, "ink_color_a"), inkB = colorOf(params, "ink_color_b");
  const int cell = std::clamp(int(std::nearbyintf(4.f + dotScale * 20.f)), 3, 28);
  const float aWeight = float(inkA.alpha()) / 255.f * inkStrength, bWeight = float(inkB.alpha()) / 255.f * inkStrength;
  const float paperKeep = clamp01(1.f - paperFade * 0.90f);
  const float aR = float(inkA.red()) / 255.f, aG = float(inkA.green()) / 255.f, aB = float(inkA.blue()) / 255.f;
  const float bR = float(inkB.red()) / 255.f, bG = float(inkB.green()) / 255.f, bB = float(inkB.blue()) / 255.f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float coverage = clamp01(1.f - luminance01(src));
    const float dotA = risoDotMask(x, y, cell, coverage, inkNoise, seed, 0xA11);
    const int xB = int(std::nearbyintf(float(x) + misreg));
    const float dotB = risoDotMask(xB, y, cell, coverage, inkNoise, seed, 0xB22);
    const float a = clamp01(dotA * aWeight * paperKeep), b = clamp01(dotB * bWeight * paperKeep);
    float r = 1.f, g = 1.f, bl = 1.f;
    r = r * (1.f - a) + aR * a;
    g = g * (1.f - a) + aG * a;
    bl = bl * (1.f - a) + aB * a;
    r = r * (1.f - b) + bR * b;
    g = g * (1.f - b) + bG * b;
    bl = bl * (1.f - b) + bB * b;
    if (inkNoise > 0.001f) {
      const float grain = (hash01(x, y, seed ^ 0x99) * 2.f - 1.f) * inkNoise * 0.06f;
      r = std::max(0.f, r + grain);
      g = std::max(0.f, g + grain * 0.92f);
      bl = std::max(0.f, bl + grain * 1.06f);
    }
    return qRgba(toByte(r * 255.f), toByte(g * 255.f), toByte(bl * 255.f), qAlpha(src));
  });
}

// ---- starfield_border -----------------------------------------------------------------------------
EffectResult starfieldBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int border = intOf(params, "border_size");
  const float starDens = floatOf(params, "star_density") / 100.f, nebulaStr = floatOf(params, "nebula_intensity") / 100.f;
  const QColor bg = colorOf(params, "bg_color"), nebula = colorOf(params, "nebula_color");
  const bool brightStars = boolOf(params, "bright_stars");
  const int sw = source.width(), sh = source.height();
  const int nw = sw + border * 2, nh = sh + border * 2;
  const int seed = 4207;
  const float nebulaScale = 0.012f;
  return paintBorder(
      source, border, context,
      [&](int x, int y) {
        float r = float(bg.red()) / 255.f, g = float(bg.green()) / 255.f, b = float(bg.blue()) / 255.f;
        const float neb1 = fractalNoise(float(x) * nebulaScale, float(y) * nebulaScale, 4, 2.f, 0.5f, seed);
        const float neb2 = fractalNoise(float(x) * nebulaScale * 0.7f + 300.f, float(y) * nebulaScale * 0.7f + 500.f, 3, 2.f, 0.5f, seed ^ 0x33);
        const float nebMask = clamp01((neb1 + 0.3f) * 0.7f) * nebulaStr, nebMask2 = clamp01((neb2 + 0.2f) * 0.5f) * nebulaStr * 0.5f;
        r += float(nebula.red()) / 255.f * nebMask + 0.1f * nebMask2;
        g += float(nebula.green()) / 255.f * nebMask + 0.15f * nebMask2;
        b += float(nebula.blue()) / 255.f * nebMask + 0.25f * nebMask2;
        const float starHash = hash01(x, y, seed ^ 0xAA);
        const float starThreshold = 1.f - starDens * 0.015f;
        if (starHash > starThreshold) {
          float starBright = (starHash - starThreshold) / (1.f - starThreshold);
          starBright *= starBright;
          const float hueShift = hash01(x ^ 0x55, y ^ 0x33, seed ^ 0xBB);
          r += starBright * (0.7f + hueShift * 0.3f);
          g += starBright * (0.7f + (1.f - hueShift) * 0.3f);
          b += starBright * 0.9f;
        }
        return qRgba(toByte(r * 255.f), toByte(g * 255.f), toByte(b * 255.f), 255);
      },
      [&](QImage &out) {
        if (!brightStars) return;
        QImage surface = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&surface);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int bigStarCount = int(float(nw + nh) * 0.15f * starDens);
        for (int i = 0; i < bigStarCount; ++i) {
          if (context.cancelled()) return;
          const float sx = hash01(i, 0, seed ^ 0xDE) * float(nw), sy = hash01(0, i, seed ^ 0xEF) * float(nh);
          const bool inBorder = sx < float(border) || sx >= float(border + sw) || sy < float(border) || sy >= float(border + sh);
          if (!inBorder) continue;
          const float brightness = hash01(i, i, seed ^ 0xF1), size = 0.8f + brightness * 2.5f;
          const float temp = hash01(i ^ 0x77, i ^ 0x99, seed ^ 0xF2);
          const int sr = toByte(200.f + 55.f * brightness), sg = toByte(190.f + 55.f * brightness * (0.9f + temp * 0.1f));
          const int sb = toByte(180.f + 75.f * brightness * (0.5f + temp * 0.5f)), alpha = toByte(160.f + 95.f * brightness);
          if (!drawBlurredCircle(painter, QPointF(sx, sy), double(size * 2.5f), QColor(sr, sg, sb, alpha / 2), 3.f, context)) return;
          painter.setPen(Qt::NoPen);
          painter.setBrush(QColor(sr, sg, sb, alpha));
          painter.drawEllipse(QPointF(sx, sy), double(size), double(size));
        }
        painter.end();
        const QColorSpace space = out.colorSpace();
        out = surface.convertToFormat(QImage::Format_ARGB32);
        out.setColorSpace(space);
      });
}

// ---- crimson_border -------------------------------------------------------------------------------
EffectResult crimsonBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int size = intOf(params, "size");
  const float glow = floatOf(params, "glow_strength") / 100.f;
  const QColor base = colorOf(params, "color");
  const int sw = source.width(), sh = source.height();
  const int nw = sw + size * 2, nh = sh + size * 2;
  auto darken = [](const QColor &c, float f) { return QColor(int(float(c.red()) * (1.f - f)), int(float(c.green()) * (1.f - f)), int(float(c.blue()) * (1.f - f)), c.alpha()); };
  auto lighten = [](const QColor &c, float f) {
    return QColor(int(std::min(255.f, float(c.red()) + float(255 - c.red()) * f)), int(std::min(255.f, float(c.green()) + float(255 - c.green()) * f)),
                  int(std::min(255.f, float(c.blue()) + float(255 - c.blue()) * f)), c.alpha());
  };
  const QColor dark = darken(base, 0.5f), light = lighten(base, 0.3f);
  QImage surface(nw, nh, QImage::Format_ARGB32_Premultiplied);
  if (surface.isNull()) return {{}, kFailed};
  surface.fill(Qt::transparent);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(QRectF(0, 0, nw, nh), base);
  if (glow > 0.f) {
    const double glowWidth = double(size) * 0.7;
    QColor clear = base;
    clear.setAlpha(0);
    auto ramp = [&](QPointF from, QPointF to, const QColor &c, int alpha, QRectF rect) {
      QColor start = c;
      start.setAlpha(alpha);
      QLinearGradient gradient(from, to);
      gradient.setColorAt(0, start);
      gradient.setColorAt(1, clear);
      painter.fillRect(rect, QBrush(gradient));
    };
    ramp(QPointF(0, 0), QPointF(0, glowWidth), dark, int(200.f * glow), QRectF(0, 0, nw, glowWidth));
    ramp(QPointF(0, nh), QPointF(0, nh - glowWidth), dark, int(200.f * glow), QRectF(0, nh - glowWidth, nw, glowWidth));
    ramp(QPointF(0, 0), QPointF(glowWidth, 0), dark, int(180.f * glow), QRectF(0, 0, glowWidth, nh));
    ramp(QPointF(nw, 0), QPointF(nw - glowWidth, 0), dark, int(180.f * glow), QRectF(nw - glowWidth, 0, glowWidth, nh));
    const double innerGlow = double(size) * 0.4;
    ramp(QPointF(0, size), QPointF(0, size - innerGlow), light, int(120.f * glow), QRectF(size, size - innerGlow, sw, innerGlow));
    ramp(QPointF(0, nh - size), QPointF(0, nh - size + innerGlow), light, int(120.f * glow), QRectF(size, nh - size, sw, innerGlow));
  }
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.drawImage(size, size, source);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);
  if (boolOf(params, "inner_line")) {
    QColor line = light;
    line.setAlpha(180);
    painter.setPen(strokePen(line, 1));
    painter.drawRect(QRectF(double(size) - 0.5, double(size) - 0.5, sw + 1, sh + 1));
  }
  if (boolOf(params, "outer_line")) {
    painter.setPen(strokePen(dark, 1.5));
    painter.drawRect(QRectF(0.5, 0.5, nw - 1, nh - 1));
  }
  painter.end();
  return finishSurfaceRows(surface, source, context);
}

// ---- rust_corrosion -------------------------------------------------------------------------------
EffectResult rustCorrosion(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float corrosion = floatOf(params, "corrosion") / 100.f, pitting = floatOf(params, "pitting") / 100.f;
  const float streaks = floatOf(params, "streaks") / 100.f, dirt = floatOf(params, "dirt") / 100.f, edgeWear = floatOf(params, "edge_wear") / 100.f;
  if (corrosion <= 0.0001f) return identityCopy(source, context);
  const Pixels p(source);
  const float vScale = 1.f / float(std::max(1, p.h - 1));
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)), g = float(qGreen(src)), b = float(qBlue(src));
    const float v = float(y) * vScale;
    const float lumLeft = luminance01(p.at(std::max(0, x - 1), y)), lumRight = luminance01(p.at(std::min(p.w - 1, x + 1), y));
    const float lumTop = luminance01(p.at(x, std::max(0, y - 1))), lumBottom = luminance01(p.at(x, std::min(p.h - 1, y + 1)));
    const float edge = clamp01((std::fabs(lumRight - lumLeft) + std::fabs(lumBottom - lumTop)) * 1.1f);
    const float fx = float(x), fy = float(y);
    const float warpScale = 3.5f + corrosion * 12.f;
    const float warpX = (fractalNoise(fx * 0.010f, fy * 0.010f, 3, 2.05f, 0.55f, 541) * 2.f - 1.f) * warpScale;
    const float warpY = (fractalNoise((fx + 91.7f) * 0.010f, (fy - 63.4f) * 0.010f, 3, 2.00f, 0.55f, 887) * 2.f - 1.f) * warpScale * 0.75f;
    const float sampleX = fx + warpX, sampleY = fy + warpY;
    const float coarse = fractalNoise(sampleX * 0.016f, sampleY * 0.016f, 3, 2.00f, 0.56f, 701);
    const float medium = fractalNoise(sampleX * 0.040f, sampleY * 0.040f, 3, 2.15f, 0.52f, 1301);
    const float fine = fractalNoise(sampleX * 0.105f, sampleY * 0.105f, 2, 2.30f, 0.60f, 1907);
    const float streakSource = fractalNoise(fx * 0.012f + 17.2f, fy * 0.0035f + 4.1f, 3, 2.00f, 0.58f, 2203);
    float streak = smoothStep(0.70f - streaks * 0.08f, 0.98f, streakSource) * streaks * v;
    streak *= 0.42f + 0.58f * fractalNoise((fx - 41.3f) * 0.022f, (fy + 18.7f) * 0.060f, 2, 2.20f, 0.62f, 3251);
    float mask = coarse * 0.52f + medium * 0.32f + fine * 0.16f;
    mask = smoothStep(0.48f - corrosion * 0.18f, 0.94f, mask);
    mask = std::max(mask, edge * edgeWear * 0.72f);
    mask = clamp01(mask * corrosion + streak);
    if (mask > 0.0001f) {
      const float tone = coarse * 0.56f + medium * 0.32f + fine * 0.12f;
      const float warmth = fractalNoise((sampleX + 37.1f) * 0.030f, (sampleY - 22.4f) * 0.030f, 2, 2.10f, 0.60f, 4783);
      const float rustR = lerp(126.f, 214.f, tone * 0.72f + warmth * 0.28f), rustG = lerp(44.f, 108.f, tone), rustB = lerp(12.f, 40.f, tone * 0.88f);
      r = lerp(r, rustR, mask * 0.86f);
      g = lerp(g, rustG, mask * 0.82f);
      b = lerp(b, rustB, mask * 0.78f);
      const float pitNoise = fractalNoise(sampleX * 0.150f, sampleY * 0.150f, 2, 2.35f, 0.58f, 6121);
      const float pit = smoothStep(0.72f, 0.96f, pitNoise) * pitting * mask;
      r *= 1.f - pit * 0.26f;
      g *= 1.f - pit * 0.34f;
      b *= 1.f - pit * 0.38f;
    }
    if (dirt > 0.0001f) {
      const float grime = (fractalNoise(fx * 0.028f, fy * 0.028f, 3, 2.20f, 0.55f, 4021) * 2.f - 1.f) * dirt * 14.f;
      r += grime * 0.80f;
      g += grime * 0.70f;
      b += grime * 0.55f;
    }
    return qRgba(toByte(r), toByte(g), toByte(b), qAlpha(src));
  });
}

// ---- lava_molten_border ---------------------------------------------------------------------------
EffectResult lavaMoltenBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int border = intOf(params, "border_size");
  const float density = floatOf(params, "crack_density") / 100.f, glowStr = floatOf(params, "glow_intensity") / 100.f;
  const QColor rock = colorOf(params, "rock_color"), lava = colorOf(params, "lava_color");
  const bool sparks = boolOf(params, "ember_sparks");
  const int sw = source.width(), sh = source.height();
  const int nw = sw + border * 2, nh = sh + border * 2;
  const int seed = 8371;
  const float noiseScale = 0.04f + density * 0.06f;
  return paintBorder(
      source, border, context,
      [&](int x, int y) {
        const float n1 = fractalNoise(float(x) * noiseScale, float(y) * noiseScale, 4, 2.f, 0.5f, seed);
        const float n2 = fractalNoise(float(x) * noiseScale * 1.7f + 100.f, float(y) * noiseScale * 1.7f + 200.f, 3, 2.f, 0.5f, seed ^ 0x55);
        const float crackWidth = 0.08f + density * 0.12f;
        const float crack1 = 1.f - smoothStep(0.f, crackWidth, std::fabs(n1)), crack2 = 1.f - smoothStep(0.f, crackWidth, std::fabs(n2));
        const float crackMask = std::max(crack1, crack2), lavaBlend = crackMask * glowStr;
        const float rockNoise = hash01(x, y, seed ^ 0x77) * 0.15f - 0.075f;
        float rr = (float(rock.red()) / 255.f + rockNoise) * (1.f - lavaBlend) + float(lava.red()) / 255.f * lavaBlend;
        float rg = (float(rock.green()) / 255.f + rockNoise) * (1.f - lavaBlend) + float(lava.green()) / 255.f * lavaBlend;
        float rb = (float(rock.blue()) / 255.f + rockNoise) * (1.f - lavaBlend) + float(lava.blue()) / 255.f * lavaBlend;
        if (crackMask > 0.7f) {
          const float boost = (crackMask - 0.7f) / 0.3f * glowStr;
          rr += boost * 0.6f;
          rg += boost * 0.3f;
          rb += boost * 0.05f;
        }
        return qRgba(toByte(rr * 255.f), toByte(rg * 255.f), toByte(rb * 255.f), 255);
      },
      [&](QImage &out) {
        QImage surface = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&surface);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (sparks) {
          const int sparkCount = (nw + nh) / 8;
          for (int i = 0; i < sparkCount; ++i) {
            if (context.cancelled()) return;
            const float sx = hash01(i, 0, seed ^ 0xAB) * float(nw), sy = hash01(0, i, seed ^ 0xCD) * float(nh);
            const bool inBorder = sx < float(border) || sx >= float(border + sw) || sy < float(border) || sy >= float(border + sh);
            if (!inBorder) continue;
            if (std::fabs(fractalNoise(sx * noiseScale, sy * noiseScale, 4, 2.f, 0.5f, seed)) > 0.15f) continue;
            const float brightness = hash01(i, i, seed ^ 0xEF);
            const QColor color(toByte(255.f * (0.8f + brightness * 0.2f)), toByte(180.f * brightness), toByte(40.f * brightness), toByte(150.f + brightness * 100.f));
            if (!drawBlurredCircle(painter, QPointF(sx, sy), double(1.f + brightness * 2.f), color, 2.f, context)) return;
          }
        }
        // Glow stroke round the picture: 4 px, blurred with sigma 6.
        QImage layer(nw, nh, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        {
          QPainter lp(&layer);
          lp.setRenderHint(QPainter::Antialiasing, true);
          lp.setBrush(Qt::NoBrush);
          lp.setPen(strokePen(QColor(lava.red(), lava.green(), lava.blue(), int(60.f * glowStr)), 4.0));
          lp.drawRect(QRectF(border, border, sw, sh));
        }
        const QImage straight = layer.convertToFormat(QImage::Format_ARGB32);
        FloatImage f = toPremul(straight);
        if (!gaussianBlur255(f, 6.f, false, context)) return;
        painter.drawImage(0, 0, fromPremul(f, straight));
        painter.end();
        const QColorSpace space = out.colorSpace();
        out = surface.convertToFormat(QImage::Format_ARGB32);
        out.setColorSpace(space);
      });
}

// ---- rainy_window ---------------------------------------------------------------------------------

EffectResult rainyWindow(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float distortionPx = floatOf(params, "distortion"), streakDensity = floatOf(params, "streak_density") / 100.f;
  const float mist = floatOf(params, "mist_amount") / 100.f, droplets = floatOf(params, "droplet_amount") / 100.f;
  if (distortionPx <= 0.f && streakDensity <= 0.f && mist <= 0.f && droplets <= 0.f) return identityCopy(source, context);
  const int seed = 8723 ^ seedOf(context);
  const float dropletCellSize = std::max(8.f, 30.f - droplets * 22.f);
  const float twoPi = kPi * 2.f;
  const Pixels p(source);
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const float yWave = float(y) * 0.09f;
    const float columnPhase = hash01(x / 8, 3, seed) * twoPi;
    const float waveA = std::sin(float(y) * 0.075f + columnPhase);
    const float waveB = std::sin(float(x) * 0.022f + float(y) * 0.15f + columnPhase * 0.5f);
    const float offsetX = distortionPx * (0.38f * waveA + 0.18f * waveB);
    const float offsetY = distortionPx * 0.08f * std::sin(float(x) * 0.08f + columnPhase * 1.3f);
    const float sx = float(x) + offsetX, sy = float(y) + offsetY;
    QRgb baseColor = bilinear(p, sx, sy);
    if (mist > 0.f) {
      const QRgb b0 = bilinear(p, sx - 1.25f, sy), b1 = bilinear(p, sx + 1.25f, sy), b2 = bilinear(p, sx, sy - 1.25f), b3 = bilinear(p, sx, sy + 1.25f);
      const QRgb blurColor = qRgba((qRed(b0) + qRed(b1) + qRed(b2) + qRed(b3)) / 4, (qGreen(b0) + qGreen(b1) + qGreen(b2) + qGreen(b3)) / 4,
                                   (qBlue(b0) + qBlue(b1) + qBlue(b2) + qBlue(b3)) / 4, (qAlpha(b0) + qAlpha(b1) + qAlpha(b2) + qAlpha(b3)) / 4);
      baseColor = lerpColor(baseColor, blurColor, mist * 0.45f);
    }
    const float columnNoise = hash01(x / 6, 11, seed);
    float streak = 0.f;
    if (columnNoise > 1.f - 0.72f * streakDensity) {
      const float signal = std::sin(yWave + columnNoise * 18.f);
      streak = std::pow(std::max(0.f, signal), 3.f) * streakDensity;
    }
    float dropletMask = 0.f;
    const int cellX = int(float(x) / dropletCellSize), cellY = int(float(y) / dropletCellSize);
    const float cellNoise = hash01(cellX, cellY, seed ^ 0x2A31);
    if (cellNoise > 0.93f - 0.55f * droplets) {
      const float cx = (float(cellX) + hash01(cellX, cellY, seed ^ 193)) * dropletCellSize;
      const float cy = (float(cellY) + hash01(cellX, cellY, seed ^ 431)) * dropletCellSize;
      const float radius = dropletCellSize * (0.12f + 0.24f * hash01(cellX, cellY, seed ^ 997));
      const float dx = float(x) - cx, dy = float(y) - cy;
      const float radial = std::sqrt(dx * dx + dy * dy) / std::max(radius, 0.001f);
      dropletMask = 1.f - smoothStep(0.2f, 1.f, radial);
    }
    const float lum = luminance01(baseColor);
    const QRgb fog = qRgba(186, 196, 206, qAlpha(baseColor));
    const QRgb mistColor = lerpColor(baseColor, fog, mist * (0.2f + (1.f - lum) * 0.8f));
    const float wetShade = 1.f - (0.08f * mist + 0.06f * streak);
    const float highlight = dropletMask * 0.50f + streak * 0.20f;
    return qRgba(toByte(float(qRed(mistColor)) * wetShade + highlight * 88.f), toByte(float(qGreen(mistColor)) * wetShade + highlight * 96.f),
                 toByte(float(qBlue(mistColor)) * wetShade + highlight * 116.f), qAlpha(mistColor));
  });
}

}  // namespace

void registerFilterEffects5(EffectRegistry &registry) {
  add(registry, "rope_border", "Rope border", "RopeBorder",
      {intParam("border_size", "Border size", 40, 12, 200), colorParam("rope_color", "Rope color", QColor(180, 145, 90)),
       colorParam("bg_color", "Background color", QColor(60, 45, 30)), floatParam("twist_density", "Twist density", 50, 10, 100),
       floatParam("fiber_detail", "Fiber detail", 55, 0, 100)},
      ropeBorder, false, true);
  add(registry, "heat_haze_refraction", "Heat haze refraction", "HeatHazeRefraction",
      {floatParam("strength", "Strength", 45, 0, 100), floatParam("frequency", "Frequency", 40, 0, 100),
       floatParam("blur_radius", "Blur radius", 10, 0, 200), floatParam("offset", "Offset", 6, -200, 200),
       floatParam("luminance_influence", "Luminance influence", 55, 0, 100)},
      heatHaze, true);
  add(registry, "old_camera_flash_burn", "Old camera flash burn", "OldCameraFlashBurn",
      {floatParam("flash_strength", "Flash strength", 70, 0, 100), floatParam("flash_radius", "Flash radius", 68, 20, 100),
       floatParam("edge_burn", "Edge burn", 45, 0, 100), floatParam("warmth", "Warmth", 35, 0, 100),
       floatParam("grain", "Grain", 20, 0, 100), floatParam("center_x", "Center X", 50, 0, 100),
       floatParam("center_y", "Center Y", 50, 0, 100)},
      oldCameraFlashBurn);
  add(registry, "datamosh_smear", "Datamosh smear", "DatamoshSmear",
      {enumParam("direction", "Direction", "horizontal", {"horizontal", "vertical"}), floatParam("smear_amount", "Smear amount", 58, 0, 100),
       floatParam("corruption", "Corruption", 36, 0, 100), intParam("block_size", "Block size", 12, 4, 64),
       floatParam("drift", "Drift", 24, -100, 100), floatParam("channel_split", "Channel split", 25, 0, 100)},
      datamoshSmear, true);
  add(registry, "tilt_shift", "Tilt-shift", "TiltShift",
      {enumParam("mode", "Mode", "linear", {"linear", "radial"}), floatParam("blur_radius", "Blur radius", 12, 0, 30),
       floatParam("focus_size", "Focus size", 30, 5, 90), floatParam("focus_position_x", "Focus position X", 50, 0, 100),
       floatParam("focus_position_y", "Focus position Y", 50, 0, 100), floatParam("falloff", "Falloff", 24, 1, 60),
       floatParam("saturation_boost", "Saturation boost", 35, 0, 100)},
      tiltShift);
  add(registry, "oil_slick_interference", "Oil slick interference", "OilSlickInterference",
      {floatParam("intensity", "Intensity", 58, 0, 100), floatParam("scale", "Scale", 70, 0, 100), floatParam("darkness", "Darkness", 45, 0, 100),
       floatParam("gloss", "Gloss", 60, 0, 100), floatParam("shift", "Shift", 0, 0, 360)},
      oilSlickInterference);
  add(registry, "frosted_glass_ice_edges", "Frosted glass ice edges", "FrostedGlassIceEdges",
      {floatParam("distortion", "Distortion", 10, 0, 30), floatParam("blur", "Blur", 35, 0, 100),
       intParam("edge_threshold", "Edge threshold", 28, 1, 120), floatParam("ice_strength", "Ice strength", 75, 0, 100),
       floatParam("tint", "Tint", 35, 0, 100)},
      frostedGlassIceEdges, true);
  add(registry, "liquid_mercury", "Liquid mercury", "LiquidMercury",
      {floatParam("reflection", "Reflection", 78, 0, 100), floatParam("ripple", "Ripple", 42, 0, 100), floatParam("shine", "Shine", 82, 0, 100),
       floatParam("fluidity", "Fluidity", 55, 0, 100), floatParam("depth", "Depth", 65, 0, 100)},
      liquidMercury);
  add(registry, "marble_border", "Marble border", "MarbleBorder",
      {intParam("border_size", "Border size", 50, 2, 300), colorParam("base_color", "Base color", QColor(235, 230, 220)),
       colorParam("vein_color", "Vein color", QColor(90, 80, 70)), floatParam("vein_strength", "Vein strength", 60, 0, 100),
       floatParam("vein_scale", "Vein scale", 30, 5, 100), boolParam("inner_bevel", "Inner bevel", true)},
      marbleBorder, false, true);
  add(registry, "pencil_sketch", "Pencil sketch", "PencilSketch",
      {intParam("blur_radius", "Blur radius", 8, 1, 24), floatParam("edge_strength", "Edge strength", 65, 0, 160),
       floatParam("pencil_darkness", "Pencil darkness", 70, 0, 100), floatParam("paper_brightness", "Paper brightness", 100, 40, 130)},
      pencilSketch);
  add(registry, "bloom", "Bloom", "Bloom",
      {floatParam("threshold", "Threshold", 65, 0, 100), floatParam("soft_knee", "Soft knee", 35, 0, 100),
       floatParam("radius", "Radius", 24, 1, 100), floatParam("intensity", "Intensity", 85, 0, 200)},
      bloom);
  add(registry, "etched_glass", "Etched glass", "EtchedGlass",
      {floatParam("frost", "Frost", 48, 0, 100), floatParam("engrave", "Engrave", 68, 0, 100), floatParam("refraction", "Refraction", 18, 0, 100),
       floatParam("highlight", "Highlight", 42, 0, 100), floatParam("background_fade", "Background fade", 38, 0, 100)},
      etchedGlass);
  add(registry, "halftone", "Halftone", "Halftone",
      {intParam("cell_size", "Cell size", 8, 3, 24), floatParam("dot_softness", "Dot softness", 18, 0, 100),
       floatParam("ink_strength", "Ink strength", 90, 0, 100), floatParam("source_blend", "Source blend", 20, 0, 100),
       floatParam("angle_offset", "Angle offset", 0, -45, 45)},
      halftone);
  add(registry, "chain_link_border", "Chain link border", "ChainLinkBorder",
      {intParam("border_size", "Border size", 52, 16, 250), colorParam("wire_color", "Wire color", QColor(170, 175, 180)),
       colorParam("bg_color", "Background color", QColor(35, 40, 48)), floatParam("cell_size", "Cell size", 20, 8, 60),
       floatParam("wire_width", "Wire width", 2.5, 1, 8), boolParam("highlight", "Wire highlight", true)},
      chainLinkBorder, false, true);
  add(registry, "claymation_texture", "Claymation texture", "ClaymationTexture",
      {intParam("chunkSize", "Chunk size", 8, 3, 18), floatParam("smoothness", "Smoothness", 55, 0, 100),
       floatParam("relief", "Relief", 35, 0, 100), floatParam("saturation", "Saturation", 20, -100, 100),
       floatParam("textureGrain", "Texture grain", 30, 0, 100)},
      claymationTexture, true);
  add(registry, "macos_window", "macOS window", "MacOSWindow",
      {textParam("title", "Title", ""), intParam("corner_radius", "Corner radius", 10, 0, 30), boolParam("dark_mode", "Dark mode", false),
       intParam("shadow_size", "Shadow size", 20, 0, 60), intParam("padding", "Padding", 20, 0, 100)},
      macosWindow, false, true);
  add(registry, "torn_edge", "Torn edge", "TornEdge",
      {intParam("depth", "Depth", 20, 1, 100), intParam("range", "Range", 20, 1, 100), boolParam("top", "Top", true),
       boolParam("right", "Right", true), boolParam("bottom", "Bottom", true), boolParam("left", "Left", true),
       boolParam("curved", "Curved edges", false)},
      tornEdge, true);
  add(registry, "ascii_art", "ASCII art", "ASCIIArt",
      {intParam("cell_size", "Cell size", 8, 4, 24), floatParam("contrast", "Contrast", 110, 50, 200),
       textParam("character_set", "Character set", "@%#*+=-:. "), boolParam("invert", "Invert", false),
       boolParam("dark_background", "Dark background", true), boolParam("use_source_color", "Use source color", true)},
      asciiArt);
  add(registry, "watercolor_border", "Watercolor border", "WatercolorBorder",
      {intParam("border_size", "Border size", 60, 10, 300), colorParam("wash_color", "Wash color", QColor(185, 160, 130)),
       floatParam("edge_roughness", "Edge roughness", 65, 0, 100), floatParam("bleed_amount", "Bleed amount", 40, 0, 100),
       floatParam("opacity", "Opacity", 90, 10, 100)},
      watercolorBorder, false, true);
  add(registry, "halation", "Halation", "Halation",
      {floatParam("threshold", "Threshold", 72, 0, 100), floatParam("radius", "Radius", 18, 1, 100),
       floatParam("strength", "Strength", 60, 0, 150), floatParam("warmth", "Warmth", 70, 0, 100)},
      halation);
  add(registry, "brick_wall_border", "Brick wall border", "BrickWallBorder",
      {intParam("border_size", "Border size", 56, 10, 300), colorParam("brick_color", "Brick color", QColor(178, 90, 62)),
       colorParam("mortar_color", "Mortar color", QColor(195, 188, 175)), floatParam("mortar_thickness", "Mortar thickness", 4, 1, 20),
       floatParam("color_variation", "Color variation", 40, 0, 100), floatParam("bevel_strength", "Bevel strength", 50, 0, 100)},
      brickWallBorder, false, true);
  add(registry, "voronoi_mosaic", "Voronoi mosaic", "VoronoiMosaic",
      {intParam("cell_count", "Cell Count", 300, 10, 2000), boolParam("draw_edges", "Draw Edges", true),
       floatParam("edge_thickness", "Edge Thickness", 1, 0.5, 5), colorParam("edge_color", "Edge Color", QColor(0, 0, 0, 255))},
      voronoiMosaic);
  add(registry, "riso_print", "Riso print", "RisoPrint",
      {floatParam("ink_strength", "Ink strength", 70, 0, 100), floatParam("paper_fade", "Paper fade", 25, 0, 100),
       floatParam("offset", "Offset", 3, -200, 200), floatParam("dot_scale", "Dot scale", 18, 0, 100),
       floatParam("ink_noise", "Ink noise", 35, 0, 100), colorParam("ink_color_a", "Ink color A", QColor(220, 70, 70, 255)),
       colorParam("ink_color_b", "Ink color B", QColor(70, 200, 210, 255))},
      risoPrint, true);
  add(registry, "starfield_border", "Starfield border", "StarfieldBorder",
      {intParam("border_size", "Border size", 60, 12, 300), colorParam("bg_color", "Background color", QColor(5, 5, 18)),
       colorParam("nebula_color", "Nebula tint", QColor(40, 10, 80)), floatParam("star_density", "Star density", 65, 10, 100),
       floatParam("nebula_intensity", "Nebula intensity", 45, 0, 100), boolParam("bright_stars", "Bright star glow", true)},
      starfieldBorder, false, true);
  add(registry, "crimson_border", "Crimson border", "CrimsonBorder",
      {intParam("size", "Size", 12, 2, 60), colorParam("color", "Color", QColor(153, 0, 18)), floatParam("glow_strength", "Glow strength", 50, 0, 100),
       boolParam("inner_line", "Inner line", true), boolParam("outer_line", "Outer line", true)},
      crimsonBorder, false, true);
  add(registry, "rust_corrosion", "Rust corrosion", "RustCorrosion",
      {floatParam("corrosion", "Corrosion", 58, 0, 100), floatParam("pitting", "Pitting", 44, 0, 100), floatParam("streaks", "Streaks", 36, 0, 100),
       floatParam("dirt", "Dirt", 26, 0, 100), floatParam("edge_wear", "Edge wear", 52, 0, 100)},
      rustCorrosion);
  add(registry, "lava_molten_border", "Lava molten border", "LavaMoltenBorder",
      {intParam("border_size", "Border size", 56, 12, 300), colorParam("rock_color", "Rock color", QColor(30, 25, 22)),
       colorParam("lava_color", "Lava color", QColor(255, 80, 10)), floatParam("crack_density", "Crack density", 60, 10, 100),
       floatParam("glow_intensity", "Glow intensity", 70, 10, 100), boolParam("ember_sparks", "Ember sparks", true)},
      lavaMoltenBorder, false, true);
  add(registry, "rainy_window", "Rainy window", "RainyWindow",
      {floatParam("distortion", "Distortion", 8, 0, 30), floatParam("streak_density", "Streak density", 45, 0, 100),
       floatParam("mist_amount", "Mist amount", 25, 0, 100), floatParam("droplet_amount", "Droplet amount", 35, 0, 100)},
      rainyWindow, true);
}

}  // namespace xerahs::effects
