// EDITOR-EFFECTS-001: the Filters category, batch 6. EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census source
// path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Skia blurs (SKImageFilter.CreateBlur, SKMaskFilter) are approximateGaussianBlur: a separable
//    3-sigma Gaussian. Mask-filter glows (circuit traces, stained-glass panels, ornate and Windows
//    shadows, matrix glyphs) are one blurred layer per band rather than a blur on every stroke.
//  - Neon pads by ceil(radius*2) and blurs with Clamp, sigma = radius/3, then crops.
//  - Skia paths, arcs and round-rects are QPainter with antialiasing. Skia AddArc is clockwise
//    from 3 o'clock; Qt's arc sweeps the other way, so the sweep is negated. Edge coverage can
//    differ by a few levels. Matrix glyphs are QPainter text when a QGuiApplication exists, and a
//    filled cell otherwise (guiless tests have no font engine). Windows title text is drawn only
//    when a QGuiApplication exists.
//  - SKColor.FromHsv uses QColor::fromHsvF (s and v scaled from 0..100).
//  - Snowfall, ink splatter and blood splash call Random.Shared with no stable seed. Those draws
//    use a positive mix of EffectContext::seed (seed 0 selects 1) and usesSeed is set.
//  - Settable Seed properties that are not census parameters (stained glass, liquid glass, mosaic,
//    wooden frame, pixel sorting, vintage print, matrix, nebula) are the reference default XOR
//    the seed, so seed 0 reproduces the reference. Frost (4519) and circuit (8157) stay fixed.
#include "Effect.h"
#include "EffectSupport.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;
const QString kReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

float clamp01(float v) { return v <= 0.f ? 0.f : (v >= 1.f ? 1.f : v); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float smoothStep(float e0, float e1, float v) {
  if (std::fabs(e1 - e0) < 0.000001f) return v >= e1 ? 1.f : 0.f;
  const float t = clamp01((v - e0) / (e1 - e0));
  return t * t * (3.f - 2.f * t);
}
int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int toByte(float v) {
  if (std::isnan(v) || v <= 0.f) return 0;
  if (v >= 255.f) return 255;
  return int(std::nearbyintf(v));
}
int truncByte(float v) {
  if (!(v > 0.f)) return 0;
  if (v >= 255.f) return 255;
  return int(v);
}
int iround(float v) { return int(std::nearbyintf(v)); }
float hash01(int x, int y, int seed = 0) {
  uint32_t h = 2166136261u;
  h = (h ^ uint32_t(int32_t(x))) * 16777619u;
  h = (h ^ uint32_t(int32_t(y))) * 16777619u;
  h = (h ^ uint32_t(int32_t(seed))) * 16777619u;
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return float(h & 0x00FFFFFFu) / 16777215.f;
}
float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
float valueNoise(float x, float y, int seed = 0) {
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const float tx = fade(x - float(x0)), ty = fade(y - float(y0));
  const float v00 = hash01(x0, y0, seed), v10 = hash01(x0 + 1, y0, seed);
  const float v01 = hash01(x0, y0 + 1, seed), v11 = hash01(x0 + 1, y0 + 1, seed);
  return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}
float fractalNoise(float x, float y, int octaves, float lacunarity, float gain, int seed = 0) {
  float value = 0.f, amplitude = 1.f, frequency = 1.f, normalization = 0.f;
  for (int i = 0; i < octaves; ++i) {
    value += valueNoise(x * frequency, y * frequency, seed + i * 1619) * amplitude;
    normalization += amplitude;
    frequency *= lacunarity;
    amplitude *= gain;
  }
  return normalization > 0.f ? value / normalization : 0.f;
}
float luminance(QRgb c) { return 0.2126f * float(qRed(c)) + 0.7152f * float(qGreen(c)) + 0.0722f * float(qBlue(c)); }
float luminance01(QRgb c) { return luminance(c) / 255.f; }
int signf(float v) { return v > 0.f ? 1 : (v < 0.f ? -1 : 0); }
int seedOf(const EffectContext &context) { return int(uint32_t(context.seed ^ (context.seed >> 32))); }
int positiveSeed(const EffectContext &context) {
  const int mixed = seedOf(context);
  return mixed > 0 ? mixed : 1;
}
QString textOf(const ParamValues &params, const char *id) { return params.value(QLatin1String(id)).toString(); }

struct Pixels {
  int w = 0, h = 0;
  const QRgb *data = nullptr;
  explicit Pixels(const QImage &image)
      : w(image.width()), h(image.height()), data(reinterpret_cast<const QRgb *>(image.constBits())) {}
  QRgb at(int x, int y) const { return data[size_t(y) * size_t(w) + size_t(x)]; }
};
QRgb bilinear(const Pixels &p, float x, float y) {
  const int right = p.w - 1, bottom = p.h - 1;
  x = std::clamp(x, 0.f, float(right));
  y = std::clamp(y, 0.f, float(bottom));
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const int x1 = x0 < right ? x0 + 1 : right, y1 = y0 < bottom ? y0 + 1 : bottom;
  const float tx = x - float(x0), ty = y - float(y0);
  const QRgb c00 = p.at(x0, y0), c10 = p.at(x1, y0), c01 = p.at(x0, y1), c11 = p.at(x1, y1);
  auto channel = [&](int (*f)(QRgb)) {
    return toByte(lerp(lerp(float(f(c00)), float(f(c10)), tx), lerp(float(f(c01)), float(f(c11)), tx), ty));
  };
  return qRgba(channel(qRed), channel(qGreen), channel(qBlue), channel(qAlpha));
}
QImage copyOf(const QImage &source) {
  QImage result = source.copy();
  result.setColorSpace(source.colorSpace());
  return result;
}
EffectResult identityCopy(const QImage &source, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  return {copyOf(source), {}};
}
template <class Op>
EffectResult mapPixels(const QImage &source, const EffectContext &context, Op op) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < source.width(); ++x) output[x] = op(x, y, input[x]);
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}
EffectResult publish(QImage surface, const QImage &like, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  if (surface.format() != QImage::Format_ARGB32) surface = surface.convertToFormat(QImage::Format_ARGB32);
  if (surface.isNull()) return {{}, kFailed};
  surface.setColorSpace(like.colorSpace());
  if (!runRows(context, std::max(1, surface.height()), 0, 100, [](int) {})) return {{}, kCancelled};
  return {surface, {}};
}
QImage premulCanvas(QSize size, QRgb fill) {
  QImage surface(size, QImage::Format_ARGB32_Premultiplied);
  surface.fill(QColor::fromRgba(fill));
  return surface;
}
QImage blurStraight(const QImage &straight, float sigma, const EffectContext &context) {
  return approximateGaussianBlur(straight, std::max(0.001f, sigma), BlurBorder::Decal, context, 0, 0);
}
void hsvToRgb(float h, float s, float v, float &r, float &g, float &b) {
  h = h - std::floor(h);
  if (s <= 0.0001f) {
    r = g = b = v;
    return;
  }
  const float scaled = h * 6.f;
  const int sector = int(std::floor(scaled));
  const float frac = scaled - float(sector);
  const float p = v * (1.f - s), q = v * (1.f - s * frac), t = v * (1.f - s * (1.f - frac));
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}
QColor fromHsv100(float hue, float sat, float val) {
  // Skia FromHsv takes s and v in 0..100. QColor::fromHsvF is the closest match.
  return QColor::fromHsvF(std::fmod(std::fmod(hue, 360.f) + 360.f, 360.f) / 360.f, std::clamp(sat / 100.f, 0.f, 1.f),
                          std::clamp(val / 100.f, 0.f, 1.f));
}
void skiaArc(QPainterPath &path, const QRectF &rect, float startDeg, float sweepDeg) {
  QPainterPath arc;
  arc.arcMoveTo(rect, startDeg);
  arc.arcTo(rect, startDeg, -sweepDeg);
  path.addPath(arc);
}
QRgb darkenTrunc(QRgb c, float factor) {
  const float keep = 1.f - factor;
  return qRgba(truncByte(float(qRed(c)) * keep), truncByte(float(qGreen(c)) * keep), truncByte(float(qBlue(c)) * keep), qAlpha(c));
}
QRgb lightenTrunc(QRgb c, float factor) {
  auto ch = [&](int v) { return truncByte(float(v) + float(255 - v) * factor); };
  return qRgba(ch(qRed(c)), ch(qGreen(c)), ch(qBlue(c)), qAlpha(c));
}
QRgb withAlpha(QRgb c, int a) { return qRgba(qRed(c), qGreen(c), qBlue(c), clampInt(a, 0, 255)); }

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

// ---- cartoon sticker cutout -----------------------------------------------------------------------
EffectResult cartoonSticker(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int levels = clampInt(intOf(params, "colorLevels"), 2, 20);
  const int edgeThreshold = clampInt(intOf(params, "edgeThreshold"), 5, 120);
  const float inkStrength = std::clamp(floatOf(params, "inkStrength"), 0.f, 100.f) / 100.f;
  const int borderRadius = clampInt(intOf(params, "stickerBorder"), 0, 10);
  const float borderStrength = std::clamp(floatOf(params, "borderStrength"), 0.f, 100.f) / 100.f;
  const int width = source.width(), height = source.height(), right = width - 1, bottom = height - 1;
  const float step = 255.f / float(levels - 1);
  std::vector<QRgb> quantized(size_t(width) * size_t(height));
  std::vector<uint8_t> edgeMask(quantized.size()), borderMask;
  auto quant = [&](int value) { return uint8_t(toByte(std::nearbyintf(float(value) / step) * step)); };
  for (int y = 0; y < height; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    for (int x = 0; x < width; ++x) quantized[size_t(y * width + x)] = qRgba(quant(qRed(row[x])), quant(qGreen(row[x])), quant(qBlue(row[x])), qAlpha(row[x]));
  }
  for (int y = 0; y < height; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    const int yUp = y > 0 ? y - 1 : 0, yDown = y < bottom ? y + 1 : bottom;
    for (int x = 0; x < width; ++x) {
      const int xLeft = x > 0 ? x - 1 : 0, xRight = x < right ? x + 1 : right;
      const QRgb left = quantized[size_t(y * width + xLeft)], rightColor = quantized[size_t(y * width + xRight)];
      const QRgb up = quantized[size_t(yUp * width + x)], down = quantized[size_t(yDown * width + x)];
      const int gradX = std::abs(qRed(rightColor) - qRed(left)) + std::abs(qGreen(rightColor) - qGreen(left)) + std::abs(qBlue(rightColor) - qBlue(left));
      const int gradY = std::abs(qRed(down) - qRed(up)) + std::abs(qGreen(down) - qGreen(up)) + std::abs(qBlue(down) - qBlue(up));
      const int edge = (gradX + gradY) / 6;
      const int normalized = (edge - edgeThreshold) * 255 / std::max(1, 255 - edgeThreshold);
      edgeMask[size_t(y * width + x)] = uint8_t(normalized <= 0 ? 0 : std::min(255, normalized));
    }
  }
  const bool wantBorder = borderRadius > 0 && borderStrength > 0.001f;
  if (wantBorder) {
    borderMask.assign(edgeMask.size(), 0);
    const int radiusSq = borderRadius * borderRadius;
    for (int y = 0; y < height; ++y) {
      if (context.cancelled()) return {{}, kCancelled};
      for (int x = 0; x < width; ++x) {
        int maxV = 0;
        for (int ky = -borderRadius; ky <= borderRadius && maxV < 255; ++ky) {
          const int sy = clampInt(y + ky, 0, bottom);
          for (int kx = -borderRadius; kx <= borderRadius; ++kx) {
            if (kx * kx + ky * ky > radiusSq) continue;
            const int sx = clampInt(x + kx, 0, right);
            maxV = std::max(maxV, int(edgeMask[size_t(sy * width + sx)]));
            if (maxV == 255) break;
          }
        }
        borderMask[size_t(y * width + x)] = uint8_t(maxV);
      }
    }
  }
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const size_t i = size_t(y * width + x);
      const QRgb base = quantized[i];
      float r = float(qRed(base)), g = float(qGreen(base)), b = float(qBlue(base));
      const float edge = float(edgeMask[i]) / 255.f;
      if (wantBorder) {
        const float borderOnly = std::max(0.f, float(borderMask[i]) / 255.f - edge * 0.65f);
        const float borderMix = borderOnly * borderStrength;
        if (borderMix > 0.001f) {
          r = lerp(r, 255.f, borderMix);
          g = lerp(g, 255.f, borderMix);
          b = lerp(b, 255.f, borderMix);
        }
      }
      const float inkMix = edge * inkStrength;
      if (inkMix > 0.001f) {
        r = lerp(r, 0.f, inkMix);
        g = lerp(g, 0.f, inkMix);
        b = lerp(b, 0.f, inkMix);
      }
      output[x] = qRgba(toByte(r), toByte(g), toByte(b), qAlpha(base));
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// ---- jpeg artifact --------------------------------------------------------------------------------
void toYCbCr(QRgb color, float &y, float &cb, float &cr) {
  const float r = float(qRed(color)), g = float(qGreen(color)), b = float(qBlue(color));
  y = 0.299f * r + 0.587f * g + 0.114f * b;
  cb = 128.f - 0.168736f * r - 0.331264f * g + 0.5f * b;
  cr = 128.f + 0.5f * r - 0.418688f * g - 0.081312f * b;
}
QRgb fromYCbCr(float y, float cb, float cr, int alpha) {
  const float r = y + 1.402f * (cr - 128.f);
  const float g = y - 0.344136f * (cb - 128.f) - 0.714136f * (cr - 128.f);
  const float b = y + 1.772f * (cb - 128.f);
  return qRgba(toByte(r), toByte(g), toByte(b), alpha);
}
EffectResult jpegArtifact(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float amount = std::clamp(floatOf(params, "compression"), 0.f, 100.f) / 100.f;
  const int blockSize = clampInt(intOf(params, "block_size"), 4, 32);
  const float chromaBleed = std::clamp(floatOf(params, "chroma_bleed"), 0.f, 100.f) / 100.f;
  const float ringing = std::clamp(floatOf(params, "ringing"), 0.f, 100.f) / 100.f;
  if (amount <= 0.0001f && ringing <= 0.0001f) return identityCopy(source, context);
  const int width = source.width(), height = source.height();
  const Pixels src(source);
  std::vector<QRgb> compressed(size_t(width) * size_t(height));
  const float lumaStep = 1.f + amount * 22.f, chromaStep = 2.f + amount * 38.f;
  const float chromaMix = 0.15f + chromaBleed * 0.70f + amount * 0.08f;
  auto quant = [](float value, float step) { return std::nearbyintf(value / step) * step; };
  for (int by = 0; by < height; by += blockSize) {
    if (context.cancelled()) return {{}, kCancelled};
    const int y1 = std::min(height, by + blockSize);
    for (int bx = 0; bx < width; bx += blockSize) {
      const int x1 = std::min(width, bx + blockSize);
      float avgY = 0.f, avgCb = 0.f, avgCr = 0.f;
      int count = 0;
      for (int y = by; y < y1; ++y)
        for (int x = bx; x < x1; ++x) {
          float yy, cb, cr;
          toYCbCr(src.at(x, y), yy, cb, cr);
          avgY += yy;
          avgCb += cb;
          avgCr += cr;
          ++count;
        }
      if (count > 0) {
        const float inv = 1.f / float(count);
        avgY *= inv;
        avgCb *= inv;
        avgCr *= inv;
      }
      for (int y = by; y < y1; ++y)
        for (int x = bx; x < x1; ++x) {
          const QRgb pixel = src.at(x, y);
          float yy, cb, cr;
          toYCbCr(pixel, yy, cb, cr);
          compressed[size_t(y * width + x)] = fromYCbCr(quant(lerp(yy, avgY, amount * 0.16f), lumaStep), quant(lerp(cb, avgCb, chromaMix), chromaStep),
                                                        quant(lerp(cr, avgCr, chromaMix), chromaStep), qAlpha(pixel));
        }
    }
  }
  if (ringing <= 0.0001f) {
    QImage result = makeResult(source);
    if (result.isNull()) return {{}, kFailed};
    const bool done = runRows(context, height, 0, 100, [&](int y) {
      std::copy_n(compressed.data() + size_t(y * width), width, reinterpret_cast<QRgb *>(result.scanLine(y)));
    });
    if (!done) return {{}, kCancelled};
    return {result, {}};
  }
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb current = compressed[size_t(y * width + x)];
      const int localX = x % blockSize, localY = y % blockSize;
      const float boundary = std::max(localX <= 1 || localX >= blockSize - 2 ? 1.f : 0.f, localY <= 1 || localY >= blockSize - 2 ? 1.f : 0.f);
      if (boundary <= 0.f) {
        output[x] = current;
        continue;
      }
      const float lumLeft = luminance(compressed[size_t(y * width + std::max(0, x - 1))]);
      const float lumRight = luminance(compressed[size_t(y * width + std::min(width - 1, x + 1))]);
      const float lumTop = luminance(compressed[size_t(std::max(0, y - 1) * width + x)]);
      const float lumBottom = luminance(compressed[size_t(std::min(height - 1, y + 1) * width + x)]);
      const float edge = ((lumRight - lumLeft) + (lumBottom - lumTop)) * 0.25f;
      const float oscillation = std::sin((float(x + y) * 0.82f) + (float(x / blockSize + y / blockSize) * 1.7f));
      const float sharpen = edge * oscillation * ringing * boundary * 0.32f;
      output[x] = qRgba(toByte(float(qRed(current)) + sharpen * 1.05f), toByte(float(qGreen(current)) + sharpen * 0.86f),
                        toByte(float(qBlue(current)) - sharpen * 0.30f), qAlpha(current));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- perlin ---------------------------------------------------------------------------------------
const uint8_t kPerm[256] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,
    190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125,
    136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,
    245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164,
    100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,
    182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253, 19,  98,  108,
    110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,
    239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,
    72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180};
float perlinGrad(int hash, float x, float y) {
  switch (hash & 3) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    default: return -x - y;
  }
}
float perlin2(float x, float y) {
  const int xi = int(std::floor(x)) & 255, yi = int(std::floor(y)) & 255;
  const float xf = x - std::floor(x), yf = y - std::floor(y);
  const float u = fade(xf), v = fade(yf);
  const int aa = kPerm[(kPerm[xi] + yi) & 255];
  const int ab = kPerm[(kPerm[xi] + yi + 1) & 255];
  const int ba = kPerm[(kPerm[(xi + 1) & 255] + yi) & 255];
  const int bb = kPerm[(kPerm[(xi + 1) & 255] + yi + 1) & 255];
  const float x0 = lerp(perlinGrad(aa, xf, yf), perlinGrad(ba, xf - 1.f, yf), u);
  const float x1 = lerp(perlinGrad(ab, xf, yf - 1.f), perlinGrad(bb, xf - 1.f, yf - 1.f), u);
  return lerp(x0, x1, v);
}
float fractalPerlin(float x, float y, int octaves, float persistence, int noiseSeed) {
  float value = 0.f, amplitude = 1.f, frequency = 1.f, maxAmplitude = 0.f;
  for (int i = 0; i < octaves; ++i) {
    const float ox = float(noiseSeed) * 137.3f + float(i) * 47.7f;
    const float oy = float(noiseSeed) * 251.1f + float(i) * 89.3f;
    value += perlin2(x * frequency + ox, y * frequency + oy) * amplitude;
    maxAmplitude += amplitude;
    frequency *= 2.f;
    amplitude *= persistence;
  }
  return maxAmplitude > 0.f ? value / maxAmplitude : 0.f;
}
EffectResult perlinNoise(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float strength = std::clamp(floatOf(params, "strength"), 0.f, 100.f) / 100.f;
  if (strength <= 0.f) return identityCopy(source, context);
  const float scale = std::max(1.f, floatOf(params, "scale"));
  const int octaves = clampInt(intOf(params, "octaves"), 1, 8);
  const float persistence = std::clamp(floatOf(params, "persistence"), 0.f, 1.f);
  const bool monochrome = boolOf(params, "monochrome");
  const float invScale = 1.f / scale;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float nr, ng, nb;
    if (monochrome) {
      nr = ng = nb = (fractalPerlin(float(x) * invScale, float(y) * invScale, octaves, persistence, 0) + 1.f) * 0.5f;
    } else {
      nr = (fractalPerlin(float(x) * invScale, float(y) * invScale, octaves, persistence, 0) + 1.f) * 0.5f;
      ng = (fractalPerlin(float(x) * invScale, float(y) * invScale, octaves, persistence, 1) + 1.f) * 0.5f;
      nb = (fractalPerlin(float(x) * invScale, float(y) * invScale, octaves, persistence, 2) + 1.f) * 0.5f;
    }
    return qRgba(toByte(lerp(float(qRed(src)) / 255.f, nr, strength) * 255.f), toByte(lerp(float(qGreen(src)) / 255.f, ng, strength) * 255.f),
                 toByte(lerp(float(qBlue(src)) / 255.f, nb, strength) * 255.f), qAlpha(src));
  });
}

