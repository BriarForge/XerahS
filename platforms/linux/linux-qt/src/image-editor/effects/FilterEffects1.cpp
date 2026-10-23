// EDITOR-EFFECTS-001: the Filters category, first batch (the simple filters).
// EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census
// source path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Randomness comes only from EffectContext::seed (SplitMix64); the reference's fixed
//    Random(42) in frosted_glass and tv_static becomes the context seed.
//  - The 3x3 convolutions run on the straight-alpha channels of the ARGB32 pixel contract;
//    Skia convolves premultiplied channels, which differs only where the source is translucent.
//  - Pixelate uses the nearest-neighbour rule Skia's SKFilterMode.Nearest applies (the
//    destination pixel centre picks the source texel it covers) written out as integer maths.
#include "Effect.h"
#include "EffectSupport.h"

#include <QColorSpace>
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;  // shared helpers: kCancelled, kFailed, parameter builders, runRows, FunctionEffect
const QString kReferenceDirectory = QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");

// ---- shared helpers -------------------------------------------------------------
QImage copyOf(const QImage &source) {
  QImage result = source.copy();
  result.setColorSpace(source.colorSpace());
  return result;
}

// Skia clamps a filtered channel to the byte gamut and rounds it to the nearest byte.
int filteredByte(float value) { return int(std::floor(std::clamp(value, 0.f, 255.f) + 0.5f)); }

// Source-over of a black line with the given 0..255 alpha over a straight-alpha pixel.
// Skia draws the line in premultiplied space; this is the same blend for straight pixels.
QRgb blackOver(QRgb source, int lineAlpha) {
  if (lineAlpha <= 0) return source;
  const double sa = qAlpha(source) / 255.0;
  const double la = lineAlpha / 255.0;
  const double outAlpha = la + sa * (1.0 - la);
  if (outAlpha <= 0.0) return 0;
  const double weight = sa * (1.0 - la) / outAlpha;
  return qRgba(filteredByte(float(qRed(source) * weight)), filteredByte(float(qGreen(source) * weight)),
               filteredByte(float(qBlue(source) * weight)), filteredByte(float(outAlpha * 255.0)));
}

// Nearest-neighbour sampling: the centre of the destination texel picks the source texel
// it covers, which is what Skia's SKFilterMode.Nearest does.
int nearestSample(int destination, int destinationSpan, int sourceSpan) {
  const double position = (double(destination) + 0.5) * double(sourceSpan) / double(destinationSpan);
  return std::clamp(int(std::floor(position)), 0, sourceSpan - 1);
}

void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference, Mode mode,
         QVector<ParamSpec> parameters, ApplyFn fn, bool usesSeed = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Filters;
  spec.mode = mode;
  spec.parameters = std::move(parameters);
  spec.usesSeed = usesSeed;
  spec.referencePath = kReferenceDirectory + reference;
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}

// ---- individual effects ----------------------------------------------------------
// Emboss: the reference's 3x3 kernel with gain 1 and bias 127 (ConvolutionHelper.Apply3x3).
EffectResult emboss(const QImage &source, const ParamValues &, const EffectContext &context) {
  return convolve3x3(source, {-1.f, 0.f, -1.f, 0.f, 4.f, 0.f, -1.f, 0.f, -1.f}, 1.f, 127.f, false, context);
}

// Mean removal: the reference's 3x3 kernel with gain 1 and no bias.
EffectResult meanRemoval(const QImage &source, const ParamValues &, const EffectContext &context) {
  return convolve3x3(source, {-1.f, -1.f, -1.f, -1.f, 9.f, -1.f, -1.f, -1.f, -1.f}, 1.f, 0.f, false, context);
}

// Smooth: the reference's 3x3 box kernel of 1/9 per sample.
EffectResult smooth(const QImage &source, const ParamValues &, const EffectContext &context) {
  constexpr float n = 1.f / 9.f;
  return convolve3x3(source, {n, n, n, n, n, n, n, n, n}, 1.f, 0.f, false, context);
}

// Edge detect: the reference's 3x3 kernel with gain 1 and bias 127.
EffectResult edgeDetect(const QImage &source, const ParamValues &, const EffectContext &context) {
  return convolve3x3(source, {-1.f, -1.f, -1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f}, 1.f, 127.f, false, context);
}

