// EDITOR-EFFECTS-001: EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); individual
// census source paths are recorded in EffectSpec::referencePath.
//
// The reference works on straight (unpremultiplied) 8-bit SKColor values with
// float maths and MathF.Round (round half to even); this file does the same on
// Format_ARGB32 pixels. Where the reference hands a colour matrix to Skia
// (offsets in 0..1 units, result clamped and rounded to the byte gamut), the
// same maths is applied directly, matching the pilot adjustments.
#include "Effect.h"
#include "EffectSupport.h"

#include <QColorSpace>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace xerahs::effects {
namespace {
const QString kReferenceDirectory = QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Adjustments/");
using namespace support;  // shared helpers: kCancelled, kFailed, parameter builders, runRows, FunctionEffect

// ---- numeric helpers (C# Math/MathF semantics) -----------------------------
float clamp01(float v) { return v <= 0.f ? 0.f : (v >= 1.f ? 1.f : v); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
// MathF.Round: half to even. NaN converts to 0 like a saturating C# byte cast.
int clampByte(float v) {
  if (std::isnan(v) || v <= 0.f) return 0;
  if (v >= 255.f) return 255;
  return int(std::nearbyintf(v));
}
float minNaN(float a, float b) { return (std::isnan(a) || std::isnan(b)) ? std::nanf("") : (a < b ? a : b); }
float maxNaN(float a, float b) { return (std::isnan(a) || std::isnan(b)) ? std::nanf("") : (a > b ? a : b); }
QRgb rgba(int r, int g, int b, int a) { return qRgba(r, g, b, a); }

using Matrix20 = std::array<float, 20>;
constexpr Matrix20 kIdentity{1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};

// Skia colour-matrix filter: straight 0..255 channels in, offsets in 0..1 units,
// result clamped to the byte gamut and rounded to nearest.
QRgb applyMatrix(const Matrix20 &m, QRgb pixel) {
  const float in[4]{float(qRed(pixel)), float(qGreen(pixel)), float(qBlue(pixel)), float(qAlpha(pixel))};
  int out[4];
  for (int row = 0; row < 4; ++row) {
    float value = m[row * 5 + 4] * 255.f;
    for (int col = 0; col < 4; ++col) value += m[row * 5 + col] * in[col];
    out[row] = int(std::floor(std::clamp(value, 0.f, 255.f) + 0.5f));
  }
  return qRgba(out[0], out[1], out[2], out[3]);
}
Matrix20 blendWithIdentity(const Matrix20 &target, float s) {
  Matrix20 result;
  for (int i = 0; i < 20; ++i) result[size_t(i)] = kIdentity[size_t(i)] * (1.f - s) + target[size_t(i)] * s;
  return result;
}

template <class Op>
EffectResult mapPixels(const QImage &source, const EffectContext &context, Op op, int lo = 0, int hi = 100) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, source.height(), lo, hi, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < source.width(); ++x) output[x] = op(x, y, input[x]);
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}
EffectResult identity(const QImage &source, const EffectContext &context) {
  return mapPixels(source, context, [](int, int, QRgb p) { return p; });
}

void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference, Mode mode,
         QVector<ParamSpec> parameters, ApplyFn fn, bool usesSeed = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Adjustments;
  spec.mode = mode;
  spec.parameters = std::move(parameters);
  spec.usesSeed = usesSeed;
  spec.referencePath = kReferenceDirectory + reference + QStringLiteral("ImageEffect.cs");
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}

// ---- individual effects ----------------------------------------------------
void findRange(const std::array<int, 256> &histogram, int clipCount, int &minimum, int &maximum) {
  minimum = 0;
  int sum = 0;
  for (int i = 0; i < 256; ++i) {
    sum += histogram[size_t(i)];
    if (sum > clipCount) {
      minimum = i;
      break;
    }
  }
  maximum = 255;
  sum = 0;
  for (int i = 255; i >= 0; --i) {
    sum += histogram[size_t(i)];
    if (sum > clipCount) {
      maximum = i;
      break;
    }
  }
}
int stretch(int value, int minimum, int maximum) {
  if (maximum <= minimum) return value;
  if (value <= minimum) return 0;
  if (value >= maximum) return 255;
  const float mapped = (value - minimum) * 255.f / (maximum - minimum);
  return int(std::nearbyintf(mapped));
}

EffectResult autoContrast(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float clipPercent = std::clamp(floatOf(params, "clip_percent"), 0.f, 20.f);
  const qint64 total = qint64(source.width()) * source.height();
  const int clipCount = int(std::nearbyintf(float(total) * (clipPercent / 100.f)));
  std::array<int, 256> histR{}, histG{}, histB{};
  if (!runRows(context, source.height(), 0, 50, [&](int y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
          ++histR[size_t(qRed(row[x]))];
          ++histG[size_t(qGreen(row[x]))];
          ++histB[size_t(qBlue(row[x]))];
        }
      }))
    return {{}, kCancelled};
  int minR, maxR, minG, maxG, minB, maxB;
  findRange(histR, clipCount, minR, maxR);
  findRange(histG, clipCount, minG, maxG);
  findRange(histB, clipCount, minB, maxB);
  const bool flat = maxR <= minR && maxG <= minG && maxB <= minB;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    if (flat) return p;
    return rgba(stretch(qRed(p), minR, maxR), stretch(qGreen(p), minG, maxG), stretch(qBlue(p), minB, maxB), qAlpha(p));
  }, 50, 100);
}