// ---- dithering ------------------------------------------------------------------------------------
void quantizePalette(float r, float g, float b, const QString &palette, int &qr, int &qg, int &qb) {
  auto channel = [](float value, int levels) {
    const float clamped = std::clamp(value, 0.f, 255.f) / 255.f;
    const int steps = levels - 1;
    const float output = float(int(std::nearbyintf(clamped * float(steps)))) / float(steps);
    return toByte(output * 255.f);
  };
  if (palette == QLatin1String("web_safe_216")) {
    qr = channel(r, 6);
    qg = channel(g, 6);
    qb = channel(b, 6);
  } else if (palette == QLatin1String("rgb332")) {
    qr = channel(r, 8);
    qg = channel(g, 8);
    qb = channel(b, 4);
  } else if (palette == QLatin1String("grayscale_4")) {
    qr = qg = qb = channel(0.2126f * r + 0.7152f * g + 0.0722f * b, 4);
  } else {
    const int bw = (0.2126f * r + 0.7152f * g + 0.0722f * b) >= 128.f ? 255 : 0;
    qr = qg = qb = bw;
  }
}
EffectResult dithering(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float strength = std::clamp(floatOf(params, "strength"), 0.f, 100.f) / 100.f;
  if (strength <= 0.f) return identityCopy(source, context);
  const QString method = choiceOf(params, "method"), palette = choiceOf(params, "palette");
  const bool serpentine = boolOf(params, "serpentine");
  const int width = source.width(), height = source.height(), count = width * height;
  const Pixels src(source);
  std::vector<QRgb> dithered(static_cast<size_t>(count));
  if (method == QLatin1String("bayer_4x4")) {
    static const int matrix[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int y = 0; y < height; ++y) {
      if (context.cancelled()) return {{}, kCancelled};
      for (int x = 0; x < width; ++x) {
        const QRgb pixel = src.at(x, y);
        const float bias = (float(matrix[y & 3][x & 3]) / 16.f - 0.5f) * 36.f;
        int qr, qg, qb;
        quantizePalette(float(qRed(pixel)) + bias, float(qGreen(pixel)) + bias, float(qBlue(pixel)) + bias, palette, qr, qg, qb);
        dithered[size_t(y * width + x)] = qRgba(qr, qg, qb, qAlpha(pixel));
      }
    }
  } else {
    std::vector<float> r(static_cast<size_t>(count)), g(static_cast<size_t>(count)), b(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      r[size_t(i)] = float(qRed(src.data[i]));
      g[size_t(i)] = float(qGreen(src.data[i]));
      b[size_t(i)] = float(qBlue(src.data[i]));
    }
    auto distribute = [&](int x, int y, float er, float eg, float eb, float factor) {
      if (x < 0 || x >= width || y < 0 || y >= height) return;
      const size_t index = size_t(y * width + x);
      r[index] = std::clamp(r[index] + er * factor, 0.f, 255.f);
      g[index] = std::clamp(g[index] + eg * factor, 0.f, 255.f);
      b[index] = std::clamp(b[index] + eb * factor, 0.f, 255.f);
    };
    for (int y = 0; y < height; ++y) {
      if (context.cancelled()) return {{}, kCancelled};
      const bool reverse = serpentine && (y & 1) == 1;
      const int xStart = reverse ? width - 1 : 0, xEnd = reverse ? -1 : width, xStep = reverse ? -1 : 1;
      for (int x = xStart; x != xEnd; x += xStep) {
        const size_t index = size_t(y * width + x);
        int qr, qg, qb;
        quantizePalette(r[index], g[index], b[index], palette, qr, qg, qb);
        dithered[index] = qRgba(qr, qg, qb, qAlpha(src.data[index]));
        const float er = r[index] - float(qr), eg = g[index] - float(qg), eb = b[index] - float(qb);
        const int dir = reverse ? -1 : 1;
        distribute(x + dir, y, er, eg, eb, 7.f / 16.f);
        distribute(x - dir, y + 1, er, eg, eb, 3.f / 16.f);
        distribute(x, y + 1, er, eg, eb, 5.f / 16.f);
        distribute(x + dir, y + 1, er, eg, eb, 1.f / 16.f);
      }
    }
  }
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb dst = dithered[size_t(y * width + x)];
      output[x] = qRgba(toByte(lerp(float(qRed(input[x])), float(qRed(dst)), strength)), toByte(lerp(float(qGreen(input[x])), float(qGreen(dst)), strength)),
                        toByte(lerp(float(qBlue(input[x])), float(qBlue(dst)), strength)), qAlpha(input[x]));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- neon edge glow -------------------------------------------------------------------------------
EffectResult neonEdge(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float edgeStrength = std::clamp(floatOf(params, "edge_strength"), 0.1f, 6.f);
  const int threshold = clampInt(intOf(params, "threshold"), 0, 255);
  const float glowRadius = std::clamp(floatOf(params, "glow_radius"), 0.f, 40.f);
  const float glowIntensity = std::clamp(floatOf(params, "glow_intensity"), 0.f, 250.f) / 100.f;
  const float baseDim = std::clamp(floatOf(params, "base_dim"), 0.f, 100.f) / 100.f;
  const QColor neon = colorOf(params, "neon_color");
  const int width = source.width(), height = source.height(), right = width - 1, bottom = height - 1;
  const float neonAlpha = float(neon.alpha()) / 255.f;
  std::vector<uint8_t> luma(size_t(width) * size_t(height));
  std::vector<QRgb> core(luma.size());
  const Pixels src(source);
  for (int i = 0; i < width * height; ++i) {
    const QRgb c = src.data[i];
    luma[size_t(i)] = uint8_t(((qRed(c) * 77) + (qGreen(c) * 150) + (qBlue(c) * 29)) >> 8);
  }
  for (int y = 0; y < height; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    const int y0 = clampInt(y - 1, 0, bottom), y2 = clampInt(y + 1, 0, bottom);
    for (int x = 0; x < width; ++x) {
      const int x0 = clampInt(x - 1, 0, right), x2 = clampInt(x + 1, 0, right);
      const int p00 = luma[size_t(y0 * width + x0)], p01 = luma[size_t(y0 * width + x)], p02 = luma[size_t(y0 * width + x2)];
      const int p10 = luma[size_t(y * width + x0)], p12 = luma[size_t(y * width + x2)];
      const int p20 = luma[size_t(y2 * width + x0)], p21 = luma[size_t(y2 * width + x)], p22 = luma[size_t(y2 * width + x2)];
      const int gx = (-p00 + p02) + (-2 * p10 + 2 * p12) + (-p20 + p22);
      const int gy = (-p00 - 2 * p01 - p02) + (p20 + 2 * p21 + p22);
      const float magnitude = std::sqrt(float(gx * gx + gy * gy)) * edgeStrength;
      float mask = 0.f;
      if (magnitude > float(threshold)) {
        const float t = clamp01((magnitude - float(threshold)) / std::max(1.f, 255.f - float(threshold)));
        mask = t * t * (3.f - 2.f * t) * neonAlpha;
      }
      const QRgb pixel = src.at(x, y);
      core[size_t(y * width + x)] = qRgba(toByte(float(neon.red()) * mask), toByte(float(neon.green()) * mask), toByte(float(neon.blue()) * mask),
                                          toByte(float(qAlpha(pixel)) * mask));
    }
  }
  QImage coreImage(width, height, QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y) std::copy_n(core.data() + size_t(y * width), width, reinterpret_cast<QRgb *>(coreImage.scanLine(y)));
  QImage glow = coreImage;
  if (glowRadius > 0.01f) {
    const int padding = std::max(2, int(std::ceil(glowRadius * 2.f)));
    QImage expanded(width + padding * 2, height + padding * 2, QImage::Format_ARGB32);
    for (int y = 0; y < expanded.height(); ++y) {
      auto *row = reinterpret_cast<QRgb *>(expanded.scanLine(y));
      for (int x = 0; x < expanded.width(); ++x) row[x] = coreImage.pixel(clampInt(x - padding, 0, width - 1), clampInt(y - padding, 0, height - 1));
    }
    // Reference pads with a clamp shader, then Skia-blurs at sigma = radius/3 and crops.
    const QImage blurred = approximateGaussianBlur(expanded, std::max(0.001f, glowRadius / 3.f), BlurBorder::Clamp, context, 0, 0);
    if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
    glow = blurred.copy(padding, padding, width, height);
  }
  const float baseFactor = 1.f - baseDim;
  const float coreGain = 0.85f + 0.35f * std::min(glowIntensity, 1.6f);
  const Pixels glowPixels(glow);
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb pixel = src.at(x, y), c = core[size_t(y * width + x)], g = glowPixels.at(x, y);
      output[x] = qRgba(toByte(float(qRed(pixel)) * baseFactor + float(qRed(c)) * coreGain + float(qRed(g)) * glowIntensity),
                        toByte(float(qGreen(pixel)) * baseFactor + float(qGreen(c)) * coreGain + float(qGreen(g)) * glowIntensity),
                        toByte(float(qBlue(pixel)) * baseFactor + float(qBlue(c)) * coreGain + float(qBlue(g)) * glowIntensity), qAlpha(pixel));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- watercolor / kuwahara ------------------------------------------------------------------------
EffectResult watercolor(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const int radius = clampInt(intOf(params, "radius"), 2, 10);
  const float saturationBoost = std::clamp(floatOf(params, "saturation_boost"), 0.f, 100.f) / 100.f;
  const float detailBlend = std::clamp(floatOf(params, "detail_blend"), 0.f, 100.f) / 100.f;
  const int stride = width + 1;
  std::vector<double> integralR(size_t(stride * (height + 1))), integralG(integralR.size()), integralB(integralR.size()), integralL(integralR.size()),
      integralL2(integralR.size());
  const Pixels src(source);
  for (int y = 1; y <= height; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    double rowR = 0, rowG = 0, rowB = 0, rowL = 0, rowL2 = 0;
    for (int x = 1; x <= width; ++x) {
      const QRgb c = src.at(x - 1, y - 1);
      const double l = (0.2126 * qRed(c) + 0.7152 * qGreen(c) + 0.0722 * qBlue(c)) / 255.0;
      rowR += qRed(c);
      rowG += qGreen(c);
      rowB += qBlue(c);
      rowL += l;
      rowL2 += l * l;
      const int index = y * stride + x, prev = (y - 1) * stride + x;
      integralR[size_t(index)] = integralR[size_t(prev)] + rowR;
      integralG[size_t(index)] = integralG[size_t(prev)] + rowG;
      integralB[size_t(index)] = integralB[size_t(prev)] + rowB;
      integralL[size_t(index)] = integralL[size_t(prev)] + rowL;
      integralL2[size_t(index)] = integralL2[size_t(prev)] + rowL2;
    }
  }
  auto rectSum = [&](const std::vector<double> &integral, int x0, int y0, int x1, int y1) {
    return integral[size_t(y1 * stride + x1)] - integral[size_t(y0 * stride + x1)] - integral[size_t(y1 * stride + x0)] + integral[size_t(y0 * stride + x0)];
  };
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      double bestVariance = 1e300, meanR = 0, meanG = 0, meanB = 0;
      const int windows[4][4] = {{x - radius, y - radius, x + 1, y + 1}, {x, y - radius, x + radius + 1, y + 1}, {x - radius, y, x + 1, y + radius + 1},
                                 {x, y, x + radius + 1, y + radius + 1}};
      for (const auto &w : windows) {
        const int left = clampInt(w[0], 0, width), top = clampInt(w[1], 0, height), right = clampInt(w[2], 0, width), bottom = clampInt(w[3], 0, height);
        if (right <= left || bottom <= top) continue;
        const int count = (right - left) * (bottom - top);
        const double inv = 1.0 / double(count);
        const double sumL = rectSum(integralL, left, top, right, bottom);
        const double meanL = sumL * inv;
        const double variance = rectSum(integralL2, left, top, right, bottom) * inv - meanL * meanL;
        if (variance < bestVariance) {
          bestVariance = variance;
          meanR = rectSum(integralR, left, top, right, bottom) * inv;
          meanG = rectSum(integralG, left, top, right, bottom) * inv;
          meanB = rectSum(integralB, left, top, right, bottom) * inv;
        }
      }
      float r = float(meanR), g = float(meanG), b = float(meanB);
      if (saturationBoost > 0.f) {
        const float gray = (r + g + b) / 3.f, factor = 1.f + saturationBoost;
        r = gray + (r - gray) * factor;
        g = gray + (g - gray) * factor;
        b = gray + (b - gray) * factor;
      }
      r = float(toByte(r));
      g = float(toByte(g));
      b = float(toByte(b));
      const QRgb pixel = src.at(x, y);
      if (detailBlend > 0.f) {
        const float t = std::clamp(detailBlend * 0.55f, 0.f, 1.f);
        r = float(toByte(lerp(r, float(qRed(pixel)), t)));
        g = float(toByte(lerp(g, float(qGreen(pixel)), t)));
        b = float(toByte(lerp(b, float(qBlue(pixel)), t)));
      }
      output[x] = qRgba(int(r), int(g), int(b), qAlpha(pixel));
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- bevel ----------------------------------------------------------------------------------------
EffectResult bevel(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int size = clampInt(intOf(params, "size"), 1, 40);
  const float strength = std::clamp(floatOf(params, "strength"), 0.f, 100.f) / 100.f;
  const QColor highlight = colorOf(params, "highlight_color"), shadow = colorOf(params, "shadow_color");
  if (strength <= 0.f || (highlight.alpha() == 0 && shadow.alpha() == 0)) return identityCopy(source, context);
  const int width = source.width(), height = source.height();
  const Pixels src(source);
  bool transparent = false;
  for (int i = 0; i < width * height; ++i)
    if (qAlpha(src.data[i]) != 255) {
      transparent = true;
      break;
    }
  const float angle = floatOf(params, "light_angle") * kPi / 180.f;
  const float dirX = std::cos(angle), dirY = std::sin(angle);
  const int sampleCount = clampInt(size, 1, 12);
  std::vector<float> offsets(static_cast<size_t>(sampleCount)), weights(static_cast<size_t>(sampleCount));
  float weightSum = 0.f;
  for (int i = 0; i < sampleCount; ++i) {
    const float t = float(i + 1) / float(sampleCount);
    offsets[size_t(i)] = t * float(size);
    weights[size_t(i)] = 1.f - t * 0.72f;
    weightSum += weights[size_t(i)];
  }
  auto edgeWeights = [&](int length, bool fromStart) {
    std::vector<float> out(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
      const int distance = fromStart ? i : (length - 1) - i;
      out[size_t(i)] = distance < 0 || distance >= size ? 0.f : 1.f - smoothStep(0.f, float(size), float(distance));
    }
    return out;
  };
  const std::vector<float> leftW = edgeWeights(width, true), rightW = edgeWeights(width, false), topW = edgeWeights(height, true), bottomW = edgeWeights(height, false);
  std::vector<int> sampleX(static_cast<size_t>(sampleCount)), sampleY(static_cast<size_t>(sampleCount));
  if (transparent)
    for (int i = 0; i < sampleCount; ++i) {
      sampleX[size_t(i)] = iround(dirX * offsets[size_t(i)]);
      sampleY[size_t(i)] = iround(dirY * offsets[size_t(i)]);
    }
  auto sampleAlpha = [&](float x, float y) {
    x = std::clamp(x, 0.f, float(width - 1));
    y = std::clamp(y, 0.f, float(height - 1));
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const int x1 = x0 < width - 1 ? x0 + 1 : width - 1, y1 = y0 < height - 1 ? y0 + 1 : height - 1;
    const float tx = x - float(x0), ty = y - float(y0);
    const float a0 = lerp(float(qAlpha(src.at(x0, y0))), float(qAlpha(src.at(x1, y0))), tx);
    const float a1 = lerp(float(qAlpha(src.at(x0, y1))), float(qAlpha(src.at(x1, y1))), tx);
    return lerp(a0, a1, ty) / 255.f;
  };
  const float highlightAlpha = float(highlight.alpha()) / 255.f, shadowAlpha = float(shadow.alpha()) / 255.f;
  return mapPixels(source, context, [&](int x, int y, QRgb original) {
    if (qAlpha(original) == 0) return original;
    const float leftWeight = leftW[size_t(x)], rightWeight = rightW[size_t(x)], topWeight = topW[size_t(y)], bottomWeight = bottomW[size_t(y)];
    float response = ((leftWeight * -dirX) + (rightWeight * dirX) + (topWeight * -dirY) + (bottomWeight * dirY)) * 0.95f;
    float edgePresence = clamp01(std::max(std::max(leftWeight, rightWeight), std::max(topWeight, bottomWeight)) +
                                 std::min(leftWeight + rightWeight + topWeight + bottomWeight, 1.f) * 0.35f);
    if (transparent) {
      float alphaResponse = 0.f, alphaEdge = 0.f;
      for (int i = 0; i < sampleCount; ++i) {
        const float toward = sampleAlpha(float(x + sampleX[size_t(i)]), float(y + sampleY[size_t(i)]));
        const float away = sampleAlpha(float(x - sampleX[size_t(i)]), float(y - sampleY[size_t(i)]));
        const float delta = away - toward;
        alphaResponse += delta * weights[size_t(i)];
        alphaEdge += std::fabs(delta) * weights[size_t(i)];
      }
      alphaResponse /= weightSum;
      response += alphaResponse * 1.25f;
      edgePresence = clamp01(std::max(edgePresence, clamp01((alphaEdge / weightSum) * 1.45f)));
    }
    const float originalAlpha = float(qAlpha(original)) / 255.f;
    const float highlightMix = clamp01(std::max(0.f, response) * 1.55f * strength) * edgePresence * highlightAlpha * originalAlpha;
    const float shadowMix = clamp01(std::max(0.f, -response) * 1.55f * strength) * edgePresence * shadowAlpha * originalAlpha;
    float red = float(qRed(original)), green = float(qGreen(original)), blue = float(qBlue(original));
    if (shadowMix > 0.f) {
      red = lerp(red, float(shadow.red()), shadowMix);
      green = lerp(green, float(shadow.green()), shadowMix);
      blue = lerp(blue, float(shadow.blue()), shadowMix);
    }
    if (highlightMix > 0.f) {
      red = lerp(red, float(highlight.red()), highlightMix);
      green = lerp(green, float(highlight.green()), highlightMix);
      blue = lerp(blue, float(highlight.blue()), highlightMix);
    }
    const float lift = (highlightMix - shadowMix) * 26.f * strength;
    return qRgba(toByte(red + lift), toByte(green + lift), toByte(blue + lift), qAlpha(original));
  });
}

// ---- pixel sorting --------------------------------------------------------------------------------
float hue01(QRgb color) {
  const float r = float(qRed(color)) / 255.f, g = float(qGreen(color)) / 255.f, b = float(qBlue(color)) / 255.f;
  const float maxV = std::max(r, std::max(g, b)), minV = std::min(r, std::min(g, b)), delta = maxV - minV;
  if (delta <= 0.0001f) return 0.f;
  float hue;
  if (maxV == r) hue = std::fmod((g - b) / delta, 6.f);
  else if (maxV == g) hue = ((b - r) / delta) + 2.f;
  else hue = ((r - g) / delta) + 4.f;
  hue /= 6.f;
  if (hue < 0.f) hue += 1.f;
  return hue;
}
EffectResult pixelSorting(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  float low = std::clamp(floatOf(params, "threshold_low"), 0.f, 100.f) / 100.f;
  float high = std::clamp(floatOf(params, "threshold_high"), 0.f, 100.f) / 100.f;
  if (high < low) std::swap(low, high);
  const int minSpan = clampInt(intOf(params, "min_span_length"), 2, 256);
  const int maxSpan = std::clamp(intOf(params, "max_span_length"), minSpan, 512);
  const float probability = std::clamp(floatOf(params, "sort_probability"), 0.f, 100.f) / 100.f;
  const bool horizontal = choiceOf(params, "direction") == QLatin1String("horizontal");
  const bool useHue = choiceOf(params, "metric") == QLatin1String("hue");
  const int sortSeed = 3110 ^ seedOf(context);
  std::vector<QRgb> pixels(size_t(width * height));
  for (int y = 0; y < height; ++y) std::copy_n(reinterpret_cast<const QRgb *>(source.constScanLine(y)), width, pixels.data() + size_t(y * width));
  auto metricOf = [&](QRgb c) { return useHue ? hue01(c) : luminance01(c); };
  auto sortSegment = [&](int start, int length, int stride) {
    std::vector<QRgb> segment(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) segment[size_t(i)] = pixels[size_t(start + i * stride)];
    std::stable_sort(segment.begin(), segment.end(), [&](QRgb a, QRgb b) { return metricOf(a) < metricOf(b); });
    for (int i = 0; i < length; ++i) pixels[size_t(start + i * stride)] = segment[size_t(i)];
  };
  auto walk = [&](int outer, int innerCount, bool columns) {
    int cursorAxis = 0;
    while (cursorAxis < innerCount) {
      auto at = [&](int inner) { return columns ? pixels[size_t(inner * width + outer)] : pixels[size_t(outer * width + inner)]; };
      if (metricOf(at(cursorAxis)) < low || metricOf(at(cursorAxis)) > high) {
        ++cursorAxis;
        continue;
      }
      const int start = cursorAxis++;
      while (cursorAxis < innerCount && metricOf(at(cursorAxis)) >= low && metricOf(at(cursorAxis)) <= high) ++cursorAxis;
      if (cursorAxis - start < minSpan) continue;
      for (int cursor = start; cursor < cursorAxis;) {
        const int length = std::min(cursorAxis - cursor, maxSpan);
        if (length < minSpan) break;
        const float selector = columns ? hash01(outer, cursor, sortSeed ^ 7723) : hash01(cursor, outer, sortSeed ^ 1187);
        if (selector <= probability) sortSegment(columns ? cursor * width + outer : outer * width + cursor, length, columns ? width : 1);
        cursor += length;
      }
    }
  };
  if (horizontal)
    for (int y = 0; y < height; ++y) {
      if (context.cancelled()) return {{}, kCancelled};
      walk(y, width, false);
    }
  else
    for (int x = 0; x < width; ++x) {
      if (context.cancelled()) return {{}, kCancelled};
      walk(x, height, true);
    }
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    std::copy_n(pixels.data() + size_t(y * width), width, reinterpret_cast<QRgb *>(result.scanLine(y)));
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- stained glass --------------------------------------------------------------------------------
float glassHash(int x, int y, uint32_t seed) {
  uint32_t h = uint32_t(int32_t(x));
  h = (h * 374761393u) + (uint32_t(int32_t(y)) * 668265263u);
  h ^= seed;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return float(h & 0xFFFFFFu) / 16777215.f;
}
EffectResult stainedGlass(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int tileSize = clampInt(intOf(params, "tile_size"), 6, 120);
  const float irregularity = std::clamp(floatOf(params, "irregularity"), 0.f, 100.f) / 100.f;
  const float leadWidth = std::clamp(floatOf(params, "lead_width"), 0.f, 12.f);
  const float leadOpacity = std::clamp(floatOf(params, "lead_opacity"), 0.f, 100.f) / 100.f;
  const float colorBoost = std::clamp(floatOf(params, "color_boost"), 0.f, 100.f) / 100.f;
  const int width = source.width(), height = source.height(), right = width - 1, bottom = height - 1;
  const int gridW = int(std::ceil(float(width) / float(tileSize))) + 2, gridH = int(std::ceil(float(height) / float(tileSize))) + 2;
  const uint32_t baseSeed = uint32_t(int32_t(1337 ^ seedOf(context))) * 2246822519u;
  struct Cell { float x, y; QRgb color; };
  std::vector<Cell> grid(size_t(gridW * gridH));
  const Pixels src(source);
  const float jitter = float(tileSize) * 0.5f * irregularity;
  for (int gy = 0; gy < gridH; ++gy) {
    if (context.cancelled()) return {{}, kCancelled};
    for (int gx = 0; gx < gridW; ++gx) {
      const float baseX = (float(gx - 1) + 0.5f) * float(tileSize), baseY = (float(gy - 1) + 0.5f) * float(tileSize);
      const float cx = std::clamp(baseX + (glassHash(gx, gy, baseSeed ^ 0x9E3779B9u) * 2.f - 1.f) * jitter, 0.f, float(right));
      const float cy = std::clamp(baseY + (glassHash(gx, gy, baseSeed ^ 0x85EBCA6Bu) * 2.f - 1.f) * jitter, 0.f, float(bottom));
      QRgb sampled = src.at(clampInt(iround(cx), 0, right), clampInt(iround(cy), 0, bottom));
      if (colorBoost > 0.f) {
        const float r = float(qRed(sampled)), g = float(qGreen(sampled)), b = float(qBlue(sampled));
        const float gray = (r + g + b) / 3.f, factor = 1.f + colorBoost;
        sampled = qRgba(clampInt(iround(gray + (r - gray) * factor), 0, 255), clampInt(iround(gray + (g - gray) * factor), 0, 255),
                        clampInt(iround(gray + (b - gray) * factor), 0, 255), qAlpha(sampled));
      }
      grid[size_t(gy * gridW + gx)] = {cx, cy, sampled};
    }
  }
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    const int gy = (y / tileSize) + 1;
    for (int x = 0; x < width; ++x) {
      const int gx = (x / tileSize) + 1;
      float nearest = 1e30f, second = 1e30f;
      QRgb nearestColor = src.at(x, y);
      for (int ny = gy - 1; ny <= gy + 1; ++ny)
        for (int nx = gx - 1; nx <= gx + 1; ++nx) {
          const Cell &cell = grid[size_t(ny * gridW + nx)];
          const float dx = float(x) - cell.x, dy = float(y) - cell.y, d2 = dx * dx + dy * dy;
          if (d2 < nearest) {
            second = nearest;
            nearest = d2;
            nearestColor = cell.color;
          } else if (d2 < second) second = d2;
        }
      QRgb color = nearestColor;
      if (leadWidth > 0.f && second < 1e29f) {
        const float edgeDistance = (std::sqrt(second) - std::sqrt(nearest)) * 0.5f;
        if (edgeDistance < leadWidth) {
          const float keep = 1.f - std::clamp(leadOpacity * (1.f - edgeDistance / leadWidth), 0.f, 1.f);
          color = qRgba(clampInt(iround(float(qRed(color)) * keep), 0, 255), clampInt(iround(float(qGreen(color)) * keep), 0, 255),
                        clampInt(iround(float(qBlue(color)) * keep), 0, 255), qAlpha(color));
        }
      }
      output[x] = color;
    }
  });
  if (!done) return {{}, kCancelled};
  return {result, {}};
}

// ---- liquid glass ---------------------------------------------------------------------------------
EffectResult liquidGlass(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float distortion = std::clamp(floatOf(params, "distortion"), 0.f, 35.f);
  const float refraction = std::clamp(floatOf(params, "refraction"), 0.f, 100.f) / 100.f;
  const int chroma = clampInt(intOf(params, "chroma_shift"), 0, 8);
  const float gloss = std::clamp(floatOf(params, "gloss"), 0.f, 100.f) / 100.f;
  const float flowScale = std::clamp(floatOf(params, "flow_scale"), 40.f, 220.f) / 100.f;
  if (distortion <= 0.f && refraction <= 0.f && chroma <= 0 && gloss <= 0.f) return identityCopy(source, context);
  const int seed = 9421 ^ seedOf(context);
  const int width = source.width(), height = source.height();
  const Pixels src(source);
  const float freqX = 0.072f / flowScale, freqY = 0.061f / flowScale;
  return mapPixels(source, context, [&](int x, int y, QRgb) {
    const int yTop = y > 0 ? y - 1 : 0, yBottom = y < height - 1 ? y + 1 : height - 1;
    const int xLeft = x > 0 ? x - 1 : 0, xRight = x < width - 1 ? x + 1 : width - 1;
    const float cellNoise = hash01(x / 18, y / 18, seed) * 2.f - 1.f;
    const float wave1 = std::sin(float(x) * freqX + float(y) * (freqY * 0.45f) + cellNoise * 2.4f + float(seed) * 0.001f);
    const float wave2 = std::cos(float(x) * (freqX * 0.58f) - float(y) * (freqY * 1.12f) + cellNoise * 1.7f);
    const float wave3 = std::sin(float(x + y) * (freqX * 0.62f) + cellNoise * 3.1f);
    float offsetX = distortion * (0.52f * wave1 + 0.26f * wave3);
    float offsetY = distortion * (0.46f * wave2 - 0.22f * wave3);
    if (refraction > 0.f) {
      const float gradX = luminance01(src.at(xRight, y)) - luminance01(src.at(xLeft, y));
      const float gradY = luminance01(src.at(x, yBottom)) - luminance01(src.at(x, yTop));
      const float edge = std::sqrt(gradX * gradX + gradY * gradY);
      const float edgeBoost = 1.f + edge * 3.2f * refraction;
      offsetX = offsetX * edgeBoost + gradX * refraction * distortion * 3.f;
      offsetY = offsetY * edgeBoost + gradY * refraction * distortion * 3.f;
    }
    const float sampleX = float(x) + offsetX, sampleY = float(y) + offsetY;
    float r, g, b;
    int a;
    if (chroma > 0) {
      const float shift = float(chroma) * 0.75f;
      const QRgb sampleR = bilinear(src, sampleX + shift, sampleY - shift * 0.15f);
      const QRgb sampleG = bilinear(src, sampleX, sampleY);
      const QRgb sampleB = bilinear(src, sampleX - shift, sampleY + shift * 0.15f);
      r = float(qRed(sampleR));
      g = float(qGreen(sampleG));
      b = float(qBlue(sampleB));
      a = qAlpha(sampleG);
    } else {
      const QRgb sample = bilinear(src, sampleX, sampleY);
      r = float(qRed(sample));
      g = float(qGreen(sample));
      b = float(qBlue(sample));
      a = qAlpha(sample);
    }
    if (refraction > 0.f) {
      const QRgb n1 = bilinear(src, sampleX + 1.f, sampleY), n2 = bilinear(src, sampleX - 1.f, sampleY);
      const QRgb n3 = bilinear(src, sampleX, sampleY + 1.f), n4 = bilinear(src, sampleX, sampleY - 1.f);
      const float blurMix = 0.06f + 0.18f * refraction;
      r = lerp(r, (float(qRed(n1)) + float(qRed(n2)) + float(qRed(n3)) + float(qRed(n4))) * 0.25f, blurMix);
      g = lerp(g, (float(qGreen(n1)) + float(qGreen(n2)) + float(qGreen(n3)) + float(qGreen(n4))) * 0.25f, blurMix);
      b = lerp(b, (float(qBlue(n1)) + float(qBlue(n2)) + float(qBlue(n3)) + float(qBlue(n4))) * 0.25f, blurMix);
    }
    const float ridge = std::fabs(wave1 - wave2), micro = std::fabs(wave3);
    const float spec = std::pow(std::max(0.f, 1.f - ridge * 1.35f), 6.f);
    const float streak = std::pow(std::max(0.f, 1.f - micro * 1.55f), 4.f);
    const float lum = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.f;
    const float highlight = gloss * (spec * 0.62f + streak * 0.24f + std::pow(lum, 2.8f) * 0.14f);
    r += highlight * 88.f;
    g += highlight * 102.f;
    b += highlight * 126.f;
    const float tint = (0.04f + 0.10f * refraction) * (0.4f + 0.6f * gloss);
    r = lerp(r, r + 6.f, tint);
    g = lerp(g, g + 12.f, tint);
    b = lerp(b, b + 22.f, tint);
    return qRgba(toByte(r), toByte(g), toByte(b), a);
  });
}

float flakeLayer(int x, int y, float cellSize, float radius, float density, float windOffset, int seed) {
  const float px = float(x) + windOffset;
  const int cellX = int(std::floor(px / cellSize)), cellY = int(std::floor(float(y) / cellSize));
  float strongest = 0.f;
  for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
      const int nx = cellX + ox, ny = cellY + oy;
      if (hash01(nx, ny, seed) > density) continue;
      const float centerX = (float(nx) + hash01(nx, ny, seed ^ 1777)) * cellSize;
      const float centerY = (float(ny) + hash01(nx, ny, seed ^ 3907)) * cellSize;
      const float dx = px - centerX, dy = float(y) - centerY, distSq = dx * dx + dy * dy;
      const float flakeRadius = radius * (0.6f + hash01(nx, ny, seed ^ 727) * 0.9f);
      if (distSq >= flakeRadius * flakeRadius) continue;
      const float alpha = 1.f - smoothStep(flakeRadius * 0.3f, flakeRadius, std::sqrt(distSq));
      strongest = std::max(strongest, alpha);
    }
  return strongest;
}
EffectResult snowfall(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float snowAmount = std::clamp(floatOf(params, "snow_amount"), 0.f, 100.f) / 100.f;
  const float flakeSize = std::clamp(floatOf(params, "flake_size"), 1.f, 14.f);
  const float wind = std::clamp(floatOf(params, "wind"), -100.f, 100.f) / 100.f;
  const float fogAmount = std::clamp(floatOf(params, "fog_amount"), 0.f, 100.f) / 100.f;
  const float fogHeight = std::clamp(floatOf(params, "fog_height"), 10.f, 100.f) / 100.f;
  if (snowAmount <= 0.0001f && fogAmount <= 0.0001f) return identityCopy(source, context);
  const int seed = positiveSeed(context);
  const int height = source.height(), bottom = height - 1;
  const float invHeight = 1.f / float(std::max(1, bottom));
  const float layerOneCell = std::clamp(22.f - flakeSize * 0.9f, 8.f, 24.f);
  const float layerTwoCell = std::max(6.f, layerOneCell * 0.58f);
  const float layerOneRadius = 0.9f + flakeSize * 0.22f, layerTwoRadius = 0.55f + flakeSize * 0.14f;
  const float layerOneDensity = 0.015f + snowAmount * 0.22f, layerTwoDensity = 0.04f + snowAmount * 0.30f;
  const float fogStart = 1.f - fogHeight;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)), g = float(qGreen(src)), b = float(qBlue(src));
    const float normY = float(y) * invHeight, depth = 1.f - normY, perspective = 0.55f + normY * 0.45f, windOffset = wind * float(y) * 0.38f;
    if (fogAmount > 0.0001f) {
      const float fogMask = smoothStep(fogStart, 1.f, depth);
      if (fogMask > 0.0001f) {
        const float fogNoise = hash01(int(float(x) * 0.06f), int(float(y) * 0.06f), seed ^ 191) * 0.65f +
                               hash01(int(float(x) * 0.018f), int(float(y) * 0.018f), seed ^ 379) * 0.35f;
        const float fog = fogAmount * fogMask * (0.75f + 0.25f * fogNoise) * (0.62f + 0.38f * depth);
        r = lerp(r, 226.f, fog);
        g = lerp(g, 236.f, fog);
        b = lerp(b, 248.f, fog);
      }
    }
    if (snowAmount > 0.0001f) {
      const float layerOne = flakeLayer(x, y, layerOneCell, layerOneRadius, layerOneDensity, windOffset, seed ^ 911);
      const float layerTwo = flakeLayer(x, y, layerTwoCell, layerTwoRadius, layerTwoDensity, windOffset * 1.3f, seed ^ 3001);
      const float sparkle = std::pow(hash01(x, y, seed ^ 4451), 20.f) * snowAmount * 0.9f;
      const float snow = std::clamp((layerOne * 0.72f + layerTwo * 0.48f + sparkle) * perspective, 0.f, 1.f);
      if (snow > 0.0001f) {
        r = lerp(r, 255.f, snow);
        g = lerp(g, 255.f, snow);
        b = lerp(b, 255.f, snow);
      }
    }
    return qRgba(toByte(r), toByte(g), toByte(b), toByte(float(qAlpha(src))));
  });
}

