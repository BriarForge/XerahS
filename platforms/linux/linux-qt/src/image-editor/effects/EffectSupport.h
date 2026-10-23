// EDITOR-EFFECTS-001: helpers shared by the built-in effect files (internal; not part of the Effect.h contract).
// EE-003, EE-008, EE-009, EE-016, EE-018.
#pragma once

#include "Effect.h"

#include <QColorSpace>
#include <QImage>
#include <QString>
#include <array>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace xerahs::effects::support {

inline const QString kCancelled = QStringLiteral("effect-cancelled");
inline const QString kFailed = QStringLiteral("effect-failed");
inline const QString kInvalidInput = QStringLiteral("effect-invalid-input");
inline const QString kUnavailable = QStringLiteral("effect-unavailable");

// ---- parameter builders and accessors ---------------------------------------
inline ParamSpec floatParam(const QString &id, const QString &label, double initial, double minimum, double maximum) {
  return {id, label, ParamKind::Float, initial, minimum, maximum, {}};
}
inline ParamSpec intParam(const QString &id, const QString &label, int initial, int minimum, int maximum) {
  return {id, label, ParamKind::Int, initial, double(minimum), double(maximum), {}};
}
inline ParamSpec colorParam(const QString &id, const QString &label, const QColor &initial) {
  return {id, label, ParamKind::Color, initial, 0, 0, {}};
}
inline ParamSpec enumParam(const QString &id, const QString &label, const QString &initial, const QStringList &choices) {
  return {id, label, ParamKind::Enum, initial, 0, 0, choices};
}
inline ParamSpec boolParam(const QString &id, const QString &label, bool initial) {
  return {id, label, ParamKind::Bool, initial, 0, 0, {}};
}
inline ParamSpec textParam(const QString &id, const QString &label, const QString &initial) {
  return {id, label, ParamKind::Text, initial, 0, 0, {}};
}
inline ParamSpec filePathParam(const QString &id, const QString &label, const QString &initial = {}) {
  return {id, label, ParamKind::FilePath, initial, 0, 0, {}};
}
inline float floatOf(const ParamValues &params, const char *id) { return float(params.value(QLatin1String(id)).toDouble()); }
inline int intOf(const ParamValues &params, const char *id) { return params.value(QLatin1String(id)).toInt(); }
inline bool boolOf(const ParamValues &params, const char *id) { return params.value(QLatin1String(id)).toBool(); }
inline QColor colorOf(const ParamValues &params, const char *id) { return params.value(QLatin1String(id)).value<QColor>().toRgb(); }
inline QString choiceOf(const ParamValues &params, const char *id) { return params.value(QLatin1String(id)).toString(); }

// ---- row driver ---------------------------------------------------------------
// Polls cancellation before every row and reports monotonic progress lo..hi.
template <class RowFn>
bool runRows(const EffectContext &context, int height, int lo, int hi, RowFn rowFn) {
  context.progress(lo);
  int last = lo;
  for (int y = 0; y < height; ++y) {
    if (context.cancelled()) return false;
    rowFn(y);
    const int p = lo + int(qint64(y + 1) * (hi - lo) / height);
    if (p != last) {
      context.progress(p);
      last = p;
    }
  }
  return true;
}

// A new ARGB32 image the size of source carrying its colour space.
inline QImage makeResult(const QImage &source) {
  QImage result(source.size(), QImage::Format_ARGB32);
  if (!result.isNull()) result.setColorSpace(source.colorSpace());
  return result;
}

// ---- effect plumbing ----------------------------------------------------------
using ApplyFn = std::function<EffectResult(const QImage &, const ParamValues &, const EffectContext &)>;
class FunctionEffect final : public Effect {
public:
  FunctionEffect(EffectSpec spec, ApplyFn fn) : m_spec(std::move(spec)), m_fn(std::move(fn)) {}
  const EffectSpec &spec() const override { return m_spec; }
  EffectResult apply(const QImage &source, const ParamValues &params, const EffectContext &context) const override {
    try {
      return m_fn(source, params, context);
    } catch (...) {
      return {{}, kFailed};
    }
  }
private:
  EffectSpec m_spec;
  ApplyFn m_fn;
};