EffectResult blackAndWhite(const QImage &source, const ParamValues &, const EffectContext &context) {
  // The reference returns SKColors.White / Black, which are fully opaque.
  return mapPixels(source, context, [](int, int, QRgb p) {
    const float lum = 0.2126f * qRed(p) + 0.7152f * qGreen(p) + 0.0722f * qBlue(p);
    return lum > 127 ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 255);
  });
}

EffectResult channelSwap(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QString mode = choiceOf(params, "mode");
  return mapPixels(source, context, [&](int, int, QRgb p) {
    const int r = qRed(p), g = qGreen(p), b = qBlue(p), a = qAlpha(p);
    if (mode == QLatin1String("red_green")) return rgba(g, r, b, a);
    if (mode == QLatin1String("red_blue")) return rgba(b, g, r, a);
    if (mode == QLatin1String("green_blue")) return rgba(r, b, g, a);
    if (mode == QLatin1String("rotate_rgb")) return rgba(b, r, g, a);
    if (mode == QLatin1String("rotate_bgr")) return rgba(g, b, r, a);
    return p;
  });
}

// Box-blur pieces for the Gaussian approximation (three successive boxes, the
// W3C filter-effects recipe Skia also uses for large sigma). Edges are decal:
// samples outside the image are transparent.
void boxHorizontal(const std::vector<float> &in, std::vector<float> &out, int width, int y, int left, int right) {
  const float *src = in.data() + size_t(y) * size_t(width) * 4;
  float *dst = out.data() + size_t(y) * size_t(width) * 4;
  const float scale = 1.f / float(left + right + 1);
  for (int c = 0; c < 4; ++c) {
    double sum = 0;
    for (int k = 0; k <= std::min(right, width - 1); ++k) sum += src[k * 4 + c];
    for (int x = 0; x < width; ++x) {
      dst[x * 4 + c] = float(sum) * scale;
      if (x + right + 1 < width) sum += src[(x + right + 1) * 4 + c];
      if (x - left >= 0) sum -= src[(x - left) * 4 + c];
    }
  }
}
void boxVertical(const std::vector<float> &in, std::vector<float> &out, int width, int height, int left, int right,
                 const EffectContext &context, int lo, int hi, bool &cancelled) {
  std::vector<double> sums(size_t(width) * 4, 0.0);
  const size_t stride = size_t(width) * 4;
  for (int k = 0; k <= std::min(right, height - 1); ++k)
    for (size_t i = 0; i < stride; ++i) sums[i] += in[size_t(k) * stride + i];
  const float scale = 1.f / float(left + right + 1);
  cancelled = !runRows(context, height, lo, hi, [&](int y) {
    float *dst = out.data() + size_t(y) * stride;
    for (size_t i = 0; i < stride; ++i) dst[i] = float(sums[i]) * scale;
    if (y + right + 1 < height)
      for (size_t i = 0; i < stride; ++i) sums[i] += in[size_t(y + right + 1) * stride + i];
    if (y - left >= 0)
      for (size_t i = 0; i < stride; ++i) sums[i] -= in[size_t(y - left) * stride + i];
  });
}

EffectResult clarity(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float amount = std::clamp(floatOf(params, "amount"), -100.f, 100.f) / 100.f;
  if (std::fabs(amount) < 0.001f) return identity(source, context);
  const int width = source.width(), height = source.height();
  const float sigma = std::max(std::max(float(width), float(height)) * 0.02f, 3.f);
  // Premultiplied float copy: Skia blurs premultiplied pixels.
  std::vector<float> a(size_t(width) * size_t(height) * 4), b(a.size());
  for (int y = 0; y < height; ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const float alpha = qAlpha(row[x]) / 255.f;
      float *px = a.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
      px[0] = qRed(row[x]) / 255.f * alpha;
      px[1] = qGreen(row[x]) / 255.f * alpha;
      px[2] = qBlue(row[x]) / 255.f * alpha;
      px[3] = alpha;
    }
  }
  const int d = int(std::floor(sigma * 3.f * 2.5066283f / 4.f + 0.5f));  // sigma * 3 * sqrt(2 pi) / 4
  struct Box { int left, right; };
  const std::array<Box, 3> boxes = (d % 2 == 1) ? std::array<Box, 3>{{{d / 2, d / 2}, {d / 2, d / 2}, {d / 2, d / 2}}}
                                                 : std::array<Box, 3>{{{d / 2, d / 2 - 1}, {d / 2 - 1, d / 2}, {d / 2, d / 2}}};
  int pass = 0;
  const auto passLo = [&](int index) { return index * 60 / 6; };
  for (const Box &box : boxes) {
    if (!runRows(context, height, passLo(pass), passLo(pass + 1), [&](int y) { boxHorizontal(a, b, width, y, box.left, box.right); }))
      return {{}, kCancelled};
    std::swap(a, b);
    ++pass;
  }
  for (const Box &box : boxes) {
    bool cancelled = false;
    boxVertical(a, b, width, height, box.left, box.right, context, passLo(pass), passLo(pass + 1), cancelled);
    if (cancelled) return {{}, kCancelled};
    std::swap(a, b);
    ++pass;
  }
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const float *blur = a.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
    const float blurAlpha = blur[3];
    // Unpremultiply the blurred sample like SKBitmap.Pixels does.
    const float br = blurAlpha > 0.f ? clamp01(blur[0] / blurAlpha) : 0.f;
    const float bg = blurAlpha > 0.f ? clamp01(blur[1] / blurAlpha) : 0.f;
    const float bb = blurAlpha > 0.f ? clamp01(blur[2] / blurAlpha) : 0.f;
    const float sr = qRed(p) / 255.f, sg = qGreen(p) / 255.f, sb = qBlue(p) / 255.f;
    const float lum = 0.299f * sr + 0.587f * sg + 0.114f * sb;
    const float diff = lum - 0.5f;
    const float midtoneMask = std::exp(-8.f * diff * diff);
    const float strength = amount * midtoneMask * 1.5f;
    const float outR = std::clamp(sr + (sr - br) * strength, 0.f, 1.f);
    const float outG = std::clamp(sg + (sg - bg) * strength, 0.f, 1.f);
    const float outB = std::clamp(sb + (sb - bb) * strength, 0.f, 1.f);
    return rgba(int(std::nearbyintf(outR * 255.f)), int(std::nearbyintf(outG * 255.f)), int(std::nearbyintf(outB * 255.f)), qAlpha(p));
  }, 60, 100);
}