float splatMask(int x, int y, float cellSize, float radius, float density, int seed) {
  const int cellX = int(std::floor(float(x) / cellSize)), cellY = int(std::floor(float(y) / cellSize));
  float best = 0.f;
  for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
      const int nx = cellX + ox, ny = cellY + oy;
      if (hash01(nx, ny, seed) > density) continue;
      const float centerX = (float(nx) + hash01(nx, ny, seed ^ 177)) * cellSize;
      const float centerY = (float(ny) + hash01(nx, ny, seed ^ 571)) * cellSize;
      const float dist = std::sqrt(float((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY)));
      const float localRadius = radius * (0.42f + hash01(nx, ny, seed ^ 887) * 0.95f);
      best = std::max(best, 1.f - smoothStep(localRadius * 0.28f, localRadius, dist));
    }
  return best;
}
float inkDrip(int x, int y, int height, float cellWidth, float probability, float dripLength, float spread, int seed) {
  const int column = int(std::floor(float(x) / cellWidth));
  float strongest = 0.f;
  for (int c = column - 1; c <= column + 1; ++c) {
    if (hash01(c, 0, seed) > probability) continue;
    const float centerX = (float(c) + hash01(c, 1, seed ^ 211)) * cellWidth;
    const float startY = hash01(c, 2, seed ^ 419) * float(height) * 0.75f;
    const float len = (0.08f + dripLength * 0.85f) * (0.4f + hash01(c, 3, seed ^ 677) * 0.9f) * float(height);
    const float thickness = (0.9f + spread * 3.2f) * (0.65f + hash01(c, 4, seed ^ 991) * 0.95f);
    const float dy = float(y) - startY;
    if (dy < 0.f || dy > len) continue;
    const float line = 1.f - smoothStep(thickness * 0.45f, thickness, std::fabs(float(x) - centerX));
    if (line <= 0.f) continue;
    float drip = line * (0.55f + 0.45f * (1.f - smoothStep(0.f, len, dy)));
    const float tipDist = std::sqrt((float(x) - centerX) * (float(x) - centerX) + (float(y) - (startY + len)) * (float(y) - (startY + len)) * 0.55f);
    drip = std::max(drip, (1.f - smoothStep(thickness * 0.2f, thickness * 1.6f, tipDist)) * 0.9f);
    strongest = std::max(strongest, drip);
  }
  return strongest;
}
EffectResult inkSplatter(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float inkAmount = std::clamp(floatOf(params, "ink_amount"), 0.f, 100.f) / 100.f;
  const float dripLength = std::clamp(floatOf(params, "drip_length"), 0.f, 100.f) / 100.f;
  const float spread = std::clamp(floatOf(params, "spread"), 0.f, 100.f) / 100.f;
  const float paperFade = std::clamp(floatOf(params, "paper_fade"), 0.f, 100.f) / 100.f;
  if (inkAmount <= 0.0001f && paperFade <= 0.0001f) return identityCopy(source, context);
  const int seed = positiveSeed(context), height = source.height();
  const float splatCell = 14.f + (1.f - spread) * 16.f, splatRadius = 3.6f + spread * 11.5f, splatDensity = 0.04f + inkAmount * 0.25f;
  const float dripCell = 12.f + (1.f - spread) * 24.f, dripProbability = 0.12f + inkAmount * 0.42f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)), g = float(qGreen(src)), b = float(qBlue(src));
    if (paperFade > 0.0001f) {
      const float gray = luminance(src);
      const float paperNoise = (hash01(x, y, seed ^ 131) * 2.f - 1.f) * paperFade * 8.f;
      const float paperTarget = gray + 9.f * paperFade + paperNoise, paperMix = paperFade * 0.48f;
      r = lerp(r, paperTarget, paperMix);
      g = lerp(g, paperTarget, paperMix);
      b = lerp(b, paperTarget + 4.f, paperMix);
    }
    const float splatter = splatMask(x, y, splatCell, splatRadius, splatDensity, seed ^ 991);
    const float drips = inkDrip(x, y, height, dripCell, dripProbability, dripLength, spread, seed ^ 3701);
    const float speckle = std::pow(hash01(x, y, seed ^ 5239), 26.f) * inkAmount * 0.85f;
    const float inkMask = std::clamp(splatter * 0.9f + drips * 0.85f + speckle, 0.f, 1.f) * inkAmount;
    if (inkMask > 0.0001f) {
      const float bleed = std::max(0.f, splatter - 0.25f) * 0.3f;
      r = lerp(r, 11.f + bleed * 10.f, inkMask);
      g = lerp(g, 11.f + bleed * 8.f, inkMask);
      b = lerp(b, 15.f + bleed * 12.f, inkMask);
    }
    return qRgba(toByte(r), toByte(g), toByte(b), toByte(float(qAlpha(src))));
  });
}