// Frosted glass: every pixel takes a random nearby pixel, clamped to the image. The
// reference offsets x and y by Random.Next(-radius, radius + 1) independently.
EffectResult frostedGlass(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int radius = std::max(1, intOf(params, "radius"));
  const int width = source.width();
  const int height = source.height();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  SplitMix64 rng(context.seed);
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const int ox = std::clamp(x + rng.range(-radius, radius + 1), 0, width - 1);
      const int oy = std::clamp(y + rng.range(-radius, radius + 1), 0, height - 1);
      output[x] = reinterpret_cast<const QRgb *>(source.constScanLine(oy))[ox];
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// Scanlines: a black one-pixel line every "spacing" rows, drawn over the source with the
// given opacity. The reference truncates the line alpha to a byte.
EffectResult scanline(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float opacity = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  if (opacity <= 0.f) return {copyOf(source), {}};
  const int step = std::max(2, intOf(params, "spacing"));
  const int lineAlpha = int(255.f * opacity);
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const int width = source.width();
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    if (y % step != 0) {
      std::memcpy(output, input, size_t(width) * 4);
      return;
    }
    for (int x = 0; x < width; ++x) output[x] = blackOver(input[x], lineAlpha);
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// TV static: every channel is blended towards one random grey noise byte per pixel.
EffectResult tvStatic(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float blend = std::clamp(floatOf(params, "amount") / 100.f, 0.f, 1.f);
  if (blend <= 0.f) return {copyOf(source), {}};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  SplitMix64 rng(context.seed);
  const int width = source.width();
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb pixel = input[x];
      const int noise = int(rng.below(256));
      // The reference truncates the blended byte.
      const int r = int(qRed(pixel) + (noise - qRed(pixel)) * blend);
      const int g = int(qGreen(pixel) + (noise - qGreen(pixel)) * blend);
      const int b = int(qBlue(pixel) + (noise - qBlue(pixel)) * blend);
      output[x] = qRgba(r, g, b, qAlpha(pixel));
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// Palette map: each channel is quantised to "levels" steps of 255 / (levels - 1).
EffectResult paletteMap(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int levels = std::clamp(intOf(params, "levels"), 2, 32);
  const float step = 255.f / float(levels - 1);
  auto quantize = [&](int value) {
    const int index = std::clamp(int(std::nearbyintf(float(value) / step)), 0, levels - 1);
    return std::clamp(int(index * step), 0, 255);
  };
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const int width = source.width();
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb pixel = input[x];
      output[x] = qRgba(quantize(qRed(pixel)), quantize(qGreen(pixel)), quantize(qBlue(pixel)), qAlpha(pixel));
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// Pixelate: the image is reduced to a width/size by height/size grid with nearest
// sampling and scaled back up with the same rule, which squares the pixels off.
EffectResult pixelate(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int size = intOf(params, "size");
  if (size <= 1) return {copyOf(source), {}};
  const int width = source.width();
  const int height = source.height();
  const int smallWidth = std::max(1, width / size);
  const int smallHeight = std::max(1, height / size);
  std::vector<QRgb> grid(size_t(smallWidth) * size_t(smallHeight));
  bool done = runRows(context, smallHeight, 0, 50, [&](int j) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(nearestSample(j, smallHeight, height)));
    for (int i = 0; i < smallWidth; ++i)
      grid[size_t(j) * size_t(smallWidth) + size_t(i)] = input[nearestSample(i, smallWidth, width)];
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  done = runRows(context, height, 50, 100, [&](int y) {
    const QRgb *input = grid.data() + size_t(nearestSample(y, height, smallHeight)) * size_t(smallWidth);
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) output[x] = input[nearestSample(x, width, smallWidth)];
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// Color depth: each channel is reduced to "bitsPerChannel" bits by snapping it to the
// nearest multiple of 255 / (2^bits - 1).
EffectResult colorDepth(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int bits = std::clamp(intOf(params, "bitsPerChannel"), 1, 8);
  if (bits == 8) return {copyOf(source), {}};
  const double interval = 255.0 / (std::pow(2.0, bits) - 1.0);
  auto remap = [&](int value) {
    return int(std::nearbyint(std::nearbyint(double(value) / interval) * interval));
  };
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const int width = source.width();
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb pixel = input[x];
      output[x] = qRgba(remap(qRed(pixel)), remap(qGreen(pixel)), remap(qBlue(pixel)), qAlpha(pixel));
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// Sharpen: the reference's four-neighbour Laplacian kernel scaled by the strength. The
// reference convolves alpha too, so this one does as well.
EffectResult sharpen(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int strength = intOf(params, "strength");
  if (strength <= 0) return {copyOf(source), {}};
  const float s = std::clamp(float(strength) / 100.f, 0.f, 1.f);
  const float centre = 1.f + 4.f * s;
  const float edge = -s;
  return convolve3x3(source, {0.f, edge, 0.f, edge, centre, edge, 0.f, edge, 0.f}, 1.f, 0.f, true, context);
}
}  // namespace

void registerFilterEffects1(EffectRegistry &registry) {
  add(registry, "emboss", "Emboss", "EmbossImageEffect.cs", Mode::Immediate, {}, emboss);
  add(registry, "mean_removal", "Mean removal", "MeanRemovalImageEffect.cs", Mode::Immediate, {}, meanRemoval);
  add(registry, "smooth", "Smooth", "SmoothImageEffect.cs", Mode::Immediate, {}, smooth);
  add(registry, "edge_detect", "Edge detect", "EdgeDetectImageEffect.cs", Mode::Immediate, {}, edgeDetect);
  add(registry, "frosted_glass", "Frosted glass", "FrostedGlassImageEffect.cs", Mode::Previewable,
      {intParam("radius", "Radius", 5, 1, 20)}, frostedGlass, true);
  add(registry, "scanline", "Scanlines", "ScanlineImageEffect.cs", Mode::Previewable,
      {intParam("spacing", "Line spacing", 4, 2, 20), floatParam("opacity", "Opacity", 50, 0, 100)}, scanline);
  add(registry, "tv_static", "TV static", "TVStaticImageEffect.cs", Mode::Previewable,
      {floatParam("amount", "Amount", 40, 0, 100)}, tvStatic, true);
  add(registry, "palette_map", "Palette map", "PaletteMapImageEffect.cs", Mode::Previewable,
      {intParam("levels", "Color levels", 6, 2, 32)}, paletteMap);
  add(registry, "pixelate", "Pixelate", "PixelateImageEffect.cs", Mode::Previewable,
      {intParam("size", "Size", 10, 1, 200)}, pixelate);
  add(registry, "color_depth", "Color depth", "ColorDepthImageEffect.cs", Mode::Previewable,
      {intParam("bitsPerChannel", "Bits per channel", 4, 1, 8)}, colorDepth);
  add(registry, "sharpen", "Sharpen", "SharpenImageEffect.cs", Mode::Previewable,
      {intParam("strength", "Strength", 50, 0, 100)}, sharpen);
}
}  // namespace xerahs::effects