EffectResult colorBalance(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const char *ids[9]{"shadow_cyan_red", "shadow_magenta_green", "shadow_yellow_blue", "mid_cyan_red", "mid_magenta_green",
                     "mid_yellow_blue", "highlight_cyan_red", "highlight_magenta_green", "highlight_yellow_blue"};
  float v[9];
  bool allZero = true;
  for (int i = 0; i < 9; ++i) {
    v[i] = floatOf(params, ids[i]);
    allZero = allZero && std::fabs(v[i]) < 0.01f;
  }
  if (allZero) return identity(source, context);
  const float sCR = v[0] / 100.f, sMG = v[1] / 100.f, sYB = v[2] / 100.f;
  const float mCR = v[3] / 100.f, mMG = v[4] / 100.f, mYB = v[5] / 100.f;
  const float hCR = v[6] / 100.f, hMG = v[7] / 100.f, hYB = v[8] / 100.f;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    float r = qRed(p) / 255.f, g = qGreen(p) / 255.f, b = qBlue(p) / 255.f;
    const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
    const float shadowWeight = std::clamp(1.f - lum * 4.f, 0.f, 1.f);
    const float highlightWeight = std::clamp((lum - 0.75f) * 4.f, 0.f, 1.f);
    const float midWeight = std::max(1.f - shadowWeight - highlightWeight, 0.f);
    const float rShift = (sCR * shadowWeight + mCR * midWeight + hCR * highlightWeight) * 0.5f;
    const float gShift = (sMG * shadowWeight + mMG * midWeight + hMG * highlightWeight) * 0.5f;
    const float bShift = (sYB * shadowWeight + mYB * midWeight + hYB * highlightWeight) * 0.5f;
    r = std::clamp(r + rShift, 0.f, 1.f);
    g = std::clamp(g + gShift, 0.f, 1.f);
    b = std::clamp(b + bShift, 0.f, 1.f);
    return rgba(int(std::nearbyintf(r * 255.f)), int(std::nearbyintf(g * 255.f)), int(std::nearbyintf(b * 255.f)), qAlpha(p));
  });
}

EffectResult colorMatrix(const QImage &source, const ParamValues &params, const EffectContext &context) {
  static const char *ids[20]{"rr", "rg", "rb", "ra", "ro", "gr", "gg", "gb", "ga", "go",
                             "br", "bg", "bb", "ba", "bo", "ar", "ag", "ab", "aa", "ao"};
  Matrix20 m;
  for (int i = 0; i < 20; ++i) m[size_t(i)] = floatOf(params, ids[i]);
  return mapPixels(source, context, [&](int, int, QRgb p) { return applyMatrix(m, p); });
}

EffectResult colorize(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QColor tint = colorOf(params, "color");
  float strength = floatOf(params, "strength");
  if (strength <= 0) return identity(source, context);
  if (strength > 100) strength = 100;
  const bool full = strength >= 100;
  // Paint alpha for the partial path is (byte)(255 * (strength / 100f)).
  const float paintAlpha = full ? 1.f : float(int(255 * (strength / 100.f))) / 255.f;
  const float tr = tint.red() / 255.f, tg = tint.green() / 255.f, tb = tint.blue() / 255.f, ta = tint.alpha() / 255.f;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    const float sa = qAlpha(p) / 255.f;
    // Grayscale matrix then Modulate with the tint (straight channels, alpha multiplies).
    const float gray = std::clamp(0.2126f * qRed(p) + 0.7152f * qGreen(p) + 0.0722f * qBlue(p), 0.f, 255.f);
    const float layerR = gray * tr, layerG = gray * tg, layerB = gray * tb;
    const float layerAlpha = sa * ta * paintAlpha;
    float outAlpha = layerAlpha;
    float outR = layerR * layerAlpha, outG = layerG * layerAlpha, outB = layerB * layerAlpha;
    if (!full) {  // source drawn first, tinted copy composited over it (source-over)
      outAlpha = layerAlpha + sa * (1.f - layerAlpha);
      outR += qRed(p) * sa * (1.f - layerAlpha);
      outG += qGreen(p) * sa * (1.f - layerAlpha);
      outB += qBlue(p) * sa * (1.f - layerAlpha);
    }
    if (outAlpha <= 0.f) return qRgba(0, 0, 0, 0);
    return rgba(clampByte(outR / outAlpha), clampByte(outG / outAlpha), clampByte(outB / outAlpha), clampByte(outAlpha * 255.f));
  });
}