EffectResult vintagePrint(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float sepia = std::clamp(floatOf(params, "sepia_amount"), 0.f, 100.f) / 100.f;
  const float fade = std::clamp(floatOf(params, "fade_amount"), 0.f, 100.f) / 100.f;
  const float grain = std::clamp(floatOf(params, "grain_amount"), 0.f, 100.f) / 100.f;
  const float scratches = std::clamp(floatOf(params, "scratch_amount"), 0.f, 100.f) / 100.f;
  const float dust = std::clamp(floatOf(params, "dust_amount"), 0.f, 100.f) / 100.f;
  if (sepia <= 0.f && fade <= 0.f && grain <= 0.f && scratches <= 0.f && dust <= 0.f) return identityCopy(source, context);
  const int seed = 1947 ^ seedOf(context);
  const int width = source.width(), height = source.height();
  const float cx = float(width - 1) * 0.5f, cy = float(height - 1) * 0.5f;
  const float invCx = cx > 0.f ? 1.f / cx : 1.f, invCy = cy > 0.f ? 1.f / cy : 1.f;
  const float dustCell = std::max(8.f, 26.f - dust * 18.f);
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)) / 255.f, g = float(qGreen(src)) / 255.f, b = float(qBlue(src)) / 255.f;
    const float sr = r * 0.393f + g * 0.769f + b * 0.189f, sg = r * 0.349f + g * 0.686f + b * 0.168f, sb = r * 0.272f + g * 0.534f + b * 0.131f;
    r = lerp(r, sr, sepia);
    g = lerp(g, sg, sepia);
    b = lerp(b, sb, sepia);
    const float contrast = 1.f - fade * 0.35f, blackLift = fade * 0.045f;
    r = (r - 0.5f) * contrast + 0.5f + blackLift;
    g = (g - 0.5f) * contrast + 0.5f + blackLift;
    b = (b - 0.5f) * contrast + 0.5f + blackLift;
    const float paperMix = 0.08f + fade * 0.10f;
    r = lerp(r, 0.93f, paperMix);
    g = lerp(g, 0.86f, paperMix);
    b = lerp(b, 0.74f, paperMix);
    if (grain > 0.f) {
      const float amp = grain * 0.12f;
      r += (hash01(x, y, seed) * 2.f - 1.f) * amp;
      g += (hash01(x, y, seed ^ 193) * 2.f - 1.f) * amp;
      b += (hash01(x, y, seed ^ 761) * 2.f - 1.f) * amp;
    }
    if (scratches > 0.f) {
      const int column = x / 2;
      const float scratchChance = hash01(column, 0, seed ^ 71);
      if (scratchChance > 0.985f - scratches * 0.12f) {
        const float center = float(column * 2) + (hash01(column, 1, seed ^ 97) - 0.5f) * 2.f +
                             std::sin(float(y) * 0.03f + scratchChance * 12.f) * (0.5f + scratches * 2.f);
        const float widthPx = 0.5f + hash01(column, 2, seed ^ 131) * 1.8f;
        const float lineIntensity = (1.f - smoothStep(0.1f, 1.f, std::fabs(float(x) - center) / std::max(widthPx, 0.001f))) * scratches;
        if (hash01(column, 3, seed ^ 173) > 0.55f) {
          r += lineIntensity * 0.25f;
          g += lineIntensity * 0.23f;
          b += lineIntensity * 0.20f;
        } else {
          r -= lineIntensity * 0.22f;
          g -= lineIntensity * 0.22f;
          b -= lineIntensity * 0.22f;
        }
      }
    }
    if (dust > 0.f) {
      const int cellX = int(float(x) / dustCell), cellY = int(float(y) / dustCell);
      const float spotChance = hash01(cellX, cellY, seed ^ 521);
      if (spotChance > 0.988f - dust * 0.23f) {
        const float spotX = (float(cellX) + hash01(cellX, cellY, seed ^ 577)) * dustCell;
        const float spotY = (float(cellY) + hash01(cellX, cellY, seed ^ 653)) * dustCell;
        const float radius = dustCell * (0.10f + 0.24f * hash01(cellX, cellY, seed ^ 997));
        const float dist = std::sqrt((float(x) - spotX) * (float(x) - spotX) + (float(y) - spotY) * (float(y) - spotY));
        const float spotStrength = (1.f - smoothStep(0.15f, 1.f, dist / std::max(radius, 0.001f))) * dust;
        if (hash01(cellX, cellY, seed ^ 733) > 0.60f) {
          r += spotStrength * 0.18f;
          g += spotStrength * 0.16f;
          b += spotStrength * 0.14f;
        } else {
          r -= spotStrength * 0.20f;
          g -= spotStrength * 0.19f;
          b -= spotStrength * 0.18f;
        }
      }
    }
    const float dxNorm = (float(x) - cx) * invCx, dyNorm = (float(y) - cy) * invCy;
    const float edge = smoothStep(0.62f, 1.f, std::sqrt(dxNorm * dxNorm + dyNorm * dyNorm) * 0.70710678f);
    const float edgeBurn = edge * (0.10f + fade * 0.32f + scratches * 0.18f);
    r *= 1.f - edgeBurn;
    g *= 1.f - edgeBurn;
    b *= 1.f - edgeBurn;
    return qRgba(toByte(r * 255.f), toByte(g * 255.f), toByte(b * 255.f), qAlpha(src));
  });
}

void accumulatePuff(float x, float y, float centerX, float centerY, float radius, float &field, float &light, float &shadow) {
  const float nx = (x - centerX) / (radius * 1.18f);
  const float dy = y - centerY;
  const float ny = dy < 0.f ? dy / (radius * 0.96f) : dy / (radius * 0.70f);
  const float dist = std::sqrt(nx * nx + ny * ny);
  if (dist >= 1.f) return;
  const float puff = 1.f - smoothStep(0.10f, 1.f, dist);
  const float localLight = clamp01(0.72f - nx * 0.34f - ny * 0.58f);
  const float underside = clamp01(ny * 0.90f + 0.12f);
  field = 1.f - (1.f - field) * (1.f - puff);
  light = std::max(light, puff * localLight);
  shadow = std::max(shadow, puff * (1.f - localLight) * underside);
}
void sampleCloudLayer(float x, float y, float cellSize, float spawnThreshold, int seed, float &field, float &light, float &shadow) {
  field = light = shadow = 0.f;
  const int cellX = int(std::floor(x / cellSize)), cellY = int(std::floor(y / cellSize));
  for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
      const int nx = cellX + ox, ny = cellY + oy;
      if (hash01(nx, ny, seed) < spawnThreshold) continue;
      const float centerX = (float(nx) + 0.18f + hash01(nx, ny, seed ^ 0x0713) * 0.64f) * cellSize;
      const float centerY = (float(ny) + 0.22f + hash01(nx, ny, seed ^ 0x1731) * 0.52f) * cellSize;
      const float radius = cellSize * (0.28f + hash01(nx, ny, seed ^ 0x2A51) * 0.20f);
      accumulatePuff(x, y, centerX, centerY, radius, field, light, shadow);
      const float satelliteChance = hash01(nx, ny, seed ^ 0x3C17);
      if (satelliteChance > 0.32f) {
        const float angle = hash01(nx, ny, seed ^ 0x49D3) * kTau;
        const float offset = radius * (0.35f + hash01(nx, ny, seed ^ 0x5123) * 0.40f);
        accumulatePuff(x, y, centerX + std::cos(angle) * offset, centerY + std::sin(angle) * offset * 0.72f,
                       radius * (0.42f + hash01(nx, ny, seed ^ 0x6891) * 0.28f), field, light, shadow);
      }
    }
}
EffectResult clouds(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float coverage = std::clamp(floatOf(params, "coverage"), 0.f, 100.f) / 100.f;
  if (coverage <= 0.0001f) return identityCopy(source, context);
  const float scale = std::clamp(floatOf(params, "scale"), 0.f, 100.f) / 100.f;
  const float heightBias = std::clamp(floatOf(params, "heightBias"), 0.f, 100.f) / 100.f;
  const float softness = std::clamp(floatOf(params, "softness"), 0.f, 100.f) / 100.f;
  const float sunlight = std::clamp(floatOf(params, "sunlight"), 0.f, 100.f) / 100.f;
  const int width = source.width(), height = source.height();
  const float cellSize = 36.f + scale * 140.f, detailCell = std::max(18.f, cellSize * 0.52f);
  const float warpFreq = 0.0045f + (1.f - scale) * 0.012f, warpAmount = 10.f + scale * 40.f;
  const float erosionFreq = 0.010f + (1.f - scale) * 0.020f, spawnThreshold = 0.82f - coverage * 0.45f;
  const float invWidth = width > 1 ? 1.f / float(width - 1) : 0.f, invHeight = height > 1 ? 1.f / float(height - 1) : 0.f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float u = float(x) * invWidth, v = float(y) * invHeight;
    const float verticalMask = 1.f - smoothStep(0.20f + heightBias * 0.55f, 1.f, v);
    const float warpX = (fractalNoise(float(x) * warpFreq + 4.7f, float(y) * warpFreq * 0.92f - 2.1f, 3, 2.02f, 0.56f, 1411) * 2.f - 1.f) * warpAmount;
    const float warpY = (fractalNoise(float(x) * warpFreq * 0.88f - 6.4f, float(y) * warpFreq + 3.6f, 3, 2.10f, 0.54f, 2311) * 2.f - 1.f) * warpAmount * 0.65f;
    const float sampleX = float(x) + warpX, sampleY = float(y) + warpY;
    float macroField, macroLight, macroShadow, detailField, detailLight, detailShadow;
    sampleCloudLayer(sampleX, sampleY, cellSize, spawnThreshold, 3571, macroField, macroLight, macroShadow);
    sampleCloudLayer(sampleX + cellSize * 0.24f, sampleY - cellSize * 0.15f, detailCell, spawnThreshold + 0.08f, 5171, detailField, detailLight, detailShadow);
    const float field = 1.f - (1.f - macroField) * (1.f - detailField * 0.72f);
    const float breakupNoise = fractalNoise(sampleX * erosionFreq - 3.2f, sampleY * erosionFreq + 5.9f, 3, 2.00f, 0.56f, 6211);
    const float breakup = 1.f - std::fabs(breakupNoise * 2.f - 1.f);
    const float curls = fractalNoise(sampleX * erosionFreq * 2.2f + 11.3f, sampleY * erosionFreq * 1.9f - 7.6f, 2, 2.12f, 0.58f, 7351);
    const float bankWave = 0.5f + 0.5f * std::sin(u * 5.6f + v * 1.7f + 0.8f);
    float structure = clamp01(field * (0.72f + 0.22f * breakup + 0.10f * curls) + bankWave * 0.07f);
    float mask = smoothStep(0.30f - coverage * 0.18f - verticalMask * 0.08f, 0.84f, structure * (0.70f + 0.30f * verticalMask));
    mask = std::pow(mask, 1.45f - softness * 0.95f);
    const float cloudLight = std::max(macroLight, detailLight * 0.88f), cloudShadow = std::max(macroShadow, detailShadow * 0.92f);
    const float sunlightBias = clamp01(0.68f + ((0.78f - u) * 0.42f + (0.40f - v) * 0.74f));
    const float highlight = sunlight * mask * (0.18f + 0.34f * cloudLight + 0.18f * sunlightBias);
    const float shade = mask * ((0.08f + 0.22f * cloudShadow) * (0.90f - sunlight * 0.20f));
    const float body = 0.79f + 0.12f * structure + 0.05f * breakup;
    const float cloudR = clamp01(body - shade + highlight * 1.06f), cloudG = clamp01(body + 0.03f - shade * 0.94f + highlight),
                cloudB = clamp01(body + 0.08f - shade * 0.84f + highlight * 0.82f);
    const float ambient = coverage * verticalMask * (0.006f + 0.016f * structure);
    const float sr = float(qRed(src)) / 255.f, sg = float(qGreen(src)) / 255.f, sb = float(qBlue(src)) / 255.f;
    const float blend = mask * (0.52f + coverage * 0.34f);
    return qRgba(toByte(lerp(lerp(sr, 0.84f, ambient), cloudR, blend) * 255.f), toByte(lerp(lerp(sg, 0.89f, ambient), cloudG, blend) * 255.f),
                 toByte(lerp(lerp(sb, 0.95f, ambient), cloudB, blend) * 255.f), qAlpha(src));
  });
}

EffectResult nebula(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float intensity = std::clamp(floatOf(params, "intensity"), 0.f, 100.f) / 100.f;
  if (intensity <= 0.f) return identityCopy(source, context);
  const float scale = std::clamp(floatOf(params, "scale"), 0.f, 100.f) / 100.f;
  const float hueShift = std::clamp(floatOf(params, "hue_shift"), -180.f, 180.f);
  const float starDensity = std::clamp(floatOf(params, "star_density"), 0.f, 100.f) / 100.f;
  const float starSize = std::clamp(floatOf(params, "star_size"), 0.f, 200.f) / 200.f;
  const float twinkle = std::clamp(floatOf(params, "twinkle"), 0.f, 100.f) / 100.f;
  const float vignette = std::clamp(floatOf(params, "vignette_strength"), 0.f, 100.f) / 100.f;
  const int seed = 1337 ^ seedOf(context);
  const int width = source.width(), height = source.height();
  const float cx = hash01(seed, 1, seed ^ 31) * float(width - 1), cy = hash01(seed, 2, seed ^ 47) * float(height - 1);
  const float maxDist = std::max(1.f, std::sqrt(float(width - 1) * float(width - 1) + float(height - 1) * float(height - 1)));
  float nebR, nebG, nebB;
  hsvToRgb((hueShift + 180.f) / 360.f, 0.85f, 1.f, nebR, nebG, nebB);
  const int cell = clampInt(iround(10.f - starSize * 6.f), 3, 16);
  const float starThreshold = 1.f - starDensity, radiusBase = 0.18f + 0.55f * starSize, radiusFeather = 0.05f + 0.10f * starSize;
  const float freq = 0.25f + scale * 2.25f, phase = float(seed % 1000) * 0.013f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    const float nx = (float(x) - cx) / float(width), ny = (float(y) - cy) / float(height);
    const float dist = std::sqrt(nx * nx + ny * ny);
    const float u = float(x) / float(std::max(1, width - 1)), v = float(y) / float(std::max(1, height - 1));
    const float waveA = std::sin(u * freq * 8.f + v * freq * 5.f + phase);
    const float waveB = std::sin(u * freq * 13.f - v * freq * 9.f + phase * 1.37f);
    const float waveC = std::sin((u + v) * freq * 7.f + phase * 0.77f);
    const float nebulaNoise = ((waveA * 0.45f + waveB * 0.35f + waveC * 0.20f) * 0.5f) + 0.5f;
    const float falloff = std::exp(-(dist * dist) * (2.2f + scale * 3.2f));
    const float nebulaStrength = intensity * (0.10f + 0.90f * nebulaNoise) * falloff;
    const int cellX = x / cell, cellY = y / cell;
    float starMask = 0.f;
    if (hash01(cellX, cellY, seed ^ 0x3A17) > starThreshold) {
      const float starCenterX = float(cellX * cell) + hash01(cellX, cellY, seed ^ 0xBEEF) * float(cell);
      const float starCenterY = float(cellY * cell) + hash01(cellX, cellY, seed ^ 0xCAFE) * float(cell);
      const float dx = float(x) - starCenterX, dy = float(y) - starCenterY;
      const float twinkleSeed = hash01(cellX, cellY, seed ^ 0x51A7);
      const float twinkleWave = 0.5f + 0.5f * std::sin(twinkleSeed * kPi * 10.f + phase + float(cellX) * 0.91f + float(cellY) * 1.37f);
      const float twinkleRadius = lerp(1.f, 0.82f + twinkleWave * 0.70f, twinkle);
      const float twinkleIntensity = lerp(1.f, 0.35f + twinkleWave * 1.65f, twinkle);
      float d01 = std::sqrt(dx * dx + dy * dy) / (float(cell) * 0.5f);
      d01 /= std::max(0.35f, twinkleRadius);
      starMask = 1.f - smoothStep(radiusBase, radiusBase + radiusFeather * lerp(1.f, 1.35f, twinkle), d01);
      if (twinkle > 0.001f) {
        const float streakScale = 0.08f + starSize * 0.20f;
        const float streakX = 1.f - smoothStep(0.2f, 1.45f, std::fabs(dx) / std::max(0.0001f, float(cell) * streakScale));
        const float streakY = 1.f - smoothStep(0.2f, 1.45f, std::fabs(dy) / std::max(0.0001f, float(cell) * streakScale));
        const float halo = 1.f - smoothStep(radiusBase * 0.7f, radiusBase + 1.25f + starSize, d01);
        starMask = starMask * twinkleIntensity + std::max(streakX, streakY) * halo * twinkle * std::max(0.f, twinkleIntensity - 1.f) * 0.45f;
      }
    }
    const float starStrength = intensity * starMask * (0.25f + 0.75f * starSize);
    const float rFromCenter = std::sqrt((float(x) - cx) * (float(x) - cx) + (float(y) - cy) * (float(y) - cy)) / maxDist;
    const float vign = clamp01(1.f - vignette * rFromCenter * rFromCenter);
    const float add = std::min(1.f, nebulaStrength + starStrength), remaining = 1.f - add;
    float outR = (float(qRed(src)) / 255.f * remaining + nebR * nebulaStrength + nebR * starStrength) * vign;
    float outG = (float(qGreen(src)) / 255.f * remaining + nebG * nebulaStrength + nebG * starStrength) * vign;
    float outB = (float(qBlue(src)) / 255.f * remaining + nebB * nebulaStrength + nebB * starStrength) * vign;
    return qRgba(toByte(outR * 255.f), toByte(outG * 255.f), toByte(outB * 255.f), qAlpha(src));
  });
}

