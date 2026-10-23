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

}  // namespace xerahs::effects::support