EffectResult crossProcess(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float s = std::clamp(floatOf(params, "strength") / 100.f, 0.f, 1.f);
  if (s <= 0.f) return identity(source, context);
  static const Matrix20 cross{1.2f, 0.1f, -0.1f, 0, 0.05f, -0.1f, 1.3f, 0.0f, 0, 0.02f,
                              -0.1f, 0.0f, 0.8f, 0, 0.1f, 0, 0, 0, 1, 0};
  const Matrix20 m = blendWithIdentity(cross, s);
  return mapPixels(source, context, [&](int, int, QRgb p) { return applyMatrix(m, p); });
}

EffectResult dehaze(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float amount = std::clamp(floatOf(params, "amount"), -100.f, 100.f) / 100.f;
  if (std::fabs(amount) < 0.001f) return identity(source, context);
  const qint64 count = qint64(source.width()) * source.height();
  const qint64 topN = std::max<qint64>(1, count / 1000);
  // The reference sorts (r+g+b)/3 and takes the topN-th largest; the integer sum is an equivalent key.
  std::array<qint64, 766> histogram{};
  if (!runRows(context, source.height(), 0, 33, [&](int y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) ++histogram[size_t(qRed(row[x]) + qGreen(row[x]) + qBlue(row[x]))];
      }))
    return {{}, kCancelled};
  int thresholdKey = 0;
  qint64 cumulative = 0;
  for (int key = 765; key >= 0; --key) {
    cumulative += histogram[size_t(key)];
    if (cumulative >= topN) {
      thresholdKey = key;
      break;
    }
  }
  float sumR = 0, sumG = 0, sumB = 0;  // float accumulation in pixel order, as the reference
  int atmCount = 0;
  if (!runRows(context, source.height(), 33, 66, [&](int y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
          if (qRed(row[x]) + qGreen(row[x]) + qBlue(row[x]) >= thresholdKey) {
            sumR += qRed(row[x]);
            sumG += qGreen(row[x]);
            sumB += qBlue(row[x]);
            ++atmCount;
          }
        }
      }))
    return {{}, kCancelled};
  const float atmR = atmCount > 0 ? sumR / float(atmCount) : 200.f;
  const float atmG = atmCount > 0 ? sumG / float(atmCount) : 200.f;
  const float atmB = atmCount > 0 ? sumB / float(atmCount) : 200.f;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    float r = qRed(p), g = qGreen(p), b = qBlue(p);
    if (amount > 0.f) {
      const float darkChannel = minNaN(r / atmR, minNaN(g / atmG, b / atmB));
      float transmission = 1.f - amount * darkChannel;
      transmission = maxNaN(transmission, 0.1f);
      r = (r - atmR * (1.f - transmission)) / transmission;
      g = (g - atmG * (1.f - transmission)) / transmission;
      b = (b - atmB * (1.f - transmission)) / transmission;
    } else {
      const float haze = -amount;
      r = r + (atmR - r) * haze;
      g = g + (atmG - g) * haze;
      b = b + (atmB - b) * haze;
    }
    return rgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(p));
  }, 66, 100);
}

EffectResult duotone(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QColor shadows = colorOf(params, "shadow_color"), mids = colorOf(params, "midtone_color"),
               highs = colorOf(params, "highlight_color");
  // Internal clamps follow the reference (gamma 0.3..3) even though the slider spans 0.1..5.
  const float contrast = std::clamp(floatOf(params, "contrast"), 50.f, 200.f) / 100.f;
  const float gamma = std::clamp(floatOf(params, "gamma"), 0.3f, 3.f);
  const float blend = std::clamp(floatOf(params, "blend"), 0.f, 100.f) / 100.f;
  const auto lerpColor = [](const QColor &a, const QColor &b, float t, int out[3]) {
    out[0] = clampByte(lerp(float(a.red()), float(b.red()), t));
    out[1] = clampByte(lerp(float(a.green()), float(b.green()), t));
    out[2] = clampByte(lerp(float(a.blue()), float(b.blue()), t));
  };
  return mapPixels(source, context, [&](int, int, QRgb p) {
    float lum = (0.2126f * qRed(p) + 0.7152f * qGreen(p) + 0.0722f * qBlue(p)) / 255.f;
    lum = std::clamp((lum - 0.5f) * contrast + 0.5f, 0.f, 1.f);
    lum = std::pow(lum, 1.f / gamma);
    const float t = clamp01(lum);
    int mapped[3];
    if (t <= 0.5f) lerpColor(shadows, mids, t * 2.f, mapped);
    else lerpColor(mids, highs, (t - 0.5f) * 2.f, mapped);
    return rgba(clampByte(lerp(float(qRed(p)), float(mapped[0]), blend)),
                clampByte(lerp(float(qGreen(p)), float(mapped[1]), blend)),
                clampByte(lerp(float(qBlue(p)), float(mapped[2]), blend)), qAlpha(p));
  });
}

EffectResult exposure(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = std::clamp(floatOf(params, "amount"), -10.f, 10.f);
  if (std::fabs(amount) < 0.0001f) return identity(source, context);
  const float gain = std::pow(2.f, amount);
  return mapPixels(source, context, [&](int, int, QRgb p) {
    return rgba(clampByte(qRed(p) * gain), clampByte(qGreen(p) * gain), clampByte(qBlue(p) * gain), qAlpha(p));
  });
}