float sampleDroplet(float x, float y, float centerX, float centerY, float radius, int seed) {
  const float dist = std::sqrt((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));
  const float irregular = radius * (0.78f + hash01(int(centerX * 0.25f), int(centerY * 0.25f), seed) * 0.34f);
  return 1.f - smoothStep(irregular * 0.22f, irregular, dist);
}
float splashMask(int x, int y, float cellSize, float radius, float density, int seed) {
  const int cellX = int(std::floor(float(x) / cellSize)), cellY = int(std::floor(float(y) / cellSize));
  float strongest = 0.f;
  for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
      const int nx = cellX + ox, ny = cellY + oy;
      if (hash01(nx, ny, seed) > density) continue;
      const float centerX = (float(nx) + hash01(nx, ny, seed ^ 157)) * cellSize;
      const float centerY = (float(ny) + hash01(nx, ny, seed ^ 389)) * cellSize;
      const float localRadius = radius * (0.48f + hash01(nx, ny, seed ^ 701) * 0.92f);
      strongest = std::max(strongest, sampleDroplet(float(x), float(y), centerX, centerY, localRadius, seed ^ 911));
      for (int i = 0; i < 3; ++i) {
        const float satelliteChance = hash01(nx * 17 + i, ny * 19 - i, seed ^ 1237);
        if (satelliteChance > 0.82f) continue;
        const float angle = hash01(nx * 23 + i, ny * 29 + i, seed ^ 1601) * kTau;
        const float orbit = localRadius * (0.32f + hash01(nx * 31 + i, ny * 37 + i, seed ^ 1907) * 1.12f);
        strongest = std::max(strongest, sampleDroplet(float(x), float(y), centerX + std::cos(angle) * orbit,
                                                     centerY + std::sin(angle) * orbit * (0.72f + satelliteChance * 0.55f),
                                                     localRadius * (0.16f + satelliteChance * 0.32f), seed ^ (2401 + i)));
      }
    }
  return strongest;
}
float dropletField(int x, int y, float cellSize, float radius, float density, int seed) {
  const int cellX = int(std::floor(float(x) / cellSize)), cellY = int(std::floor(float(y) / cellSize));
  float strongest = 0.f;
  for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
      const int nx = cellX + ox, ny = cellY + oy;
      if (hash01(nx, ny, seed) > density) continue;
      const float centerX = (float(nx) + hash01(nx, ny, seed ^ 223)) * cellSize;
      const float centerY = (float(ny) + hash01(nx, ny, seed ^ 521)) * cellSize;
      strongest = std::max(strongest, sampleDroplet(float(x), float(y), centerX, centerY, radius * (0.65f + hash01(nx, ny, seed ^ 883) * 0.9f), seed ^ 1291));
    }
  return strongest;
}
float bloodDrip(int x, int y, int height, float cellWidth, float probability, float dripLength, float spread, int seed) {
  const int column = int(std::floor(float(x) / cellWidth));
  float strongest = 0.f;
  for (int c = column - 2; c <= column + 2; ++c) {
    if (hash01(c, 0, seed) > probability) continue;
    const float baseX = (float(c) + hash01(c, 1, seed ^ 191)) * cellWidth;
    const float startY = hash01(c, 2, seed ^ 487) * float(height) * 0.58f;
    const float length = (0.12f + dripLength * 0.9f) * (0.36f + hash01(c, 3, seed ^ 761) * 0.88f) * float(height);
    const float thickness = (1.15f + spread * 3.8f) * (0.68f + hash01(c, 4, seed ^ 1033) * 0.85f);
    const float dy = float(y) - startY;
    if (dy < 0.f || dy > length) continue;
    const float sway = (hash01(c, 5, seed ^ 1327) * 2.f - 1.f) * spread * 5.5f;
    const float frequency = 0.012f + hash01(c, 6, seed ^ 1607) * 0.035f;
    const float phase = hash01(c, 7, seed ^ 1931) * kTau;
    const float laneX = baseX + std::sin(dy * frequency + phase) * sway + dy * sway * 0.018f;
    const float line = 1.f - smoothStep(thickness * 0.45f, thickness, std::fabs(float(x) - laneX));
    if (line <= 0.f) continue;
    const float drip = line * (0.48f + 0.52f * (1.f - smoothStep(0.f, length, dy)));
    const float tipDist = std::sqrt((float(x) - laneX) * (float(x) - laneX) + (float(y) - (startY + length)) * (float(y) - (startY + length)) * 0.52f);
    const float tip = 1.f - smoothStep(thickness * 0.24f, thickness * 1.75f, tipDist);
    const float shoulderDist = std::sqrt((float(x) - baseX) * (float(x) - baseX) + (float(y) - startY) * (float(y) - startY) * 0.7f);
    const float shoulder = 1.f - smoothStep(thickness * 0.20f, thickness * 1.9f, shoulderDist);
    strongest = std::max(strongest, std::max(drip, std::max(tip * 0.92f, shoulder * 0.38f)));
  }
  return strongest;
}
EffectResult bloodSplash(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float splashAmount = std::clamp(floatOf(params, "splash_amount"), 0.f, 100.f) / 100.f;
  if (splashAmount <= 0.0001f) return identityCopy(source, context);
  const float dripLength = std::clamp(floatOf(params, "drip_length"), 0.f, 100.f) / 100.f;
  const float spread = std::clamp(floatOf(params, "spread"), 0.f, 100.f) / 100.f;
  const float darkness = std::clamp(floatOf(params, "darkness"), 0.f, 100.f) / 100.f;
  const float wetShine = std::clamp(floatOf(params, "wet_shine"), 0.f, 100.f) / 100.f;
  const int seed = positiveSeed(context), height = source.height();
  const float splashCell = 18.f + (1.f - spread) * 20.f, splashRadius = 5.5f + spread * 15.5f, splashDensity = 0.025f + splashAmount * 0.16f;
  const float sprayCell = 10.f + (1.f - spread) * 10.f, sprayRadius = 0.85f + spread * 2.5f, sprayDensity = 0.035f + splashAmount * 0.22f;
  const float dripCell = 16.f + (1.f - spread) * 18.f, dripProbability = 0.08f + splashAmount * 0.26f;
  return mapPixels(source, context, [&](int x, int y, QRgb src) {
    float r = float(qRed(src)), g = float(qGreen(src)), b = float(qBlue(src));
    const float splash = splashMask(x, y, splashCell, splashRadius, splashDensity, seed ^ 991);
    const float spray = dropletField(x, y, sprayCell, sprayRadius, sprayDensity, seed ^ 2081);
    const float drips = bloodDrip(x, y, height, dripCell, dripProbability, dripLength, spread, seed ^ 4201);
    const float mist = std::pow(hash01(x, y, seed ^ 6311), 24.f) * splashAmount * (0.18f + spread * 0.45f);
    const float mask = std::clamp(splash * 0.98f + spray * 0.58f + drips * 0.88f + mist * 0.32f, 0.f, 1.f) * (0.52f + splashAmount * 0.48f);
    if (mask > 0.0001f) {
      const float clot = std::max(0.f, splash - 0.32f) * (0.85f + darkness * 0.35f);
      const float pooled = std::max(clot, drips * 0.28f);
      const float darkMix = std::clamp(darkness * 0.72f + pooled * 0.55f, 0.f, 1.f);
      const float coverage = std::clamp(mask + splash * 0.12f, 0.f, 1.f);
      r = lerp(r, lerp(198.f, 78.f, darkMix), coverage);
      g = lerp(g, lerp(24.f, 6.f, darkMix), coverage);
      b = lerp(b, lerp(26.f, 10.f, darkMix), coverage);
      const float absorb = std::clamp(pooled * 0.24f + darkness * mask * 0.16f, 0.f, 0.42f);
      r *= 1.f - absorb * 0.18f;
      g *= 1.f - absorb * 0.34f;
      b *= 1.f - absorb * 0.38f;
      if (wetShine > 0.0001f) {
        const float shineNoise = hash01(int(float(x) * 0.28f), int(float(y) * 0.28f), seed ^ 7607);
        const float shineMask = smoothStep(0.76f, 0.985f, shineNoise) * smoothStep(0.22f, 0.82f, std::max(splash, drips));
        const float shine = shineMask * wetShine * (0.25f + 0.75f * std::max(splash, drips));
        if (shine > 0.0001f) {
          r = lerp(r, 255.f, shine * 0.24f);
          g = lerp(g, 132.f, shine * 0.20f);
          b = lerp(b, 122.f, shine * 0.14f);
        }
      }
    }
    return qRgba(toByte(r), toByte(g), toByte(b), toByte(float(qAlpha(src))));
  });
}

void fillBands(QPainter &painter, int newW, int newH, int border, int srcH, const QColor &color) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawRect(0, 0, newW, border);
  painter.drawRect(0, newH - border, newW, border);
  painter.drawRect(0, border, border, srcH);
  painter.drawRect(newW - border, border, border, srcH);
}
float crossRatio(int x, int y, int border, int srcW, int srcH) {
  const float clampX = std::clamp(float(x), float(border), float(border + srcW - 1));
  const float clampY = std::clamp(float(y), float(border), float(border + srcH - 1));
  const float dist = std::sqrt((float(x) - clampX) * (float(x) - clampX) + (float(y) - clampY) * (float(y) - clampY));
  return std::clamp(dist / float(border), 0.f, 1.f);
}
EffectResult chromeBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = clampInt(intOf(params, "border_size"), 4, 300);
  const float reflectivity = std::clamp(floatOf(params, "reflectivity"), 0.f, 100.f) / 100.f;
  const float bevel = std::clamp(floatOf(params, "bevel_strength"), 0.f, 100.f) / 100.f;
  const QColor tint = colorOf(params, "tint_color");
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  static const float stops[][2] = {{0.f, 0.15f}, {0.10f, 0.80f}, {0.22f, 0.48f}, {0.38f, 0.12f}, {0.52f, 0.72f},
                                   {0.68f, 0.90f}, {0.80f, 0.55f}, {0.90f, 0.22f}, {1.f, 0.35f}};
  QImage straight(newW, newH, QImage::Format_ARGB32);
  straight.fill(0);
  const float tR = float(tint.red()) / 200.f, tG = float(tint.green()) / 200.f, tB = float(tint.blue()) / 200.f;
  for (int y = 0; y < newH; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    auto *row = reinterpret_cast<QRgb *>(straight.scanLine(y));
    const bool top = y < border, bottom = y >= border + srcH;
    for (int x = 0; x < newW; ++x) {
      if (!top && !bottom && x >= border && x < border + srcW) continue;
      float t = crossRatio(x, y, border, srcW, srcH);
      float chrome = 0.5f;
      for (int i = 1; i < 9; ++i)
        if (t <= stops[i][0]) {
          const float local = (t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0]);
          chrome = lerp(0.5f, lerp(stops[i - 1][1], stops[i][1], local), reflectivity);
          break;
        }
      float side = 0.f;
      const bool midX = x >= border && x < border + srcW, midY = y >= border && y < border + srcH;
      if (top && midX) side = bevel * 0.5f;
      else if (bottom && midX) side = bevel * -0.4f;
      else if (x < border && midY) side = bevel * 0.2f;
      else if (x >= border + srcW && midY) side = bevel * -0.2f;
      const float brightness = std::clamp(chrome + side * 0.12f, 0.f, 1.f) * 255.f;
      row[x] = qRgba(toByte(brightness * tR), toByte(brightness * tG), toByte(brightness * tB), 255);
    }
  }
  QImage surface = straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.drawImage(border, border, source);
  if (boolOf(params, "inner_line")) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(40, 40, 45), 1));
    painter.drawRect(QRectF(border - 0.5, border - 0.5, srcW + 1, srcH + 1));
  }
  if (boolOf(params, "outer_line")) {
    painter.setPen(QPen(QColor(20, 20, 22), 1.5));
    painter.drawRect(QRectF(0.75, 0.75, newW - 1.5, newH - 1.5));
  }
  painter.end();
  return publish(surface, source, context);
}

EffectResult frostBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  constexpr int kSeed = 4519;
  const int border = clampInt(intOf(params, "border_size"), 10, 300);
  const float crystal = std::clamp(floatOf(params, "crystallization"), 0.f, 100.f) / 100.f;
  const float transparency = std::clamp(floatOf(params, "transparency"), 0.f, 80.f) / 100.f;
  const QColor ice = colorOf(params, "ice_color");
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  QImage straight(newW, newH, QImage::Format_ARGB32);
  straight.fill(0);
  for (int y = 0; y < newH; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    auto *row = reinterpret_cast<QRgb *>(straight.scanLine(y));
    const bool top = y < border, bottom = y >= border + srcH;
    for (int x = 0; x < newW; ++x) {
      if (!top && !bottom && x >= border && x < border + srcW) continue;
      const float ratio = crossRatio(x, y, border, srcW, srcH);
      const float iceThickness = smoothStep(0.f, 0.5f, ratio);
      const float coarse = fractalNoise(float(x) * 0.02f, float(y) * 0.02f, 4, 2.5f, 0.45f, kSeed);
      const float fine = fractalNoise(float(x) * 0.08f, float(y) * 0.08f, 3, 2.0f, 0.5f, kSeed ^ 0x3F);
      const float pattern = coarse + fine * 0.5f;
      const float crystallized = crystal > 0.f ? std::pow(std::clamp(pattern, 0.f, 1.f), 1.f - crystal * 0.7f) : pattern;
      const float highlight = smoothStep(0.55f, 0.75f, crystallized);
      float r = lerp(float(ice.red()) / 255.f, 1.f, highlight * 0.6f);
      float g = lerp(float(ice.green()) / 255.f, 1.f, highlight * 0.5f);
      float b = lerp(float(ice.blue()) / 255.f, 1.f, highlight * 0.35f);
      const float vein = smoothStep(0.2f, 0.35f, pattern);
      r -= vein * 0.12f;
      g -= vein * 0.06f;
      float alpha = lerp(1.f - transparency, 1.f, iceThickness) * (0.85f + crystallized * 0.15f);
      row[x] = qRgba(toByte(r * 255.f), toByte(g * 255.f), toByte(b * 255.f), toByte(std::clamp(alpha, 0.f, 1.f) * 255.f));
    }
  }
  QImage surface = straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&surface);
  painter.drawImage(border, border, source);
  if (boolOf(params, "sparkle")) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int count = (newW + newH) / 12;
    for (int i = 0; i < count; ++i) {
      const float hx = hash01(i, kSeed ^ 0xAA, kSeed), hy = hash01(i, kSeed ^ 0xBB, kSeed ^ 0x11), hs = hash01(i, kSeed ^ 0xCC, kSeed ^ 0x22);
      const int sx = int(hx * float(newW)), sy = int(hy * float(newH));
      if (!(sx < border || sx >= border + srcW || sy < border || sy >= border + srcH)) continue;
      const float radius = 0.8f + hs * 2.2f;
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 255, 255, truncByte(140.f + hs * 115.f)));
      painter.drawEllipse(QPointF(sx, sy), radius, radius);
      if (hs > 0.5f) {
        const float starLen = radius * 2.5f;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, truncByte(80.f + hs * 80.f)), 0.5));
        painter.drawLine(QPointF(sx - starLen, sy), QPointF(sx + starLen, sy));
        painter.drawLine(QPointF(sx, sy - starLen), QPointF(sx, sy + starLen));
      }
    }
  }
  painter.end();
  return publish(surface, source, context);
}