// ---- deterministic randomness (EE-008) ----------------------------------------
// SplitMix64 seeded straight from EffectContext::seed. Never time or a global generator.
class SplitMix64 {
public:
  explicit SplitMix64(quint64 seed) : m_state(seed) {}
  quint64 next() {
    quint64 z = (m_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  // Uniform in [0, bound); bound > 0. Rejection sampling avoids modulo bias.
  quint64 below(quint64 bound) {
    const quint64 threshold = (0 - bound) % bound;
    for (;;) {
      const quint64 value = next();
      if (value >= threshold) return value % bound;
    }
  }
  // C# Random.Next(min, maxExclusive) semantics (caller guarantees min < maxExclusive).
  int range(int min, int maxExclusive) { return min + int(below(quint64(qint64(maxExclusive) - min))); }
  // Uniform float in [0, 1).
  float unit() { return float(next() >> 40) * (1.0f / 16777216.0f); }
private:
  quint64 m_state;
};

// ---- convolution (the Skia matrix-convolution equivalent) ----------------------
// 3x3 kernel over the straight-alpha ARGB32 source with Skia's semantics: the kernel is
// centred (offset 1,1), samples outside the image clamp to the edge (tile mode Clamp), the
// weighted sum is scaled by gain and shifted by bias, then clamped to the byte gamut and
// rounded to the nearest byte. convolveAlpha convolves the alpha channel with the same
// kernel; otherwise the source alpha is copied unchanged.
EffectResult convolve3x3(const QImage &source, const std::array<float, 9> &kernel, float gain, float bias,
                         bool convolveAlpha, const EffectContext &context);

// ---- file inputs (EE-007, EE-018) -----------------------------------------------
constexpr int kMaxImageSide = 16384;
// Validates with QImageReader (readable, non-empty, at most kMaxImageSide per side) and decodes to
// Format_ARGB32. Returns a null image when the file is missing, unreadable or invalid.
QImage loadImageFile(const QString &path);
// Header-only validation (no pixel decode) with the same rules.
bool validateImageFile(const QString &path);

// ---- blur ------------------------------------------------------------------------
// Samples outside the image are transparent (Decal, Skia's default blur tile) or the
// edge pixel (Clamp).
enum class BlurBorder { Decal, Clamp };

// Separable Gaussian with a 3-sigma kernel on premultiplied pixels, unpremultiplied
// back to straight ARGB32. Skia SKImageFilter.CreateBlur is the same association; its
// internal window is not reproduced exactly. sigma <= 0 copies. A null image means
// cancellation or allocation failure.
QImage approximateGaussianBlur(const QImage &source, float sigma, BlurBorder border, const EffectContext &context,
                               int progressLo = 0, int progressHi = 100);
int xorshift(int state);
QRgb lerpColor(QRgb from, QRgb to, float t);
QRgb sourceOverTop(QRgb top, QRgb bottom);

// Pixel helpers retain their original arithmetic and distinct edge/rounding policies.
float clamp01(float v);
float lerp(float a, float b, float t);
float smoothStep(float e0, float e1, float v);
int clampInt(int v, int lo, int hi);

// Saturating nearest-even; NaN maps to zero.
int toByte(float v);
// Saturating half-up, kept separate from nearest-even.
int filteredByte(float value);
float hash01(int x, int y, int seed = 0);
float fade(float t);
float valueNoise(float x, float y, int seed = 0);
float fractalNoise(float x, float y, int octaves, float lacunarity, float gain, int seed = 0);
float luminance(QRgb c);
float luminance01(QRgb c);
float smoothStepRange(float edge0, float edge1, float x);

struct Pixels {
  int w = 0, h = 0;
  const QRgb *data = nullptr;
  explicit Pixels(const QImage &image) : w(image.width()), h(image.height()), data(reinterpret_cast<const QRgb *>(image.constBits())) {}
  QRgb at(int x, int y) const { return data[size_t(y) * size_t(w) + size_t(x)]; }
  QRgb clamped(int x, int y) const { return at(clampInt(x, 0, w - 1), clampInt(y, 0, h - 1)); }
};
QRgb bilinear(const Pixels &p, float x, float y);
QRgb bilinear(const QImage &image, float x, float y);

struct FloatImage {
  int w = 0, h = 0;
  std::vector<float> d;  // premultiplied r,g,b,a
};
FloatImage toPremul(const QImage &image);
QRgb fromPremulPixel(const float *v);
QImage fromPremul(const FloatImage &f, const QImage &like);

// Premultiplied 0..255; ceil(sigma * 3.f), distinct from the normalized blur above.
bool gaussianBlur255(FloatImage &f, float sigma, bool clampEdges, const EffectContext &context);
bool blurredClamp(const QImage &source, float radius, const EffectContext &context, QImage &out);
QImage copyOf(const QImage &source);
EffectResult identityCopy(const QImage &source, const EffectContext &context);
int seedOf(const EffectContext &context);
void hsvToRgb(float h, float s, float v, float &r, float &g, float &b);
QRgb fromHsv(float h, float s, float v);

// These differ for source values outside 0..1.
float screenClamped(float b, float s);
float screenOverlay(float source, float overlay);

// HSL variants retain their different hue arithmetic and rounded/truncated output.
void rgbToHsl(int r, int g, int b, float &h, float &s, float &l);
float hueToRgb(float p, float q, float t);
QRgb hslToRgb(float h, float s, float l, int a);
void toHsl(QRgb c, float &h, float &s, float &l);
QRgb fromHsl(float h, float s, float l, int alpha);
void boxHorizontal(const std::vector<float> &in, std::vector<float> &out, int width, int y, int left, int right);
void boxVertical(const std::vector<float> &in, std::vector<float> &out, int width, int height, int left, int right,
                 const EffectContext &context, int lo, int hi, bool &cancelled);

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

void fillRect(QImage &image, float left, float top, float right, float bottom, QRgb color);
void copyRows(QImage &dest, const QImage &source, int offsetX, int offsetY);
QImage borderedCanvas(const QImage &source, int border);
QRgb sourceOverBorder(QRgb dst, QRgb src);
void copyInterior(const QImage &source, QImage &out, int border);

template <class Op>
EffectResult paintBorder(const QImage &source, int border, const EffectContext &context, Op op,
                         const std::function<void(QImage &)> &post = nullptr) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage out = borderedCanvas(source, border);
  if (out.isNull()) return {{}, kFailed};
  const int nw = out.width(), nh = out.height();
  const bool done = runRows(context, nh, 0, 100, [&](int y) {
    auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
    const bool vertical = y < border || y >= border + source.height();
    for (int x = 0; x < nw; ++x) {
      if (!vertical && x >= border && x < border + source.width()) continue;
      dst[x] = op(x, y);
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  if (post) post(out);
  if (context.cancelled()) return {{}, kCancelled};
  copyInterior(source, out, border);
  return {out, {}};
}

}  // namespace xerahs::effects::support