float hash01(int x, int y, int seed) {
  quint32 h = 2166136261u;
  h = (h ^ quint32(x)) * 16777619u;
  h = (h ^ quint32(y)) * 16777619u;
  h = (h ^ quint32(seed)) * 16777619u;
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return float(h & 0x00FFFFFFu) / 16777215.f;
}
void filmPreset(const QString &preset, float r, float g, float b, float &tr, float &tg, float &tb) {
  if (preset == QLatin1String("warm")) {
    tr = r * 1.10f + g * 0.015f;
    tg = g * 1.04f;
    tb = b * 0.88f + r * 0.01f;
  } else if (preset == QLatin1String("cool")) {
    tr = r * 0.94f + b * 0.02f;
    tg = g * 1.01f + b * 0.01f;
    tb = b * 1.10f + g * 0.01f;
  } else if (preset == QLatin1String("faded")) {
    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    tr = lerp(r, lum + 0.04f, 0.22f);
    tg = lerp(g, lum + 0.03f, 0.18f);
    tb = lerp(b, lum - 0.01f, 0.26f);
  } else if (preset == QLatin1String("cross_processed")) {
    tr = std::pow(std::max(r, 0.0001f), 0.88f) * 1.08f;
    tg = std::pow(std::max(g, 0.0001f), 1.05f) * 0.98f;
    tb = std::pow(std::max(b, 0.0001f), 0.82f) * 1.12f;
  } else {
    tr = r * 1.05f + g * 0.02f;
    tg = g * 1.02f + r * 0.01f;
    tb = b * 0.94f + g * 0.02f;
  }
  tr = clamp01(tr);
  tg = clamp01(tg);
  tb = clamp01(tb);
}

EffectResult filmEmulation(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QString preset = choiceOf(params, "preset");
  const float tone = std::clamp(floatOf(params, "tone_strength"), 0.f, 100.f) / 100.f;
  const float grain = std::clamp(floatOf(params, "grain_amount"), 0.f, 100.f) / 100.f;
  const float fade = std::clamp(floatOf(params, "fade_amount"), 0.f, 100.f) / 100.f;
  const float contrast = std::clamp(floatOf(params, "contrast_amount"), 50.f, 150.f) / 100.f;
  if (tone <= 0.f && grain <= 0.f && fade <= 0.f && std::fabs(contrast - 1.f) < 0.0001f) return identity(source, context);
  // The reference's fixed grain seed 1977; context.seed (0 by default) is folded in so the default matches it.
  const quint32 folded = quint32(context.seed ^ (context.seed >> 32));
  const int seed = int(1977u ^ folded);
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    float r = qRed(p) / 255.f, g = qGreen(p) / 255.f, b = qBlue(p) / 255.f;
    float tr, tg, tb;
    filmPreset(preset, r, g, b, tr, tg, tb);
    r = lerp(r, tr, tone);
    g = lerp(g, tg, tone);
    b = lerp(b, tb, tone);
    r = (r - 0.5f) * contrast + 0.5f;
    g = (g - 0.5f) * contrast + 0.5f;
    b = (b - 0.5f) * contrast + 0.5f;
    const float fadeScale = 1.f - fade * 0.35f;
    const float blackLift = fade * 0.06f;
    r = r * fadeScale + blackLift;
    g = g * fadeScale + blackLift;
    b = b * fadeScale + blackLift;
    if (grain > 0.f) {
      const float n0 = hash01(x, y, seed) * 2.f - 1.f;
      const float n1 = hash01(x, y, seed ^ 117) * 2.f - 1.f;
      const float n2 = hash01(x, y, seed ^ 911) * 2.f - 1.f;
      const float amplitude = grain * 0.10f;
      r += n0 * amplitude;
      g += n1 * amplitude;
      b += n2 * amplitude;
    }
    return rgba(clampByte(clamp01(r) * 255.f), clampByte(clamp01(g) * 255.f), clampByte(clamp01(b) * 255.f), qAlpha(p));
  });
}

EffectResult gamma(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = floatOf(params, "amount");
  // Skia table filter: table built in float and truncated (not rounded) to a byte.
  std::array<int, 256> table;
  for (int i = 0; i < 256; ++i) {
    const float value = float(i) / 255.f;
    const float corrected = float(std::pow(double(value), 1.0 / double(amount)));
    table[size_t(i)] = int(std::max(0.f, std::min(1.f, corrected)) * 255.f);
  }
  return mapPixels(source, context, [&](int, int, QRgb p) {
    return rgba(table[size_t(qRed(p))], table[size_t(qGreen(p))], table[size_t(qBlue(p))], qAlpha(p));
  });
}

EffectResult hue(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float radians = float(double(floatOf(params, "amount")) * M_PI / 180.0);
  const float c = float(std::cos(double(radians)));
  const float s = float(std::sin(double(radians)));
  const Matrix20 m{0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f, 0, 0,
                   0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f, 0, 0,
                   0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f, 0, 0,
                   0, 0, 0, 1, 0};
  return mapPixels(source, context, [&](int, int, QRgb p) { return applyMatrix(m, p); });
}

EffectResult levels(const QImage &source, const ParamValues &params, const EffectContext &context) {
  int inBlack = std::clamp(intOf(params, "input_black"), 0, 255);
  int inWhite = std::clamp(intOf(params, "input_white"), 0, 255);
  int outBlack = std::clamp(intOf(params, "output_black"), 0, 255);
  int outWhite = std::clamp(intOf(params, "output_white"), 0, 255);
  const float gammaValue = std::clamp(floatOf(params, "gamma"), 0.1f, 5.f);
  if (inWhite <= inBlack) inWhite = std::min(255, inBlack + 1);
  if (outWhite < outBlack) std::swap(outBlack, outWhite);
  if (inBlack == 0 && inWhite == 255 && std::fabs(gammaValue - 1.f) < 0.0001f && outBlack == 0 && outWhite == 255)
    return identity(source, context);
  const float inRange = float(inWhite - inBlack), outRange = float(outWhite - outBlack);
  const auto map = [&](int value) {
    float normalized = float(value - inBlack) / inRange;
    normalized = std::clamp(normalized, 0.f, 1.f);
    const float corrected = std::pow(normalized, gammaValue);
    return clampByte(float(outBlack) + corrected * outRange);
  };
  return mapPixels(source, context, [&](int, int, QRgb p) { return rgba(map(qRed(p)), map(qGreen(p)), map(qBlue(p)), qAlpha(p)); });
}