void drawBandKnots(QPainter &painter, const QPen &knot, const QPen &outline, bool drawOutline, float rx, float ry, float rw, float rh, bool horizontal,
                   float cellSize) {
  painter.save();
  painter.setClipRect(QRectF(rx, ry, rw, rh), Qt::IntersectClip);
  const float primaryLen = horizontal ? rw : rh;
  const float mid = horizontal ? ry + rh * 0.5f : rx + rw * 0.5f;
  const float amplitude = (horizontal ? rh : rw) * 0.25f;
  const int numCells = std::max(2, int(primaryLen / cellSize) + 1);
  const int segments = numCells * 8;
  const float segLen = primaryLen / float(segments);
  for (int strand = 0; strand < 2; ++strand) {
    QPainterPath path;
    const float phase = float(strand) * kPi;
    for (int s = 0; s <= segments; ++s) {
      const float along = float(s) * segLen;
      const float wave = std::sin((along / cellSize) * kPi * 2.f + phase) * amplitude;
      const QPointF point = horizontal ? QPointF(rx + along, mid + wave) : QPointF(mid + wave, ry + along);
      if (s == 0) path.moveTo(point);
      else path.lineTo(point);
    }
    if (drawOutline) {
      painter.setPen(outline);
      painter.drawPath(path);
    }
    painter.setPen(knot);
    painter.drawPath(path);
  }
  painter.restore();
}
EffectResult celticKnot(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = clampInt(intOf(params, "border_size"), 16, 200);
  const float strandFrac = std::clamp(floatOf(params, "strand_width"), 10.f, 80.f) / 100.f;
  const QColor knot = colorOf(params, "knot_color"), bg = colorOf(params, "bg_color");
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  QImage surface = premulCanvas({newW, newH}, 0);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  fillBands(painter, newW, newH, border, srcH, bg);
  const float strandW = float(border) * strandFrac;
  QPen knotPen(knot, strandW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  QPen outlinePen(QColor::fromRgba(darkenTrunc(bg.rgba(), 0.3f)), strandW + 3.f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  const bool outline = boolOf(params, "outline_strands");
  const float cellSize = float(border) * 1.2f;
  painter.setBrush(Qt::NoBrush);
  drawBandKnots(painter, knotPen, outlinePen, outline, 0, 0, newW, border, true, cellSize);
  drawBandKnots(painter, knotPen, outlinePen, outline, 0, newH - border, newW, border, true, cellSize);
  drawBandKnots(painter, knotPen, outlinePen, outline, 0, border, border, srcH, false, cellSize);
  drawBandKnots(painter, knotPen, outlinePen, outline, newW - border, border, border, srcH, false, cellSize);
  painter.setClipping(false);
  painter.drawImage(border, border, source);
  if (boolOf(params, "inner_line")) {
    painter.setPen(QPen(knot, 2.5));
    painter.drawRect(QRectF(border - 1.25, border - 1.25, srcW + 2.5, srcH + 2.5));
  }
  painter.setPen(QPen(knot, 2));
  painter.drawRect(QRectF(1, 1, newW - 2, newH - 2));
  painter.end();
  return publish(surface, source, context);
}

void drawFan(QPainter &painter, const QPen &stroke, const QBrush &fill, float cx, float cy, float radius, int rays, float angle0, float sweep, float arcStart) {
  QPainterPath path;
  for (int i = 0; i <= rays; ++i) {
    const float a = angle0 + sweep * float(i) / float(rays);
    path.moveTo(cx, cy);
    path.lineTo(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
  }
  skiaArc(path, QRectF(cx - radius, cy - radius, radius * 2.f, radius * 2.f), arcStart, 180.f);
  painter.setPen(Qt::NoPen);
  painter.setBrush(fill);
  painter.drawPath(path);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(stroke);
  painter.drawPath(path);
}
EffectResult artDeco(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = clampInt(intOf(params, "border_size"), 20, 300);
  const float spacing = std::clamp(floatOf(params, "fan_spacing"), 30.f, 200.f);
  const int rays = clampInt(intOf(params, "fan_rays"), 3, 12);
  const QColor bg = colorOf(params, "bg_color"), accent = colorOf(params, "accent_color");
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  QImage surface = premulCanvas({newW, newH}, bg.rgba());
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QPen stroke(accent, 1.5);
  const QBrush fill(QColor(accent.red(), accent.green(), accent.blue(), 40));
  const float fanRadius = float(border) * 0.65f;
  for (float cx = float(border); cx <= float(newW - border); cx += spacing)
    drawFan(painter, stroke, fill, cx, float(border) * 0.5f, fanRadius, rays, kPi, kPi, 180.f);
  for (float cx = float(border); cx <= float(newW - border); cx += spacing)
    drawFan(painter, stroke, fill, cx, float(newH) - float(border) * 0.5f, fanRadius, rays, 0.f, kPi, 0.f);
  for (float cy = float(border); cy <= float(newH - border); cy += spacing)
    drawFan(painter, stroke, fill, float(border) * 0.5f, cy, fanRadius, rays, kPi / 2.f, kPi, 90.f);
  for (float cy = float(border); cy <= float(newH - border); cy += spacing)
    drawFan(painter, stroke, fill, float(newW) - float(border) * 0.5f, cy, fanRadius, rays, -kPi / 2.f, kPi, 270.f);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(accent, 2));
  painter.drawRect(QRectF(2, 2, newW - 4, newH - 4));
  painter.drawRect(QRectF(6, 6, newW - 12, newH - 12));
  painter.drawRect(QRectF(border - 2, border - 2, srcW + 4, srcH + 4));
  if (boolOf(params, "inner_lines")) {
    painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 100), 1));
    const float inset = float(border) * 0.25f, inset2 = float(border) * 0.75f;
    painter.drawRect(QRectF(inset, inset, float(newW) - inset * 2.f, float(newH) - inset * 2.f));
    painter.drawRect(QRectF(inset2, inset2, float(newW) - inset2 * 2.f, float(newH) - inset2 * 2.f));
  }
  painter.setPen(QPen(accent, 1.5));
  const float diamond = float(border) * 0.3f;
  const QPointF corners[]{QPointF(4, 4), QPointF(newW - 4, 4), QPointF(4, newH - 4), QPointF(newW - 4, newH - 4)};
  for (const QPointF &c : corners) {
    QPainterPath path;
    path.moveTo(c.x(), c.y() - diamond);
    path.lineTo(c.x() + diamond, c.y());
    path.lineTo(c.x(), c.y() + diamond);
    path.lineTo(c.x() - diamond, c.y());
    path.closeSubpath();
    painter.drawPath(path);
  }
  painter.drawImage(border, border, source);
  painter.end();
  return publish(surface, source, context);
}

void drawOrnament(QPainter &painter, QColor color, float cx, float cy, float size) {
  QPainterPath diamond;
  diamond.moveTo(cx, cy - size);
  diamond.lineTo(cx + size * 0.55f, cy);
  diamond.lineTo(cx, cy + size);
  diamond.lineTo(cx - size * 0.55f, cy);
  diamond.closeSubpath();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawPath(diamond);
  painter.drawEllipse(QPointF(cx, cy), size * 0.18f, size * 0.18f);
}
void drawScroll(QPainter &painter, const QPen &pen, float x, float y, float size, int corner) {
  const float sx = corner == 0 || corner == 2 ? 1.f : -1.f;
  const float sy = corner == 0 || corner == 1 ? 1.f : -1.f;
  QPainterPath horizontal, vertical;
  horizontal.moveTo(x, y);
  horizontal.cubicTo(x + sx * size * 0.4f, y, x + sx * size * 0.7f, y + sy * size * 0.2f, x + sx * size * 0.8f, y + sy * size * 0.5f);
  horizontal.cubicTo(x + sx * size * 0.85f, y + sy * size * 0.7f, x + sx * size * 0.65f, y + sy * size * 0.85f, x + sx * size * 0.45f, y + sy * size * 0.75f);
  vertical.moveTo(x, y);
  vertical.cubicTo(x, y + sy * size * 0.4f, x + sx * size * 0.2f, y + sy * size * 0.7f, x + sx * size * 0.5f, y + sy * size * 0.8f);
  vertical.cubicTo(x + sx * size * 0.7f, y + sy * size * 0.85f, x + sx * size * 0.85f, y + sy * size * 0.65f, x + sx * size * 0.75f, y + sy * size * 0.45f);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(pen);
  painter.drawPath(horizontal);
  painter.drawPath(vertical);
}
EffectResult ornateScroll(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = clampInt(intOf(params, "border_size"), 10, 200);
  const float lineThick = std::clamp(floatOf(params, "line_thickness"), 0.5f, 6.f);
  const QColor frame = colorOf(params, "frame_color"), ornament = colorOf(params, "ornament_color");
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  QImage surface = premulCanvas({newW, newH}, 0);
  if (boolOf(params, "shadow")) {
    QImage layer = premulCanvas(surface.size(), 0);
    QPainter shadowPainter(&layer);
    shadowPainter.setPen(Qt::NoPen);
    shadowPainter.setBrush(QColor(0, 0, 0, 40));
    shadowPainter.drawRect(QRectF(border + 3, border + 4, srcW, srcH));
    shadowPainter.end();
    // One blurred layer stands in for Skia's per-shape mask filter. Sigma is border * 0.18.
    const QImage blurred = blurStraight(layer.convertToFormat(QImage::Format_ARGB32), float(border) * 0.18f, context);
    if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
    QPainter painter(&surface);
    painter.drawImage(0, 0, blurred);
    painter.end();
  }
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  if (boolOf(params, "fill_background")) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(frame);
    painter.drawRect(0, 0, newW, newH);
  }
  painter.drawImage(border, border, source);
  QLinearGradient inner(QPointF(border, border), QPointF(border + float(border) * 0.15f, border + float(border) * 0.15f));
  inner.setColorAt(0, QColor(0, 0, 0, 45));
  inner.setColorAt(1, QColor(0, 0, 0, 0));
  painter.setPen(Qt::NoPen);
  painter.setBrush(inner);
  painter.drawRect(border, border, srcW, srcH);
  painter.setBrush(Qt::NoBrush);
  QPen pen(ornament, lineThick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.drawRect(QRectF(lineThick / 2.f, lineThick / 2.f, float(newW) - lineThick, float(newH) - lineThick));
  const float inset = float(border) * 0.30f, inset2 = float(border) * 0.70f, insetMid = float(border) * 0.50f;
  painter.drawRect(QRectF(inset, inset, float(newW) - inset * 2.f, float(newH) - inset * 2.f));
  painter.drawRect(QRectF(inset2, inset2, float(newW) - inset2 * 2.f, float(newH) - inset2 * 2.f));
  painter.setPen(QPen(ornament, lineThick * 0.5f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawRect(QRectF(insetMid, insetMid, float(newW) - insetMid * 2.f, float(newH) - insetMid * 2.f));
  const float ornSize = std::min(float(border) * 0.35f, 18.f), ornSmall = ornSize * 0.65f;
  drawOrnament(painter, ornament, inset, inset, ornSize);
  drawOrnament(painter, ornament, float(newW) - inset, inset, ornSize);
  drawOrnament(painter, ornament, inset, float(newH) - inset, ornSize);
  drawOrnament(painter, ornament, float(newW) - inset, float(newH) - inset, ornSize);
  drawOrnament(painter, ornament, float(newW) * 0.5f, inset, ornSmall);
  drawOrnament(painter, ornament, float(newW) * 0.5f, float(newH) - inset, ornSmall);
  drawOrnament(painter, ornament, inset, float(newH) * 0.5f, ornSmall);
  drawOrnament(painter, ornament, float(newW) - inset, float(newH) * 0.5f, ornSmall);
  const QPen scroll(ornament, lineThick * 0.9f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  const float scrollSize = float(border) * 0.40f, anchor = lineThick * 0.5f;
  drawScroll(painter, scroll, anchor, anchor, scrollSize, 0);
  drawScroll(painter, scroll, float(newW) - anchor, anchor, scrollSize, 1);
  drawScroll(painter, scroll, anchor, float(newH) - anchor, scrollSize, 2);
  drawScroll(painter, scroll, float(newW) - anchor, float(newH) - anchor, scrollSize, 3);
  painter.end();
  return publish(surface, source, context);
}

EffectResult woodenFrame(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int frame = clampInt(intOf(params, "frame_width"), 2, 300);
  const float grainStrength = std::clamp(floatOf(params, "grain_strength"), 0.f, 100.f) / 100.f;
  const float bevelStrength = std::clamp(floatOf(params, "bevel_strength"), 0.f, 100.f) / 100.f;
  const QColor wood = colorOf(params, "wood_color");
  const int seed = 1649 ^ seedOf(context);
  const int srcW = source.width(), srcH = source.height(), newW = srcW + frame * 2, newH = srcH + frame * 2;
  QImage straight(newW, newH, QImage::Format_ARGB32);
  straight.fill(0);
  const float span = std::max(1.f, float(frame - 1));
  for (int y = 0; y < newH; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    auto *row = reinterpret_cast<QRgb *>(straight.scanLine(y));
    const bool top = y < frame, bottom = y >= frame + srcH;
    for (int x = 0; x < newW; ++x) {
      const bool left = x < frame, right = x >= frame + srcW;
      if (!top && !bottom && !left && !right) continue;
      float along, across, outerDistance, innerDistance;
      int boardSeed, side;
      if (top) {
        side = 0;
        along = float(x);
        across = float(y);
        outerDistance = float(y);
        innerDistance = float(frame - 1 - y);
        boardSeed = seed ^ 0x1C3;
      } else if (bottom) {
        side = 3;
        along = float(x);
        across = float(y - (frame + srcH));
        outerDistance = float(newH - 1 - y);
        innerDistance = float(y - (frame + srcH));
        boardSeed = seed ^ 0x29B;
      } else if (left) {
        side = 1;
        along = float(y);
        across = float(x);
        outerDistance = float(x);
        innerDistance = float(frame - 1 - x);
        boardSeed = seed ^ 0x35F;
      } else {
        side = 2;
        along = float(y);
        across = float(x - (frame + srcW));
        outerDistance = float(newW - 1 - x);
        innerDistance = float(x - (frame + srcW));
        boardSeed = seed ^ 0x41D;
      }
      const float acrossRatio = across / span;
      const float boardVariation = (hash01(boardSeed, frame, seed) * 2.f - 1.f) * 0.06f;
      const float coarse = hash01(int(along * 0.20f), int(across * 0.08f), boardSeed) * 2.f - 1.f;
      const float medium = hash01(int(along * 0.55f), int(across * 0.16f), boardSeed ^ 0x57) * 2.f - 1.f;
      const float fine = hash01(int(along * 1.30f), int(across * 0.42f), boardSeed ^ 0xA1) * 2.f - 1.f;
      const float wave1 = std::sin(along * 0.075f + coarse * 5.6f + across * 0.028f);
      const float wave2 = std::sin(along * 0.19f - medium * 3.1f + across * 0.012f);
      const float ridge = float(signf(wave1)) * std::pow(std::fabs(wave1), 1.75f);
      const float grain = (ridge * 0.20f + wave2 * 0.08f + fine * 0.12f + std::sin(across * 0.55f + coarse * 2.2f) * 0.04f) * grainStrength;
      const float bevelSpan = std::max(2.f, float(frame) * (0.16f + 0.10f * bevelStrength));
      const float outerMask = 1.f - smoothStep(0.f, bevelSpan, outerDistance);
      const float innerMask = 1.f - smoothStep(0.f, bevelSpan, innerDistance);
      const float bodySlope = 0.5f - acrossRatio;
      float shade = 0.f;
      if (side == 0) shade = outerMask * 0.24f - innerMask * 0.28f + bodySlope * 0.12f;
      else if (side == 1) shade = outerMask * 0.18f - innerMask * 0.22f + bodySlope * 0.10f;
      else if (side == 2) shade = -(outerMask * 0.20f) + innerMask * 0.08f - bodySlope * 0.08f;
      else shade = -(outerMask * 0.26f) + innerMask * 0.10f - bodySlope * 0.12f;
      shade = boardVariation + shade * bevelStrength;
      const float seamWidth = std::max(1.5f, float(frame) * 0.08f);
      auto seam = [&](float diagonal) { return (1.f - smoothStep(0.f, seamWidth, std::fabs(diagonal))) * 0.10f; };
      if (x < frame && y < frame) shade -= seam(float(x + y) - float(frame - 1));
      else if (x >= newW - frame && y < frame) shade -= seam(float(newW - 1 - x + y) - float(frame - 1));
      else if (x < frame && y >= newH - frame) shade -= seam(float(x + (newH - 1 - y)) - float(frame - 1));
      else if (x >= newW - frame && y >= newH - frame) shade -= seam(float(newW - 1 - x + newH - 1 - y) - float(frame - 1));
      row[x] = qRgba(toByte(float(wood.red()) * (1.f + grain + shade + 0.03f)), toByte(float(wood.green()) * (1.f + grain * 0.82f + shade)),
                     toByte(float(wood.blue()) * (1.f + grain * 0.48f + shade - 0.03f)), 255);
    }
  }
  QImage surface = straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&surface);
  painter.drawImage(frame, frame, source);
  painter.end();
  return publish(surface, source, context);
}

struct TraceSeg { float x1, y1, x2, y2; };
void circuitBand(std::vector<TraceSeg> &glow, std::vector<TraceSeg> &traces, std::vector<QPointF> &pads, std::vector<QPointF> &holes, float rx, float ry,
                 float rw, float rh, bool horizontal, int gridStep, float density, float padR, int seed) {
  const float primaryLen = horizontal ? rw : rh, crossLen = horizontal ? rh : rw;
  const int numTraces = int(primaryLen / float(gridStep) * density) + 1;
  for (int i = 0; i < numTraces; ++i) {
    const float hash = hash01(i, seed, seed ^ 0x7F), hash2 = hash01(i, seed ^ 0x13, seed), hash3 = hash01(i, seed ^ 0x31, seed ^ 0x55);
    const float along = hash * primaryLen, across = hash2 * crossLen * 0.85f + crossLen * 0.05f;
    const float tLen = (0.15f + hash3 * 0.65f) * primaryLen * 0.5f, tEnd = std::min(along + tLen, primaryLen - 1.f);
    float x1, y1, x2, y2;
    if (horizontal) {
      x1 = rx + along;
      y1 = ry + across;
      x2 = rx + tEnd;
      y2 = ry + across;
    } else {
      x1 = rx + across;
      y1 = ry + along;
      x2 = rx + across;
      y2 = ry + tEnd;
    }
    glow.push_back({x1, y1, x2, y2});
    traces.push_back({x1, y1, x2, y2});
    pads.push_back(QPointF(x1, y1));
    const float hash4 = hash01(i, seed ^ 0x9A, seed ^ 0xCF);
    if (hash4 < 0.4f && tLen > float(gridStep) * 1.5f) {
      const float elbowAt = along + tLen * 0.5f;
      const float elbowCross = across + (hash4 < 0.2f ? -1.f : 1.f) * float(gridStep) * (0.5f + hash3 * 0.8f);
      if (horizontal) traces.push_back({rx + elbowAt, ry + across, rx + elbowAt, ry + elbowCross});
      else traces.push_back({rx + across, ry + elbowAt, rx + elbowCross, ry + elbowAt});
      pads.push_back(horizontal ? QPointF(rx + elbowAt, ry + elbowCross) : QPointF(rx + elbowCross, ry + elbowAt));
    }
    holes.push_back(QPointF(x2, y2));
    (void)padR;
  }
}
EffectResult circuitBoard(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  constexpr int kSeed = 8157;
  const int border = clampInt(intOf(params, "border_size"), 20, 300);
  const float density = std::clamp(floatOf(params, "trace_density"), 10.f, 100.f) / 100.f;
  const QColor board = colorOf(params, "board_color"), trace = colorOf(params, "trace_color");
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  const float traceW = std::max(1.5f, float(border) * 0.045f), padR = traceW * 1.8f;
  const int gridStep = int(std::max(6.f, float(border) * 0.22f));
  std::vector<TraceSeg> glow, traces;
  std::vector<QPointF> pads, holes;
  circuitBand(glow, traces, pads, holes, 0, 0, newW, border, true, gridStep, density, padR, kSeed ^ 0x1A);
  circuitBand(glow, traces, pads, holes, 0, newH - border, newW, border, true, gridStep, density, padR, kSeed ^ 0x2B);
  circuitBand(glow, traces, pads, holes, 0, border, border, srcH, false, gridStep, density, padR, kSeed ^ 0x3C);
  circuitBand(glow, traces, pads, holes, newW - border, border, border, srcH, false, gridStep, density, padR, kSeed ^ 0x4D);
  QImage surface = premulCanvas({newW, newH}, 0);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, false);
  fillBands(painter, newW, newH, border, srcH, board);
  painter.end();
  if (boolOf(params, "glow")) {
    QImage layer = premulCanvas(surface.size(), 0);
    QPainter glowPainter(&layer);
    glowPainter.setRenderHint(QPainter::Antialiasing, true);
    glowPainter.setPen(QPen(QColor(trace.red(), trace.green(), trace.blue(), 45), traceW * 3.5f, Qt::SolidLine, Qt::RoundCap));
    for (const auto &seg : glow) glowPainter.drawLine(QPointF(seg.x1, seg.y1), QPointF(seg.x2, seg.y2));
    glowPainter.end();
    // One blur of the glow strokes stands in for a mask filter on every trace. Sigma is trace width * 1.5.
    const QImage blurred = blurStraight(layer.convertToFormat(QImage::Format_ARGB32), traceW * 1.5f, context);
    if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
    painter.begin(&surface);
    painter.drawImage(0, 0, blurred);
    painter.end();
  }
  painter.begin(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(trace, traceW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  for (const auto &seg : traces) painter.drawLine(QPointF(seg.x1, seg.y1), QPointF(seg.x2, seg.y2));
  painter.setPen(Qt::NoPen);
  painter.setBrush(trace);
  for (const QPointF &pad : pads) painter.drawEllipse(pad, padR, padR);
  for (const QPointF &hole : holes) painter.drawEllipse(hole, padR * 0.85f * 1.5f, padR * 0.85f * 1.5f);
  painter.setBrush(board);
  for (const QPointF &hole : holes) painter.drawEllipse(hole, padR * 0.85f * 0.65f, padR * 0.85f * 0.65f);
  painter.drawImage(border, border, source);
  if (boolOf(params, "inner_line")) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(trace.red(), trace.green(), trace.blue(), 200), traceW * 0.8f));
    painter.drawRect(QRectF(border - traceW * 0.4f, border - traceW * 0.4f, srcW + traceW * 0.8f, srcH + traceW * 0.8f));
  }
  painter.end();
  return publish(surface, source, context);
}