EffectResult lomo(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float s = std::clamp(floatOf(params, "strength") / 100.f, 0.f, 1.f);
  if (s <= 0.f) return identity(source, context);
  static const Matrix20 lomoMatrix{1.3f, 0.05f, -0.05f, 0, 0.02f, -0.02f, 1.1f, -0.02f, 0, 0.01f,
                                   -0.05f, 0.0f, 0.9f, 0, -0.02f, 0, 0, 0, 1, 0};
  const Matrix20 m = blendWithIdentity(lomoMatrix, s);
  const int w = source.width(), h = source.height();
  const float cx = w / 2.f, cy = h / 2.f;
  const float radius = std::sqrt(float(w * w + h * h)) * 0.5f;
  const float edgeAlpha = float(int(180 * s)) / 255.f;
  return mapPixels(source, context, [&](int x, int y, QRgb p) {
    const QRgb colored = applyMatrix(m, p);
    // Radial gradient transparent at 0.5 of the radius to black (edgeAlpha) at 1.0, sampled at the pixel centre.
    const float dx = (float(x) + 0.5f) - cx, dy = (float(y) + 0.5f) - cy;
    const float t = std::sqrt(dx * dx + dy * dy) / radius;
    const float u = std::clamp((t - 0.5f) / 0.5f, 0.f, 1.f);
    const float va = edgeAlpha * u;
    const float ca = qAlpha(colored) / 255.f;
    const float outAlpha = va + ca * (1.f - va);  // black source-over the matrix result
    if (outAlpha <= 0.f) return qRgba(0, 0, 0, 0);
    const float k = ca * (1.f - va) / outAlpha;
    return rgba(clampByte(qRed(colored) * k), clampByte(qGreen(colored) * k), clampByte(qBlue(colored) * k), clampByte(outAlpha * 255.f));
  });
}

EffectResult polaroid(const QImage &source, const ParamValues &, const EffectContext &context) {
  static const Matrix20 m{1.438f, -0.062f, -0.062f, 0, 0, -0.122f, 1.378f, -0.122f, 0, 0,
                          -0.016f, -0.016f, 1.483f, 0, 0, 0, 0, 0, 1, 0};
  return mapPixels(source, context, [&](int, int, QRgb p) { return applyMatrix(m, p); });
}

EffectResult posterize(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int levelCount = std::clamp(intOf(params, "levels"), 2, 64);
  const float scale = float(levelCount - 1);
  const auto quantize = [&](int value) {
    const float bucket = std::nearbyintf(float(value) * scale / 255.f);
    return clampByte(bucket * 255.f / scale);
  };
  return mapPixels(source, context, [&](int, int, QRgb p) {
    return rgba(quantize(qRed(p)), quantize(qGreen(p)), quantize(qBlue(p)), qAlpha(p));
  });
}

EffectResult replaceColor(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QColor target = colorOf(params, "target_color"), replacement = colorOf(params, "replace_color");
  const int tolerance = int(floatOf(params, "tolerance") * 2.55f);
  const QRgb replacementPixel = qRgba(replacement.red(), replacement.green(), replacement.blue(), replacement.alpha());
  return mapPixels(source, context, [&](int, int, QRgb p) {
    const bool match = std::abs(qRed(p) - target.red()) <= tolerance && std::abs(qGreen(p) - target.green()) <= tolerance &&
                       std::abs(qBlue(p) - target.blue()) <= tolerance && std::abs(qAlpha(p) - target.alpha()) <= tolerance;
    return match ? replacementPixel : p;
  });
}

EffectResult sepia(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float s = std::clamp(floatOf(params, "strength") / 100.f, 0.f, 1.f);
  if (s <= 0.f) return identity(source, context);
  static const Matrix20 sepiaMatrix{0.393f, 0.769f, 0.189f, 0, 0, 0.349f, 0.686f, 0.168f, 0, 0,
                                    0.272f, 0.534f, 0.131f, 0, 0, 0, 0, 0, 1, 0};
  const Matrix20 m = blendWithIdentity(sepiaMatrix, s);
  return mapPixels(source, context, [&](int, int, QRgb p) { return applyMatrix(m, p); });
}

EffectResult shadowsHighlights(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float shadows = std::clamp(floatOf(params, "shadows"), -100.f, 100.f);
  const float highlights = std::clamp(floatOf(params, "highlights"), -100.f, 100.f);
  if (std::fabs(shadows) < 0.0001f && std::fabs(highlights) < 0.0001f) return identity(source, context);
  const float sStrength = shadows / 100.f, hStrength = highlights / 100.f;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    const float luma = (0.2126f * qRed(p) + 0.7152f * qGreen(p) + 0.0722f * qBlue(p)) / 255.f;
    const float shadowWeight = (1.f - luma) * (1.f - luma);
    const float highlightWeight = luma * luma;
    const float delta = (sStrength * shadowWeight - hStrength * highlightWeight) * 255.f;
    return rgba(clampByte(qRed(p) + delta), clampByte(qGreen(p) + delta), clampByte(qBlue(p) + delta), qAlpha(p));
  });
}

EffectResult solarize(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int threshold = std::clamp(intOf(params, "threshold"), 0, 255);
  const auto flip = [&](int v) { return v > threshold ? 255 - v : v; };
  return mapPixels(source, context, [&](int, int, QRgb p) { return rgba(flip(qRed(p)), flip(qGreen(p)), flip(qBlue(p)), qAlpha(p)); });
}

float smoothStep(float edge0, float edge1, float x) {
  const float range = edge1 - edge0;
  if (range <= 0.f) return x >= edge1 ? 1.f : 0.f;
  const float t = std::clamp((x - edge0) / range, 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}
float softLight(float base, float blend) { return (1.f - 2.f * blend) * base * base + 2.f * blend * base; }

EffectResult splitToning(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QColor shadowTint = colorOf(params, "shadow_tint"), highlightTint = colorOf(params, "highlight_tint");
  const float strength = std::clamp(floatOf(params, "strength"), 0.f, 100.f) / 100.f;
  if (strength < 0.001f) return identity(source, context);
  const float balance = std::clamp(floatOf(params, "balance"), -100.f, 100.f) / 200.f + 0.5f;
  const float sR = shadowTint.red() / 255.f, sG = shadowTint.green() / 255.f, sB = shadowTint.blue() / 255.f;
  const float hR = highlightTint.red() / 255.f, hG = highlightTint.green() / 255.f, hB = highlightTint.blue() / 255.f;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    float r = qRed(p) / 255.f, g = qGreen(p) / 255.f, b = qBlue(p) / 255.f;
    const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
    const float shadowWeight = 1.f - smoothStep(0.f, balance, lum);
    const float highlightWeight = smoothStep(balance, 1.f, lum);
    const float tintR = sR * shadowWeight + hR * highlightWeight;
    const float tintG = sG * shadowWeight + hG * highlightWeight;
    const float tintB = sB * shadowWeight + hB * highlightWeight;
    r = lerp(r, softLight(r, tintR), strength);
    g = lerp(g, softLight(g, tintG), strength);
    b = lerp(b, softLight(b, tintB), strength);
    return rgba(clampByte(r * 255.f), clampByte(g * 255.f), clampByte(b * 255.f), qAlpha(p));
  });
}

EffectResult temperatureTint(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float temperature = std::clamp(floatOf(params, "temperature"), -100.f, 100.f);
  const float tint = std::clamp(floatOf(params, "tint"), -100.f, 100.f);
  if (std::fabs(temperature) < 0.0001f && std::fabs(tint) < 0.0001f) return identity(source, context);
  const float tempDelta = temperature / 100.f * 64.f, tintDelta = tint / 100.f * 64.f;
  return mapPixels(source, context, [&](int, int, QRgb p) {
    return rgba(clampByte(qRed(p) + tempDelta - tintDelta * 0.25f), clampByte(qGreen(p) + tintDelta),
                clampByte(qBlue(p) - tempDelta - tintDelta * 0.25f), qAlpha(p));
  });
}

EffectResult threshold(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int limit = std::clamp(intOf(params, "value"), 0, 255);
  return mapPixels(source, context, [&](int, int, QRgb p) {
    const int luma = (qRed(p) * 77 + qGreen(p) * 150 + qBlue(p) * 29) >> 8;
    const int bw = luma >= limit ? 255 : 0;
    return rgba(bw, bw, bw, qAlpha(p));
  });
}

EffectResult vibrance(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float amount = std::clamp(floatOf(params, "amount"), -100.f, 100.f) / 100.f;
  if (std::fabs(amount) < 0.0001f) return identity(source, context);
  return mapPixels(source, context, [&](int, int, QRgb p) {
    float r = qRed(p), g = qGreen(p), b = qBlue(p);
    const float maximum = std::max(r, std::max(g, b)), minimum = std::min(r, std::min(g, b));
    const float saturation = (maximum - minimum) / 255.f;
    const float gray = (r + g + b) / 3.f;
    const float factor = amount >= 0.f ? 1.f + amount * (1.f - saturation) : 1.f + amount;
    r = gray + (r - gray) * factor;
    g = gray + (g - gray) * factor;
    b = gray + (b - gray) * factor;
    return rgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(p));
  });
}
}  // namespace