QRgb shiftColor(QRgb c, int delta) {
  return qRgba(clampInt(qRed(c) + delta, 0, 255), clampInt(qGreen(c) + delta, 0, 255), clampInt(qBlue(c) + delta, 0, 255), qAlpha(c));
}
void glassPanels(QPainter &painter, std::vector<QRectF> &glowRects, std::vector<QColor> &glowColors, float rx, float ry, float rw, float rh, bool horizontal,
                 float panelW, float leadT, float brightness, int seed, bool glow) {
  static const float hues[] = {0, 30, 60, 120, 180, 210, 270, 300, 340};
  const float primaryLen = horizontal ? rw : rh, crossLen = horizontal ? rh : rw;
  const float numRows = std::max(1.f, crossLen / panelW), actualH = crossLen / numRows;
  painter.save();
  painter.setClipRect(QRectF(rx, ry, rw, rh), Qt::IntersectClip);
  int panelIndex = 0;
  for (float cr = 0; cr < numRows; cr += 1.f) {
    float along = 0.f;
    while (along < primaryLen) {
      const float thisLen = panelW * (0.65f + hash01(panelIndex, seed, seed ^ 0x7F) * 0.75f);
      const float panelEnd = std::min(along + thisLen, primaryLen);
      const float hueHash = hash01(panelIndex ^ (panelIndex * 7), seed ^ 0x33, seed);
      const int hueIdx = int(hueHash * 9.f) % 9;
      const float hue = hues[hueIdx < 0 ? hueIdx + 9 : hueIdx];
      const float saturation = 70.f + hash01(panelIndex, seed ^ 0x55, seed) * 30.f;
      float x1, y1, x2, y2;
      if (horizontal) {
        x1 = rx + along + leadT * 0.5f;
        y1 = ry + cr * actualH + leadT * 0.5f;
        x2 = rx + panelEnd - leadT * 0.5f;
        y2 = ry + (cr + 1.f) * actualH - leadT * 0.5f;
      } else {
        x1 = rx + cr * actualH + leadT * 0.5f;
        y1 = ry + along + leadT * 0.5f;
        x2 = rx + (cr + 1.f) * actualH - leadT * 0.5f;
        y2 = ry + panelEnd - leadT * 0.5f;
      }
      if (x2 > x1 + 1.f && y2 > y1 + 1.f) {
        const QColor glass = fromHsv100(hue, saturation, brightness);
        const QRgb base = glass.rgb();
        if (glow) {
          glowRects.push_back(QRectF(x1, y1, x2 - x1, y2 - y1));
          glowColors.push_back(QColor(qRed(base), qGreen(base), qBlue(base), 60));
        }
        const QColor light = QColor::fromRgba(withAlpha(shiftColor(base, int(0.25f * 255.f)), 245));
        const QColor dark = QColor::fromRgba(withAlpha(shiftColor(base, -int(0.15f * 255.f)), 235));
        QLinearGradient gradient(QPointF(x1, y1), QPointF(x2, y2));
        gradient.setColorAt(0, light);
        gradient.setColorAt(1, dark);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRect(QRectF(x1, y1, x2 - x1, y2 - y1));
        const float w = x2 - x1, h = y2 - y1;
        QRadialGradient spec(QPointF(x1 + w * 0.2f, y1 + h * 0.2f), std::min(w, h) * 0.4f);
        spec.setColorAt(0, QColor(255, 255, 255, 90));
        spec.setColorAt(1, QColor(255, 255, 255, 0));
        painter.setBrush(spec);
        painter.drawRect(QRectF(x1, y1, w, h));
      }
      along = panelEnd;
      ++panelIndex;
    }
  }
  painter.restore();
}
EffectResult stainedGlassBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = clampInt(intOf(params, "border_size"), 20, 300);
  const float panelW = std::clamp(floatOf(params, "panel_width"), 10.f, 120.f);
  const float leadT = std::clamp(floatOf(params, "lead_thickness"), 1.f, 12.f);
  const float brightness = std::clamp(floatOf(params, "glass_brightness"), 30.f, 100.f);
  const int srcW = source.width(), srcH = source.height(), newW = srcW + border * 2, newH = srcH + border * 2;
  const QColor lead(14, 10, 8);
  QImage surface = premulCanvas({newW, newH}, 0);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, false);
  fillBands(painter, newW, newH, border, srcH, lead);
  const bool glow = boolOf(params, "glow");
  std::vector<QRectF> glowRects;
  std::vector<QColor> glowColors;
  // Glow is a separate replay of the panel layout, then the glass is painted on top.
  painter.end();
  if (glow) {
    QImage layer = premulCanvas(surface.size(), 0);
    QPainter glowPainter(&layer);
    glowPainter.setRenderHint(QPainter::Antialiasing, true);
    glowPainter.setPen(Qt::NoPen);
    // Record rectangles by replaying the panel layout without painting glass yet.
    painter.begin(&surface);
    painter.end();
    auto record = [&](float rx, float ry, float rw, float rh, bool horizontal, int seed) {
      static const float hues[] = {0, 30, 60, 120, 180, 210, 270, 300, 340};
      const float primaryLen = horizontal ? rw : rh, crossLen = horizontal ? rh : rw;
      const float numRows = std::max(1.f, crossLen / panelW), actualH = crossLen / numRows;
      int panelIndex = 0;
      for (float cr = 0; cr < numRows; cr += 1.f) {
        float along = 0.f;
        while (along < primaryLen) {
          const float thisLen = panelW * (0.65f + hash01(panelIndex, seed, seed ^ 0x7F) * 0.75f);
          const float panelEnd = std::min(along + thisLen, primaryLen);
          const float hueHash = hash01(panelIndex ^ (panelIndex * 7), seed ^ 0x33, seed);
          int hueIdx = int(hueHash * 9.f) % 9;
          if (hueIdx < 0) hueIdx += 9;
          const QColor glass = fromHsv100(hues[hueIdx], 70.f + hash01(panelIndex, seed ^ 0x55, seed) * 30.f, brightness);
          float x1, y1, x2, y2;
          if (horizontal) {
            x1 = rx + along + leadT * 0.5f;
            y1 = ry + cr * actualH + leadT * 0.5f;
            x2 = rx + panelEnd - leadT * 0.5f;
            y2 = ry + (cr + 1.f) * actualH - leadT * 0.5f;
          } else {
            x1 = rx + cr * actualH + leadT * 0.5f;
            y1 = ry + along + leadT * 0.5f;
            x2 = rx + (cr + 1.f) * actualH - leadT * 0.5f;
            y2 = ry + panelEnd - leadT * 0.5f;
          }
          if (x2 > x1 + 1.f && y2 > y1 + 1.f) {
            glowPainter.setBrush(QColor(glass.red(), glass.green(), glass.blue(), 60));
            glowPainter.drawRect(QRectF(x1, y1, x2 - x1, y2 - y1));
          }
          along = panelEnd;
          ++panelIndex;
        }
      }
    };
    record(0, 0, newW, border, true, 0x1A);
    record(0, newH - border, newW, border, true, 0x2B);
    record(0, border, border, srcH, false, 0x3C);
    record(newW - border, border, border, srcH, false, 0x4D);
    glowPainter.end();
    // One blur of every panel stands in for a mask filter on each panel. Sigma is lead thickness * 1.8.
    const QImage blurred = blurStraight(layer.convertToFormat(QImage::Format_ARGB32), leadT * 1.8f, context);
    if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
    painter.begin(&surface);
    painter.drawImage(0, 0, blurred);
    painter.end();
  }
  painter.begin(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  glassPanels(painter, glowRects, glowColors, 0, 0, newW, border, true, panelW, leadT, brightness, 0x1A, false);
  glassPanels(painter, glowRects, glowColors, 0, newH - border, newW, border, true, panelW, leadT, brightness, 0x2B, false);
  glassPanels(painter, glowRects, glowColors, 0, border, border, srcH, false, panelW, leadT, brightness, 0x3C, false);
  glassPanels(painter, glowRects, glowColors, newW - border, border, border, srcH, false, panelW, leadT, brightness, 0x4D, false);
  painter.setClipping(false);
  painter.drawImage(border, border, source);
  if (boolOf(params, "inner_line")) {
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(lead, leadT * 1.5f));
    painter.drawRect(QRectF(border, border, srcW, srcH));
  }
  painter.end();
  return publish(surface, source, context);
}

EffectResult goldenBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int size = clampInt(intOf(params, "size"), 5, 80);
  const float bevel = std::clamp(floatOf(params, "bevel_strength"), 0.f, 100.f) / 100.f;
  const QRgb base = colorOf(params, "color").rgba();
  const QRgb dark = darkenTrunc(base, 0.55f), light = lightenTrunc(base, 0.35f), highlight = lightenTrunc(base, 0.60f);
  const int srcW = source.width(), srcH = source.height(), newW = srcW + size * 2, newH = srcH + size * 2;
  QImage surface = premulCanvas({newW, newH}, 0);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor::fromRgba(base));
  painter.drawRect(0, 0, newW, size);
  painter.drawRect(0, newH - size, newW, size);
  painter.drawRect(0, 0, size, newH);
  painter.drawRect(newW - size, 0, size, newH);
  if (bevel > 0.f) {
    const float bevelWidth = float(size) * 0.35f * bevel, innerW = float(size) * 0.25f * bevel;
    auto fade = [&](QRgb color, int alpha, const QPointF &a, const QPointF &b, const QRectF &rect) {
      QLinearGradient gradient(a, b);
      gradient.setColorAt(0, QColor::fromRgba(withAlpha(color, truncByte(float(alpha) * bevel))));
      gradient.setColorAt(1, QColor(0, 0, 0, 0));
      painter.setBrush(gradient);
      painter.drawRect(rect);
    };
    fade(highlight, 200, QPointF(0, 0), QPointF(0, bevelWidth), QRectF(0, 0, newW, bevelWidth));
    fade(highlight, 180, QPointF(0, 0), QPointF(bevelWidth, 0), QRectF(0, 0, bevelWidth, newH));
    fade(dark, 200, QPointF(0, newH), QPointF(0, float(newH) - bevelWidth), QRectF(0, float(newH) - bevelWidth, newW, bevelWidth));
    fade(dark, 200, QPointF(newW, 0), QPointF(float(newW) - bevelWidth, 0), QRectF(float(newW) - bevelWidth, 0, bevelWidth, newH));
    fade(dark, 160, QPointF(0, size), QPointF(0, float(size) - innerW), QRectF(size, float(size) - innerW, newW - size * 2, innerW));
    fade(dark, 140, QPointF(size, 0), QPointF(float(size) - innerW, 0), QRectF(float(size) - innerW, size, innerW, newH - size * 2));
    fade(light, 140, QPointF(0, newH - size), QPointF(0, float(newH - size) + innerW), QRectF(size, newH - size, newW - size * 2, innerW));
    fade(light, 140, QPointF(newW - size, 0), QPointF(float(newW - size) + innerW, 0), QRectF(newW - size, size, innerW, newH - size * 2));
    const float groove = float(size) * 0.5f;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor::fromRgba(withAlpha(dark, truncByte(120.f * bevel))), 1));
    painter.drawLine(QPointF(0, groove), QPointF(newW, groove));
    painter.drawLine(QPointF(0, float(newH) - groove), QPointF(newW, float(newH) - groove));
    painter.drawLine(QPointF(groove, 0), QPointF(groove, newH));
    painter.drawLine(QPointF(float(newW) - groove, 0), QPointF(float(newW) - groove, newH));
    painter.setPen(QPen(QColor::fromRgba(withAlpha(highlight, truncByte(80.f * bevel))), 1));
    painter.drawLine(QPointF(0, groove + 1), QPointF(newW, groove + 1));
    painter.drawLine(QPointF(0, float(newH) - groove + 1), QPointF(newW, float(newH) - groove + 1));
    painter.drawLine(QPointF(groove + 1, 0), QPointF(groove + 1, newH));
    painter.drawLine(QPointF(float(newW) - groove + 1, 0), QPointF(float(newW) - groove + 1, newH));
  }
  painter.drawImage(size, size, source);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor::fromRgba(dark), 1.5));
  if (boolOf(params, "inner_border")) painter.drawRect(QRectF(size - 1, size - 1, srcW + 2, srcH + 2));
  if (boolOf(params, "outer_border")) painter.drawRect(QRectF(0.5, 0.5, newW - 1, newH - 1));
  painter.end();
  return publish(surface, source, context);
}

EffectResult mosaicPolygon(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int cellSize = clampInt(intOf(params, "cell_size"), 8, 80);
  const float borderWidth = std::clamp(floatOf(params, "border_width"), 0.f, 6.f);
  const float borderOpacity = std::clamp(floatOf(params, "border_opacity"), 0.f, 100.f) / 100.f;
  const float randomness = std::clamp(floatOf(params, "randomness"), 0.f, 100.f) / 100.f;
  const int seed = 9031 ^ seedOf(context);
  const bool hexagon = choiceOf(params, "shape") != QLatin1String("triangle");
  const int width = source.width(), height = source.height();
  const Pixels src(source);
  QImage surface = premulCanvas(source.size(), 0);
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QPen stroke(QColor(0, 0, 0, toByte(255.f * borderOpacity)), borderWidth, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
  if (hexagon) {
    const float radius = float(cellSize) * 0.5f, vertical = 1.7320508f * radius, horizontal = 1.5f * radius, jitter = randomness * radius * 0.45f;
    for (int col = -1;; ++col) {
      const float centerX = radius + float(col) * horizontal;
      if (centerX > float(width) + radius) break;
      const float startY = radius + ((col & 1) != 0 ? vertical * 0.5f : 0.f);
      for (float centerY = startY - vertical; centerY < float(height) + radius; centerY += vertical) {
        const int gx = col + 4096, gy = int(std::floor((centerY + vertical) / std::max(vertical, 0.0001f)));
        const float cx = centerX + (hash01(gx, gy, seed ^ 17) * 2.f - 1.f) * jitter;
        const float cy = centerY + (hash01(gx, gy, seed ^ 73) * 2.f - 1.f) * jitter;
        QPainterPath path;
        for (int i = 0; i < 6; ++i) {
          const float angle = (kPi / 3.f) * float(i) - kPi / 6.f;
          const QPointF point(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
          if (i == 0) path.moveTo(point);
          else path.lineTo(point);
        }
        path.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgba(bilinear(src, cx, cy)));
        painter.drawPath(path);
        if (borderWidth > 0.f && stroke.color().alpha() > 0) {
          painter.setBrush(Qt::NoBrush);
          painter.setPen(stroke);
          painter.drawPath(path);
        }
      }
    }
  } else {
    const float side = float(cellSize), triHeight = side * 0.8660254f, half = side * 0.5f, jitter = randomness * side * 0.30f;
    const int rows = int(std::ceil(float(height) / triHeight)) + 2, cols = int(std::ceil(float(width) / half)) + 2;
    for (int row = -1; row < rows; ++row)
      for (int col = -1; col < cols; ++col) {
        const float baseX = float(col) * half, baseY = float(row) * triHeight;
        QPointF p1, p2, p3;
        if (((row + col) & 1) == 0) {
          p1 = QPointF(baseX, baseY + triHeight);
          p2 = QPointF(baseX + half, baseY);
          p3 = QPointF(baseX + side, baseY + triHeight);
        } else {
          p1 = QPointF(baseX, baseY);
          p2 = QPointF(baseX + side, baseY);
          p3 = QPointF(baseX + half, baseY + triHeight);
        }
        const float cx = (p1.x() + p2.x() + p3.x()) / 3.f + (hash01(col, row, seed ^ 173) * 2.f - 1.f) * jitter;
        const float cy = (p1.y() + p2.y() + p3.y()) / 3.f + (hash01(col, row, seed ^ 719) * 2.f - 1.f) * jitter;
        QPainterPath path;
        path.moveTo(p1);
        path.lineTo(p2);
        path.lineTo(p3);
        path.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgba(bilinear(src, cx, cy)));
        painter.drawPath(path);
        if (borderWidth > 0.f && stroke.color().alpha() > 0) {
          painter.setBrush(Qt::NoBrush);
          painter.setPen(stroke);
          painter.drawPath(path);
        }
      }
  }
  painter.end();
  return publish(surface, source, context);
}

QString matrixChars(const QString &input) {
  const QString trimmed = input.trimmed();
  if (trimmed.isEmpty()) return QStringLiteral("01<>[]{}*+-/\\=#$%&");
  if (trimmed.size() == 1) return trimmed + QLatin1Char('0');
  return trimmed;
}
EffectResult matrixRain(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  const int cell = clampInt(intOf(params, "cell_size"), 6, 24);
  const float density = std::clamp(floatOf(params, "density"), 0.f, 100.f) / 100.f;
  const int trail = clampInt(intOf(params, "trail_length"), 3, 50);
  const float glow = std::clamp(floatOf(params, "glow_amount"), 0.f, 100.f) / 100.f;
  const float sourceBlend = std::clamp(floatOf(params, "source_blend"), 0.f, 100.f) / 100.f;
  const float rainOffset = std::clamp(floatOf(params, "rain_offset"), 0.f, 100.f) / 100.f;
  const float lumInfluence = std::clamp(floatOf(params, "luminance_influence"), 0.f, 100.f) / 100.f;
  const QString chars = matrixChars(textOf(params, "character_set"));
  const int seed = 1337 ^ seedOf(context);
  const int columns = int(std::ceil(float(width) / float(cell))), rows = int(std::ceil(float(height) / float(cell)));
  const Pixels src(source);
  QImage surface = premulCanvas(source.size(), qRgba(0, 12, 0, 255));
  QPainter painter(&surface);
  if (sourceBlend > 0.f) {
    // Reference DrawBitmap uses a paint whose alpha is sourceBlend * 255.
    painter.setOpacity(sourceBlend);
    painter.drawImage(0, 0, source);
    painter.setOpacity(1.f);
  }
  // QGuiApplication::instance() is QCoreApplication::instance() when Gui does not override it.
  const bool haveFont = qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr;
  QFont font;
  if (haveFont) {
    font = QFont(QStringLiteral("Consolas"));
    font.setPixelSize(std::max(1, int(std::nearbyintf(float(cell) * 1.02f))));
    painter.setFont(font);
  }
  QImage glyphLayer = premulCanvas(source.size(), 0);
  QPainter glyphs(&glyphLayer);
  glyphs.setRenderHint(QPainter::Antialiasing, true);
  if (haveFont) glyphs.setFont(font);
  for (int col = 0; col < columns; ++col) {
    if (context.cancelled()) return {{}, kCancelled};
    const float head = std::fmod(hash01(col, seed, seed ^ 9) + rainOffset, 1.f) * float(rows);
    const float secondary = std::fmod(hash01(col, seed ^ 37, seed ^ 101) + rainOffset * 0.63f, 1.f) * float(rows);
    for (int row = 0; row < rows; ++row) {
      const int x0 = col * cell, y0 = row * cell;
      if (x0 >= width || y0 >= height) continue;
      const int x1 = std::min(width, x0 + cell), y1 = std::min(height, y0 + cell);
      float sumLum = 0.f, sumAlpha = 0.f;
      int count = 0;
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          const QRgb c = src.at(x, y);
          sumLum += luminance01(c);
          sumAlpha += float(qAlpha(c));
          ++count;
        }
      const float lum = count ? sumLum / float(count) : 0.f;
      const int alpha = count ? toByte(sumAlpha / float(count)) : 255;
      if (hash01(col, row, seed ^ 503) > density * (0.35f + 0.65f * (lumInfluence * lum + (1.f - lumInfluence)))) continue;
      auto trailFactor = [&](float headRow, float length) {
        float dist = float(row) - headRow;
        if (dist < 0.f) dist += float(rows);
        if (dist < 0.f || dist > length) return 0.f;
        return 1.f - dist / std::max(1.f, length);
      };
      const float factor = std::max(trailFactor(head, float(trail)), trailFactor(secondary, float(trail) * 0.75f));
      const float intensity = std::clamp((lum * lumInfluence + 0.35f * (1.f - lumInfluence)) * 0.55f + factor * 0.9f, 0.f, 1.2f);
      if (intensity <= 0.03f) continue;
      const float selector = std::fmod(lum * float(chars.size()) + float(row) * 0.61f + float(col) * 0.17f, float(chars.size()));
      int charIndex = int(std::fabs(std::floor(selector)));
      if (charIndex >= chars.size()) charIndex = chars.size() - 1;
      const QString glyph = chars.mid(charIndex, 1);
      const QColor color(toByte(28.f + intensity * 118.f), toByte(120.f + intensity * 135.f), toByte(28.f + intensity * 118.f), alpha);
      const float baseline = float(y0) + float(cell) * 0.88f;
      if (haveFont) glyphs.setPen(color);
      else glyphs.setPen(Qt::NoPen);
      glyphs.setBrush(color);
      if (haveFont) glyphs.drawText(QPointF(x0, baseline), glyph);
      else glyphs.drawRect(QRectF(x0 + 1, y0 + 1, std::max(1, cell - 2), std::max(1, cell - 2)));
    }
  }
  glyphs.end();
  if (glow > 0.f) {
    // One blur of the glyph layer stands in for a mask filter on every glyph.
    const QImage blurred = blurStraight(glyphLayer.convertToFormat(QImage::Format_ARGB32), 2.2f + glow * 3.4f, context);
    if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
    painter.setOpacity(std::clamp(glow, 0.f, 1.f));
    painter.drawImage(0, 0, blurred);
    painter.setOpacity(1);
  }
  painter.drawImage(0, 0, glyphLayer);
  painter.end();
  return publish(surface, source, context);
}

EffectResult windows11(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int corner = clampInt(intOf(params, "corner_radius"), 0, 16);
  const int shadowSize = clampInt(intOf(params, "shadow_size"), 0, 60);
  const int padding = clampInt(intOf(params, "padding"), 0, 100);
  const bool dark = boolOf(params, "dark_mode");
  const QString title = textOf(params, "title");
  constexpr int kTitle = 32;
  const int windowW = source.width(), windowH = kTitle + source.height();
  const int totalW = windowW + padding * 2 + shadowSize * 2, totalH = windowH + padding * 2 + shadowSize * 2;
  const float windowX = float(padding + shadowSize), windowY = float(padding + shadowSize);
  const QRectF window(windowX, windowY, windowW, windowH);
  QImage surface = premulCanvas({totalW, totalH}, 0);
  if (shadowSize > 0) {
    QImage layer = premulCanvas(surface.size(), 0);
    QPainter shadowPainter(&layer);
    shadowPainter.setRenderHint(QPainter::Antialiasing, true);
    shadowPainter.setPen(Qt::NoPen);
    shadowPainter.setBrush(QColor(0, 0, 0, 40));
    shadowPainter.drawRoundedRect(window, corner, corner);
    shadowPainter.end();
    // Sigma is shadowSize / 2.5, one image blur instead of SKMaskFilter.
    const QImage blurred = blurStraight(layer.convertToFormat(QImage::Format_ARGB32), float(shadowSize) / 2.5f, context);
    if (blurred.isNull()) return {{}, context.cancelled() ? kCancelled : kFailed};
    QPainter painter(&surface);
    painter.drawImage(0, 0, blurred);
    painter.end();
  }
  QPainter painter(&surface);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath clip;
  clip.addRoundedRect(window, corner, corner);
  painter.setClipPath(clip);
  const QColor titleBar = dark ? QColor(32, 32, 32) : QColor(243, 243, 243);
  painter.setPen(Qt::NoPen);
  painter.setBrush(titleBar);
  painter.drawRect(QRectF(windowX, windowY, windowW, kTitle));
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(dark ? QColor(50, 50, 50) : QColor(229, 229, 229), 1));
  painter.drawLine(QPointF(windowX, windowY + kTitle), QPointF(windowX + windowW, windowY + kTitle));
  const QColor icon = dark ? QColor(200, 200, 200) : QColor(90, 90, 90);
  const float controls = windowX + windowW;
  auto button = [&](float x, const QColor &bg, const QColor &mark, int kind) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRect(QRectF(x, windowY, 46, 32));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(mark, 1, Qt::SolidLine, Qt::RoundCap));
    const float cx = x + 23.f, cy = windowY + 16.f;
    if (kind == 0) {
      painter.drawLine(QPointF(cx - 5, cy), QPointF(cx + 5, cy));
    } else if (kind == 1) {
      painter.drawRect(QRectF(cx - 5, cy - 5, 10, 10));
    } else {
      painter.drawLine(QPointF(cx - 5, cy - 5), QPointF(cx + 5, cy + 5));
      painter.drawLine(QPointF(cx + 5, cy - 5), QPointF(cx - 5, cy + 5));
    }
  };
  button(controls - 46, QColor(196, 43, 28), Qt::white, 2);
  button(controls - 92, titleBar, icon, 1);
  button(controls - 138, titleBar, icon, 0);
  if (!title.trimmed().isEmpty() && qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
    painter.setPen(dark ? QColor(220, 220, 220) : QColor(50, 50, 50));
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(12);
    painter.setFont(font);
    painter.drawText(QPointF(windowX + 12, windowY + float(kTitle) / 2.f + 4.f), title);
  }
  painter.drawImage(QPointF(windowX, windowY + kTitle), source);
  painter.setClipping(false);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(dark ? QColor(60, 60, 60, 160) : QColor(200, 200, 200, 160), 1));
  painter.drawRoundedRect(window, corner, corner);
  if (!dark) {
    painter.setClipPath(clip);
    QLinearGradient mica(QPointF(windowX, windowY), QPointF(windowX, windowY + 2));
    mica.setColorAt(0, QColor(255, 255, 255, 80));
    mica.setColorAt(1, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(mica);
    painter.drawRect(QRectF(windowX, windowY, windowW, 2));
  }
  painter.end();
  return publish(surface, source, context);
}

}  // namespace

void registerFilterEffects6(EffectRegistry &registry) {
  add(registry, "cartoon_sticker_cutout", "Cartoon sticker cutout", "CartoonStickerCutout",
      {intParam("colorLevels", "Color levels", 8, 2, 20), intParam("edgeThreshold", "Edge threshold", 32, 5, 120),
       floatParam("inkStrength", "Ink strength", 85, 0, 100), intParam("stickerBorder", "Sticker border", 3, 0, 10),
       floatParam("borderStrength", "Border strength", 90, 0, 100)},
      cartoonSticker);
  add(registry, "jpeg_artifact", "JPEG artifact", "JpegArtifact",
      {floatParam("compression", "Compression", 62, 0, 100), intParam("block_size", "Block size", 8, 4, 32),
       floatParam("chroma_bleed", "Chroma bleed", 45, 0, 100), floatParam("ringing", "Ringing", 28, 0, 100)},
      jpegArtifact);
  add(registry, "snowfall_depth_fog", "Snowfall + depth fog", "SnowfallDepthFog",
      {floatParam("snow_amount", "Snow amount", 55, 0, 100), floatParam("flake_size", "Flake size", 5, 1, 14),
       floatParam("wind", "Wind", 20, -100, 100), floatParam("fog_amount", "Fog amount", 40, 0, 100),
       floatParam("fog_height", "Fog height", 65, 10, 100)},
      snowfall, true);
  add(registry, "celtic_knot_border", "Celtic knot border", "CelticKnotBorder",
      {intParam("border_size", "Border size", 52, 16, 200), colorParam("knot_color", "Knot color", QColor(180, 140, 60)),
       colorParam("bg_color", "Background color", QColor(35, 55, 25)), floatParam("strand_width", "Strand width", 35, 10, 80),
       boolParam("outline_strands", "Outline strands", true), boolParam("inner_line", "Inner line", true)},
      celticKnot, false, true);
  add(registry, "chrome_metallic_border", "Chrome metallic border", "ChromeMetallicBorder",
      {intParam("border_size", "Border size", 36, 4, 300), colorParam("tint_color", "Tint color", QColor(200, 205, 210)),
       floatParam("reflectivity", "Reflectivity", 80, 0, 100), floatParam("bevel_strength", "Bevel strength", 70, 0, 100),
       boolParam("inner_line", "Inner line", true), boolParam("outer_line", "Outer line", true)},
      chromeBorder, false, true);
  add(registry, "frost_ice_border", "Frost ice border", "FrostIceBorder",
      {intParam("border_size", "Border size", 55, 10, 300), colorParam("ice_color", "Ice color", QColor(200, 225, 255)),
       floatParam("crystallization", "Crystallization", 65, 0, 100), floatParam("transparency", "Transparency", 25, 0, 80),
       boolParam("sparkle", "Ice sparkle", true)},
      frostBorder, false, true);
  add(registry, "stained_glass", "Stained glass", "StainedGlass",
      {intParam("tile_size", "Tile size", 22, 6, 120), floatParam("irregularity", "Irregularity", 55, 0, 100),
       floatParam("lead_width", "Lead width", 1.8, 0, 12), floatParam("lead_opacity", "Lead opacity", 85, 0, 100),
       floatParam("color_boost", "Color boost", 20, 0, 100)},
      stainedGlass, true);
  add(registry, "art_deco_border", "Art deco border", "ArtDecoBorder",
      {intParam("border_size", "Border size", 64, 20, 300), colorParam("bg_color", "Background color", QColor(15, 15, 25)),
       colorParam("accent_color", "Accent color", QColor(212, 175, 55)), floatParam("fan_spacing", "Fan spacing", 80, 30, 200),
       intParam("fan_rays", "Fan rays", 7, 3, 12), boolParam("inner_lines", "Inner stepped lines", true)},
      artDeco, false, true);
  add(registry, "liquid_glass", "Liquid glass", "LiquidGlass",
      {floatParam("distortion", "Distortion", 9, 0, 35), floatParam("refraction", "Refraction", 45, 0, 100),
       intParam("chroma_shift", "Chroma shift", 1, 0, 8), floatParam("gloss", "Gloss", 40, 0, 100),
       floatParam("flow_scale", "Flow scale", 100, 40, 220)},
      liquidGlass, true);
  add(registry, "ink_splatter_drips", "Ink splatter + drips", "InkSplatterDrips",
      {floatParam("ink_amount", "Ink amount", 55, 0, 100), floatParam("drip_length", "Drip length", 45, 0, 100),
       floatParam("spread", "Spread", 35, 0, 100), floatParam("paper_fade", "Paper fade", 20, 0, 100)},
      inkSplatter, true);
  add(registry, "mosaic_polygon", "Mosaic polygon", "MosaicPolygon",
      {intParam("cell_size", "Cell size", 24, 8, 80), enumParam("shape", "Shape", "hexagon", {"hexagon", "triangle"}),
       floatParam("border_width", "Border width", 1, 0, 6), floatParam("border_opacity", "Border opacity", 45, 0, 100),
       floatParam("randomness", "Randomness", 18, 0, 100)},
      mosaicPolygon, true);
  add(registry, "perlin_noise", "Perlin noise", "PerlinNoise",
      {floatParam("scale", "Scale", 64, 1, 500), intParam("octaves", "Octaves", 4, 1, 8), floatParam("persistence", "Persistence", 0.5, 0, 1),
       floatParam("strength", "Strength", 50, 0, 100), boolParam("monochrome", "Monochrome", true)},
      perlinNoise);
  add(registry, "dithering", "Dithering", "Dithering",
      {enumParam("method", "Method", "floyd_steinberg", {"floyd_steinberg", "bayer_4x4"}),
       enumParam("palette", "Palette", "one_bit_bw", {"one_bit_bw", "web_safe_216", "rgb332", "grayscale_4"}),
       boolParam("serpentine", "Serpentine", true), floatParam("strength", "Strength", 100, 0, 100)},
      dithering);
  add(registry, "ornate_scroll_border", "Ornate scroll border", "OrnateScrollBorder",
      {intParam("border_size", "Border size", 48, 10, 200), colorParam("frame_color", "Frame color", QColor(245, 240, 220)),
       colorParam("ornament_color", "Ornament color", QColor(160, 130, 60)), floatParam("line_thickness", "Line thickness", 2, 0.5, 6),
       boolParam("fill_background", "Fill background", true), boolParam("shadow", "Shadow", true)},
      ornateScroll, false, true);
  add(registry, "neon_edge_glow", "Neon edge glow", "NeonEdgeGlow",
      {floatParam("edge_strength", "Edge strength", 2.2, 0.1, 6), intParam("threshold", "Threshold", 36, 0, 255),
       floatParam("glow_radius", "Glow radius", 8, 0, 40), floatParam("glow_intensity", "Glow intensity", 120, 0, 250),
       floatParam("base_dim", "Base dim", 30, 0, 100), colorParam("neon_color", "Neon color", QColor(0, 240, 255))},
      neonEdge);
  add(registry, "watercolor_kuwahara", "Watercolor / Kuwahara", "WatercolorKuwahara",
      {intParam("radius", "Radius", 4, 2, 10), floatParam("saturation_boost", "Saturation boost", 20, 0, 100),
       floatParam("detail_blend", "Detail blend", 18, 0, 100)},
      watercolor);
  add(registry, "wooden_frame", "Wooden frame", "WoodenFrame",
      {intParam("frame_width", "Frame width", 48, 2, 300), floatParam("grain_strength", "Grain strength", 60, 0, 100),
       floatParam("bevel_strength", "Bevel strength", 65, 0, 100), colorParam("wood_color", "Wood color", QColor(139, 94, 60))},
      woodenFrame, true, true);
  add(registry, "circuit_board_border", "Circuit board border", "CircuitBoardBorder",
      {intParam("border_size", "Border size", 64, 20, 300), colorParam("board_color", "Board color", QColor(12, 58, 32)),
       colorParam("trace_color", "Trace color", QColor(184, 115, 51)), floatParam("trace_density", "Trace density", 55, 10, 100),
       boolParam("glow", "Trace glow", true), boolParam("inner_line", "Inner line", true)},
      circuitBoard, false, true);
  add(registry, "pixel_sorting", "Pixel sorting", "PixelSorting",
      {enumParam("direction", "Direction", "vertical", {"vertical", "horizontal"}),
       enumParam("metric", "Metric", "brightness", {"brightness", "hue"}), floatParam("threshold_low", "Threshold low", 12, 0, 100),
       floatParam("threshold_high", "Threshold high", 85, 0, 100), intParam("min_span_length", "Min span length", 8, 2, 256),
       intParam("max_span_length", "Max span length", 120, 2, 512), floatParam("sort_probability", "Sort probability", 85, 0, 100)},
      pixelSorting, true);
  add(registry, "vintage_print_damage", "Vintage print damage", "VintagePrintDamage",
      {floatParam("sepia_amount", "Sepia amount", 65, 0, 100), floatParam("fade_amount", "Fade amount", 30, 0, 100),
       floatParam("grain_amount", "Grain amount", 25, 0, 100), floatParam("scratch_amount", "Scratch amount", 25, 0, 100),
       floatParam("dust_amount", "Dust amount", 20, 0, 100)},
      vintagePrint, true);
  add(registry, "matrix_digital_rain", "Matrix digital rain", "MatrixDigitalRain",
      {intParam("cell_size", "Cell size", 12, 6, 24), floatParam("density", "Density", 85, 0, 100),
       intParam("trail_length", "Trail length", 12, 3, 50), floatParam("glow_amount", "Glow amount", 40, 0, 100),
       floatParam("source_blend", "Source blend", 22, 0, 100), floatParam("rain_offset", "Rain offset", 0, 0, 100),
       floatParam("luminance_influence", "Luminance influence", 65, 0, 100),
       textParam("character_set", "Character set", QStringLiteral("01<>[]{}*+-/\\=#$%&"))},
      matrixRain, true);
  add(registry, "windows11_window", "Windows 11", "Windows11",
      {textParam("title", "Title", QString()), intParam("corner_radius", "Corner radius", 8, 0, 16),
       boolParam("dark_mode", "Dark mode", false), intParam("shadow_size", "Shadow size", 16, 0, 60),
       intParam("padding", "Padding", 20, 0, 100)},
      windows11, false, true);
  add(registry, "bevel", "Bevel", "Bevel",
      {intParam("size", "Size", 10, 1, 40), floatParam("strength", "Strength", 70, 0, 100),
       floatParam("light_angle", "Light angle", 225, 0, 360), colorParam("highlight_color", "Highlight color", QColor(255, 255, 255, 217)),
       colorParam("shadow_color", "Shadow color", QColor(0, 0, 0, 176))},
      bevel);
  add(registry, "stained_glass_border", "Stained glass border", "StainedGlassBorder",
      {intParam("border_size", "Border size", 60, 20, 300), floatParam("panel_width", "Panel width", 40, 10, 120),
       floatParam("lead_thickness", "Lead thickness", 3.5, 1, 12), floatParam("glass_brightness", "Glass brightness", 75, 30, 100),
       boolParam("glow", "Glass glow", true), boolParam("inner_line", "Inner line", true)},
      stainedGlassBorder, false, true);
  add(registry, "golden_border", "Golden border", "GoldenBorder",
      {intParam("size", "Size", 25, 5, 80), floatParam("bevel_strength", "Bevel strength", 60, 0, 100),
       colorParam("color", "Color", QColor(212, 175, 55)), boolParam("inner_border", "Inner border", true),
       boolParam("outer_border", "Outer border", true)},
      goldenBorder, false, true);
  add(registry, "nebula_starfield", "Nebula starfield", "NebulaStarfield",
      {floatParam("intensity", "Intensity", 70, 0, 100), floatParam("scale", "Scale", 80, 0, 100),
       floatParam("hue_shift", "Hue shift", -15, -180, 180), floatParam("star_density", "Star density", 55, 0, 100),
       floatParam("star_size", "Star size", 10, 0, 200), floatParam("twinkle", "Twinkle", 40, 0, 100),
       floatParam("vignette_strength", "Vignette strength", 18, 0, 100)},
      nebula, true);
  add(registry, "clouds", "Clouds", "Clouds",
      {floatParam("coverage", "Coverage", 55, 0, 100), floatParam("scale", "Scale", 72, 0, 100),
       floatParam("heightBias", "Height bias", 62, 0, 100), floatParam("softness", "Softness", 64, 0, 100),
       floatParam("sunlight", "Sunlight", 38, 0, 100)},
      clouds);
  add(registry, "blood_splash", "Blood splash", "BloodSplash",
      {floatParam("splash_amount", "Splash amount", 52, 0, 100), floatParam("drip_length", "Drip length", 48, 0, 100),
       floatParam("spread", "Spread", 42, 0, 100), floatParam("darkness", "Darkness", 58, 0, 100),
       floatParam("wet_shine", "Wet shine", 30, 0, 100)},
      bloodSplash, true);
}

}  // namespace xerahs::effects