void registerMoreAdjustmentEffects(EffectRegistry &registry) {
  const auto P = Mode::Previewable;
  add(registry, "auto_contrast", "Auto contrast", "AutoContrast", P,
      {floatParam("clip_percent", "Clip percent", 0.5, 0, 20)}, autoContrast);
  add(registry, "black_and_white", "Black & White", "BlackAndWhite", Mode::Immediate, {}, blackAndWhite);
  add(registry, "channel_swap", "Channel swap", "ChannelSwap", P,
      {enumParam("mode", "Swap mode", "red_green", {"red_green", "red_blue", "green_blue", "rotate_rgb", "rotate_bgr"})},
      channelSwap);
  add(registry, "clarity", "Clarity", "Clarity", P, {floatParam("amount", "Amount", 0, -100, 100)}, clarity);
  add(registry, "color_balance", "Color Balance", "ColorBalance", P,
      {floatParam("shadow_cyan_red", "Shadows: Cyan-Red", 0, -100, 100),
       floatParam("shadow_magenta_green", "Shadows: Magenta-Green", 0, -100, 100),
       floatParam("shadow_yellow_blue", "Shadows: Yellow-Blue", 0, -100, 100),
       floatParam("mid_cyan_red", "Midtones: Cyan-Red", 0, -100, 100),
       floatParam("mid_magenta_green", "Midtones: Magenta-Green", 0, -100, 100),
       floatParam("mid_yellow_blue", "Midtones: Yellow-Blue", 0, -100, 100),
       floatParam("highlight_cyan_red", "Highlights: Cyan-Red", 0, -100, 100),
       floatParam("highlight_magenta_green", "Highlights: Magenta-Green", 0, -100, 100),
       floatParam("highlight_yellow_blue", "Highlights: Yellow-Blue", 0, -100, 100)},
      colorBalance);
  {
    QVector<ParamSpec> cells;
    const char *rows = "rgba";
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 5; ++col) {
        const QString id = QString(QLatin1Char(rows[row])) + (col < 4 ? QString(QLatin1Char(rows[col])) : QStringLiteral("o"));
        const QString label = id.left(1).toUpper() + id.mid(1);
        cells.append(floatParam(id, label, row == col ? 1 : 0, col == 4 ? -255 : -5, col == 4 ? 255 : 5));
      }
    }
    add(registry, "color_matrix", "Color matrix", "ColorMatrix", P, cells, colorMatrix);
  }
  add(registry, "colorize", "Colorize", "Colorize", P,
      {colorParam("color", "Color", QColor(255, 165, 0)), floatParam("strength", "Strength", 50, 0, 100)}, colorize);
  add(registry, "cross_process", "Cross process", "CrossProcess", P, {floatParam("strength", "Strength", 100, 0, 100)}, crossProcess);
  add(registry, "dehaze", "Dehaze", "Dehaze", P, {floatParam("amount", "Amount", 50, -100, 100)}, dehaze);
  add(registry, "duotone_gradient_map", "Duotone / Gradient map", "DuotoneGradientMap", P,
      {colorParam("shadow_color", "Shadow color", QColor(24, 28, 78)),
       colorParam("midtone_color", "Midtone color", QColor(182, 60, 132)),
       colorParam("highlight_color", "Highlight color", QColor(255, 224, 132)),
       floatParam("contrast", "Contrast", 110, 50, 200), floatParam("gamma", "Gamma", 1, 0.1, 5),
       floatParam("blend", "Blend", 100, 0, 100)},
      duotone);
  add(registry, "exposure", "Exposure", "Exposure", P, {floatParam("amount", "Exposure", 0, -100, 100)}, exposure);
  add(registry, "film_emulation", "Film emulation", "FilmEmulation", P,
      {enumParam("preset", "Preset", "classic", {"classic", "warm", "cool", "faded", "cross_processed"}),
       floatParam("tone_strength", "Tone strength", 65, 0, 100), floatParam("grain_amount", "Grain amount", 12, 0, 100),
       floatParam("fade_amount", "Fade amount", 10, 0, 100), floatParam("contrast_amount", "Contrast amount", 110, 50, 150)},
      filmEmulation, true);
  add(registry, "gamma", "Gamma", "Gamma", P, {floatParam("amount", "Amount", 1, 0.1, 5)}, gamma);
  add(registry, "hue", "Hue", "Hue", P, {floatParam("amount", "Amount", 0, -180, 180)}, hue);
  add(registry, "levels", "Levels", "Levels", P,
      {intParam("input_black", "Input black", 0, 0, 255), intParam("input_white", "Input white", 255, 0, 255),
       floatParam("gamma", "Gamma", 1, 0.1, 5), intParam("output_black", "Output black", 0, 0, 255),
       intParam("output_white", "Output white", 255, 0, 255)},
      levels);
  add(registry, "lomo", "Lomo", "Lomo", P, {floatParam("strength", "Strength", 100, 0, 100)}, lomo);
  add(registry, "polaroid", "Polaroid", "Polaroid", Mode::Immediate, {}, polaroid);
  add(registry, "posterize", "Posterize", "Posterize", P, {intParam("levels", "Levels", 8, 2, 64)}, posterize);
  add(registry, "replace_color", "Replace Color", "ReplaceColor", P,
      {colorParam("target_color", "Target color", QColor(255, 255, 255)),
       colorParam("replace_color", "Replace color", QColor(0, 0, 0)), floatParam("tolerance", "Tolerance", 40, 0, 255)},
      replaceColor);
  add(registry, "sepia", "Sepia", "Sepia", P, {floatParam("strength", "Strength", 100, 0, 100)}, sepia);
  add(registry, "shadows_highlights", "Shadows / Highlights", "ShadowsHighlights", P,
      {floatParam("shadows", "Shadows", 0, -100, 100), floatParam("highlights", "Highlights", 0, -100, 100)},
      shadowsHighlights);
  add(registry, "solarize", "Solarize", "Solarize", P, {intParam("threshold", "Threshold", 128, 0, 255)}, solarize);
  add(registry, "split_toning", "Split Toning", "SplitToning", P,
      {colorParam("shadow_tint", "Shadow Tint", QColor(40, 50, 120)),
       colorParam("highlight_tint", "Highlight Tint", QColor(200, 170, 100)),
       floatParam("strength", "Strength", 30, 0, 100), floatParam("balance", "Balance", 0, -100, 100)},
      splitToning);
  add(registry, "temperature_tint", "Temperature / Tint", "TemperatureTint", P,
      {floatParam("temperature", "Temperature", 0, -100, 100), floatParam("tint", "Tint", 0, -100, 100)}, temperatureTint);
  add(registry, "threshold", "Threshold", "Threshold", P, {intParam("value", "Threshold", 128, 0, 255)}, threshold);
  add(registry, "vibrance", "Vibrance", "Vibrance", P, {floatParam("amount", "Amount", 0, -100, 100)}, vibrance);
}
}  // namespace xerahs::effects
