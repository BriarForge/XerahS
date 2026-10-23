// EDITOR-EFFECTS-001: the Filters category, batch 4. EE-003, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census source
// path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Skia's blur (SKImageFilter.CreateBlur) is a true separable Gaussian with the same sigma over
//    a transparent surround; Skia switches to box-blur passes for large sigma. Measured against the
//    reference's own SkiaSharp build the two agree within two levels per channel.
//  - The rainbow_border inner line is a QPainter antialiased stroke instead of Skia's. The pixels
//    agree with the reference except at the corners of the stroke, where the coverage differs by a
//    few levels.
//  - Where the reference has a settable Seed property (crystalize_shards 4242,
//    holographic_foil_shimmer 2026, crt 1337) the hash seed is that default XOR the low/high
//    halves of EffectContext::seed, so seed 0 reproduces the reference and other seeds vary it
//    (usesSeed). blueprint_drawing's Random.Shared.Next(1, int.MaxValue) is SplitMix64 from
//    EffectContext::seed. The fixed hashes in woodcut_print and minecraft_grass stay fixed and
//    those effects do not use the seed.
//  - SkiaSharp SKColor.FromHsv is reimplemented from its documented formula (hue in degrees,
//    saturation and value in 0..100, truncated to bytes); it agrees with the reference byte for byte.
//  - SkiaSharp's SKCanvas.DrawRect takes a width and a height, as the reference intends, so the
//    border and block rectangles are ported literally.
#include "Effect.h"
#include "EffectSupport.h"

#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;
const QString kReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");

// ---- result images --------------------------------------------------------------

QImage makeSizedResult(const QImage &source, int width, int height) {
  QImage result(width, height, QImage::Format_ARGB32);
  if (!result.isNull()) result.setColorSpace(source.colorSpace());
  return result;
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

// ---- anamorphic_lens_flare ------------------------------------------------------
EffectResult anamorphicLensFlare(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float intensity = floatOf(params, "intensity") / 100.f;
  const float threshold = floatOf(params, "threshold") / 100.f;
  const float streakLength = floatOf(params, "streak_length") / 100.f;
  const float warmth = floatOf(params, "warmth") / 100.f;
  const float ghosting = floatOf(params, "ghosting") / 100.f;
  if (intensity <= 0.0001f) return {copyOf(source), {}};
  const int w = source.width(), h = source.height();
  const float centerX = float(w - 1) * 0.5f, centerY = float(h - 1) * 0.5f;
  const float sigma = 0.14f + streakLength * 0.22f;
  const float sigmaInv = 1.f / std::max(0.0001f, 2.f * sigma * sigma);
  const float maxOffset = 10.f + streakLength * 120.f;
  const int steps = 5 + int(std::nearbyintf(streakLength * 4.f));
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const Pixels src(source);
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    const float yNorm = (float(y) - centerY) / std::max(1.f, float(h - 1));
    const float streakProfile = 0.62f + 0.38f * std::exp(-(yNorm * yNorm) * sigmaInv);
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb c = src.at(x, y);
      const float sr = float(qRed(c)) / 255.f, sg = float(qGreen(c)) / 255.f, sb = float(qBlue(c)) / 255.f;
      float flare = 0.f;
      for (int i = -steps; i <= steps; ++i) {
        const float t = float(i) / float(std::max(1, steps));
        const float offset = t * maxOffset;
        const QRgb sample = bilinear(src, float(x) + offset, float(y));
        const float lum = luminance(sample) / 255.f;
        const float bright = clamp01((lum - threshold) / std::max(0.0001f, 1.f - threshold));
        const float weight = 1.f / (1.f + (float(std::abs(i)) * 0.7f));
        flare += bright * weight;
      }
      const float ghostX = float(w - 1 - x) + ((float(x) - centerX) * (0.40f + ghosting * 0.85f));
      const QRgb ghostSample = bilinear(src, ghostX, float(y));
      const float ghostLum = luminance(ghostSample) / 255.f;
      float ghost = clamp01((ghostLum - (threshold * 0.82f)) / std::max(0.0001f, 1.f - (threshold * 0.82f)));
      ghost *= intensity * ghosting * 0.55f * streakProfile;
      const float flareStrength = flare * intensity * 0.12f * streakProfile;
      const float warmR = 1.f;
      const float warmG = lerp(1.f, 0.86f, warmth);
      const float warmB = lerp(1.f, 0.58f, warmth);
      const float ghostR = lerp(0.88f, 1.f, warmth);
      const float ghostG = 0.82f;
      const float ghostB = lerp(1.f, 0.74f, warmth);
      const float overlayR = flareStrength * warmR + ghost * ghostR;
      const float overlayG = flareStrength * warmG + ghost * ghostG;
      const float overlayB = flareStrength * warmB + ghost * ghostB;
      line[x] = qRgba(toByte(screenOverlay(sr, overlayR) * 255.f), toByte(screenOverlay(sg, overlayG) * 255.f),
                      toByte(screenOverlay(sb, overlayB) * 255.f), qAlpha(c));
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- crystalize_shards ----------------------------------------------------------
EffectResult crystalizeShards(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float cellSize = float(clampInt(intOf(params, "shardSize"), 6, 80));
  const float invCell = 1.f / cellSize;
  const float jitter = floatOf(params, "jitter") / 100.f;
  const float edgeStrength = floatOf(params, "edgeStrength") / 100.f;
  const float shine = floatOf(params, "shine") / 100.f;
  const int seed = 4242 ^ seedOf(context);
  const int w = source.width(), h = source.height();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const Pixels src(source);
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const float fx = float(x) * invCell;
      const float fy = float(y) * invCell;
      const int cx = int(std::floor(fx));
      const int cy = int(std::floor(fy));
      float nearestDist = std::numeric_limits<float>::max();
      float secondDist = std::numeric_limits<float>::max();
      float nearestSeedX = float(x), nearestSeedY = float(y);
      int nearestCellX = cx, nearestCellY = cy;
      for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
          const int cellX = cx + ox, cellY = cy + oy;
          const float jx = ((hash01(cellX, cellY, seed) * 2.f) - 1.f) * 0.45f * jitter;
          const float jy = ((hash01(cellX, cellY, seed ^ 911) * 2.f) - 1.f) * 0.45f * jitter;
          const float seedX = (float(cellX) + 0.5f + jx) * cellSize;
          const float seedY = (float(cellY) + 0.5f + jy) * cellSize;
          const float dx = seedX - float(x), dy = seedY - float(y);
          const float dist = dx * dx + dy * dy;
          if (dist < nearestDist) {
            secondDist = nearestDist;
            nearestDist = dist;
            nearestSeedX = seedX;
            nearestSeedY = seedY;
            nearestCellX = cellX;
            nearestCellY = cellY;
          } else if (dist < secondDist) {
            secondDist = dist;
          }
        }
      }
      const QRgb sample = bilinear(src, nearestSeedX, nearestSeedY);
      float r = float(qRed(sample)), g = float(qGreen(sample)), b = float(qBlue(sample));
      const int a = qAlpha(sample);
      const float nearest = std::sqrt(std::max(0.f, nearestDist));
      const float second = std::sqrt(std::max(0.f, secondDist));
      const float borderDiff = second - nearest;
      float edge = 1.f - smoothStep(0.f, cellSize * 0.12f, borderDiff);
      edge *= edgeStrength;
      const float facetNoise = hash01(nearestCellX, nearestCellY, seed ^ 1777);
      const float centerFactor = 1.f - std::min(1.f, nearest / std::max(1.f, cellSize));
      const float shade = 1.f + ((facetNoise - 0.5f) * 0.25f) + (centerFactor * shine * 0.45f);
      r *= shade;
      g *= shade;
      b *= shade;
      if (edge > 0.001f) {
        const float edgeDark = edge * 0.78f;
        r = lerp(r, r * 0.22f, edgeDark);
        g = lerp(g, g * 0.26f, edgeDark);
        b = lerp(b, b * 0.34f, edgeDark);
        const float glint = edge * (0.20f + (facetNoise * 0.25f));
        r += 38.f * glint;
        g += 52.f * glint;
        b += 80.f * glint;
      }
      line[x] = qRgba(toByte(r), toByte(g), toByte(b), a);
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- inner_shadow ---------------------------------------------------------------
// Separable Gaussian over one float channel; samples outside the image are transparent,
// which matches the reference's expanded transparent canvas around the shifted alpha mask.
std::vector<float> blurChannel(const std::vector<float> &input, int w, int h, float sigma,
                               const EffectContext &context, bool &cancelled) {
  cancelled = false;
  std::vector<float> output(input.size(), 0.f);
  if (sigma <= 0.0001f) {
    output = input;
    return output;
  }
  const int radius = std::max(1, int(std::ceil(sigma * 3.f)));
  std::vector<float> kernel(size_t(radius) * 2 + 1);
  float sum = 0.f;
  for (int i = -radius; i <= radius; ++i) {
    const float v = std::exp(-float(i * i) / (2.f * sigma * sigma));
    kernel[size_t(i + radius)] = v;
    sum += v;
  }
  for (float &v : kernel) v /= sum;
  std::vector<float> temp(input.size(), 0.f);
  for (int y = 0; y < h; ++y) {
    if (context.cancelled()) {
      cancelled = true;
      return output;
    }
    for (int x = 0; x < w; ++x) {
      float acc = 0.f;
      for (int k = -radius; k <= radius; ++k) {
        const int sx = x + k;
        if (sx < 0 || sx >= w) continue;
        acc += input[size_t(y) * size_t(w) + size_t(sx)] * kernel[size_t(k + radius)];
      }
      temp[size_t(y) * size_t(w) + size_t(x)] = acc;
    }
  }
  for (int y = 0; y < h; ++y) {
    if (context.cancelled()) {
      cancelled = true;
      return output;
    }
    for (int x = 0; x < w; ++x) {
      float acc = 0.f;
      for (int k = -radius; k <= radius; ++k) {
        const int sy = y + k;
        if (sy < 0 || sy >= h) continue;
        acc += temp[size_t(sy) * size_t(w) + size_t(x)] * kernel[size_t(k + radius)];
      }
      output[size_t(y) * size_t(w) + size_t(x)] = acc;
    }
  }
  return output;
}

EffectResult innerShadow(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float opacity = floatOf(params, "opacity") / 100.f;
  const int size = clampInt(intOf(params, "size"), 0, 300);
  const int offsetX = intOf(params, "offset_x"), offsetY = intOf(params, "offset_y");
  const QColor color = colorOf(params, "color");
  if (opacity <= 0.f || (size <= 0 && offsetX == 0 && offsetY == 0) || color.alpha() == 0)
    return {copyOf(source), {}};
  const int w = source.width(), h = source.height();
  if (w <= 0 || h <= 0) return {copyOf(source), {}};
  const size_t count = size_t(w) * size_t(h);
  std::vector<float> mask(count, 0.f);
  for (int y = 0; y < h; ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    for (int x = 0; x < w; ++x) mask[size_t(y) * size_t(w) + size_t(x)] = float(qAlpha(line[x])) / 255.f;
  }
  // The reference draws the mask at (-offsetX, -offsetY), so dest(x, y) = src(x + offsetX, y + offsetY).
  std::vector<float> shifted(count, 0.f);
  for (int y = 0; y < h; ++y) {
    const int sy = y + offsetY;
    if (sy < 0 || sy >= h) continue;
    for (int x = 0; x < w; ++x) {
      const int sx = x + offsetX;
      if (sx < 0 || sx >= w) continue;
      shifted[size_t(y) * size_t(w) + size_t(x)] = mask[size_t(sy) * size_t(w) + size_t(sx)];
    }
  }
  std::vector<float> blurred = shifted;
  if (size > 0) {
    const float radius = float(size) / 2.f;
    const float sigma = std::max(0.001f, radius / 3.f);
    bool cancelled = false;
    blurred = blurChannel(shifted, w, h, sigma, context, cancelled);
    if (cancelled) return {{}, kCancelled};
  }
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const float colorAlpha = float(color.alpha()) / 255.f;
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb src = reinterpret_cast<const QRgb *>(source.constScanLine(y))[x];
      const float sourceAlpha = float(qAlpha(src)) / 255.f;
      if (sourceAlpha <= 0.f) {
        line[x] = src;
        continue;
      }
      const float shiftedAlpha = blurred[size_t(y) * size_t(w) + size_t(x)];
      const float innerCoverage = clamp01(sourceAlpha - shiftedAlpha);
      const float shadowMix = innerCoverage * opacity * colorAlpha;
      line[x] = qRgba(toByte(lerp(float(qRed(src)), float(color.red()), shadowMix)),
                      toByte(lerp(float(qGreen(src)), float(color.green()), shadowMix)),
                      toByte(lerp(float(qBlue(src)), float(color.blue()), shadowMix)), qAlpha(src));
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- blueprint_drawing ----------------------------------------------------------
EffectResult blueprintDrawing(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float lineStrength = floatOf(params, "line_strength") / 100.f;
  const float detail = floatOf(params, "detail") / 100.f;
  const float grid = floatOf(params, "grid_intensity") / 100.f;
  const float texture = floatOf(params, "texture") / 100.f;
  const float glow = floatOf(params, "glow") / 100.f;
  // The reference draws a fresh random seed per run; SplitMix64 keeps it deterministic per seed.
  const int seed = SplitMix64(context.seed).range(1, std::numeric_limits<int>::max());
  const int w = source.width(), h = source.height();
  const int right = w - 1, bottom = h - 1;
  const float centerX = float(right) * 0.5f, centerY = float(bottom) * 0.5f;
  const float invSpacing = 1.f / (10.f + ((1.f - detail) * 20.f));
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const Pixels src(source);
  std::vector<float> luminanceMap(size_t(w) * size_t(h), 0.f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) luminanceMap[size_t(y) * size_t(w) + size_t(x)] = luminance01(src.at(x, y));
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    const int yUp = y > 0 ? y - 1 : 0;
    const int yDown = y < bottom ? y + 1 : bottom;
    for (int x = 0; x < w; ++x) {
      const int xLeft = x > 0 ? x - 1 : 0;
      const int xRight = x < right ? x + 1 : right;
      const size_t index = size_t(y) * size_t(w) + size_t(x);
      const float lum = luminanceMap[index];
      const float gradX = std::fabs(luminanceMap[size_t(y) * size_t(w) + size_t(xRight)] -
                                    luminanceMap[size_t(y) * size_t(w) + size_t(xLeft)]);
      const float gradY = std::fabs(luminanceMap[size_t(yDown) * size_t(w) + size_t(x)] -
                                    luminanceMap[size_t(yUp) * size_t(w) + size_t(x)]);
      const float edge = clamp01((gradX + gradY) * (1.7f + (lineStrength * 2.4f)));
      const float tonalLine = std::pow(1.f - lum, 0.65f + ((1.f - detail) * 1.5f)) * (0.2f + (detail * 0.35f));
      const float lineMask = clamp01((edge * (0.72f + (lineStrength * 0.75f))) + tonalLine);
      const float nx = float(x) / std::max(1.f, float(right));
      const float ny = float(y) / std::max(1.f, float(bottom));
      const float dx = (float(x) - centerX) / std::max(1.f, float(right));
      const float dy = (float(y) - centerY) / std::max(1.f, float(bottom));
      const float dist = std::sqrt((dx * dx) + (dy * dy));
      const float vignette = 1.f - smoothStep(0.35f, 0.95f, dist);
      const float baseShade = 0.42f + (0.38f * vignette);
      const float gridX = std::fabs((float(x) * invSpacing) - std::nearbyintf(float(x) * invSpacing));
      const float gridY = std::fabs((float(y) * invSpacing) - std::nearbyintf(float(y) * invSpacing));
      float gridLine = std::max(1.f - (gridX * 18.f), 1.f - (gridY * 18.f));
      gridLine = clamp01(gridLine) * grid * 0.45f;
      const float paperNoiseA = ((hash01(int(nx * 1024.f), int(ny * 1024.f), seed) * 2.f) - 1.f) * texture;
      const float paperNoiseB = ((hash01(int(nx * 320.f), int(ny * 320.f), seed ^ 431) * 2.f) - 1.f) * texture * 0.6f;
      const float paper = (paperNoiseA + paperNoiseB) * 12.f;
      const float br = 10.f + (12.f * baseShade) + (16.f * gridLine) + paper;
      const float bg = 52.f + (46.f * baseShade) + (28.f * gridLine) + paper;
      const float bb = 122.f + (88.f * baseShade) + (46.f * gridLine) + paper;
      const float glowMix = lineMask * (0.55f + (glow * 0.45f));
      float r = lerp(br, 230.f, glowMix);
      float g = lerp(bg, 242.f, glowMix);
      float b = lerp(bb, 255.f, glowMix);
      const float cyanEdge = lineMask * glow * 0.28f;
      r = lerp(r, 136.f, cyanEdge);
      g = lerp(g, 224.f, cyanEdge);
      b = lerp(b, 255.f, cyanEdge);
      line[x] = qRgba(toByte(r), toByte(g), toByte(b), qAlpha(src.at(x, y)));
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- holographic_foil_shimmer ----------------------------------------------------
EffectResult holographicFoilShimmer(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float intensity = floatOf(params, "intensity") / 100.f;
  const float scale = std::clamp(floatOf(params, "scale"), 40.f, 300.f);
  const float invScale = 1.f / scale;
  const float phase = std::clamp(floatOf(params, "shift"), 0.f, 360.f) * (3.14159265358979f / 180.f);
  const float specular = floatOf(params, "specular") / 100.f;
  const float grain = floatOf(params, "grain") / 100.f;
  if (intensity <= 0.0001f) return {copyOf(source), {}};
  const int seed = 2026 ^ seedOf(context);
  const int w = source.width(), h = source.height();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const Pixels src(source);
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb c = src.at(x, y);
      const float sr = float(qRed(c)) / 255.f, sg = float(qGreen(c)) / 255.f, sb = float(qBlue(c)) / 255.f;
      const int sa = qAlpha(c);
      const float lum = (0.2126f * sr) + (0.7152f * sg) + (0.0722f * sb);
      const float u = float(x) * invScale;
      const float v = float(y) * invScale;
      const float waveA = std::sin((u * 2.7f) + (v * 1.9f) + phase);
      const float waveB = std::cos((u * 3.6f) - (v * 2.2f) - (phase * 0.7f));
      const float waveC = std::sin(((u + v) * 4.1f) + (phase * 1.3f));
      const float wave = (waveA * 0.45f) + (waveB * 0.35f) + (waveC * 0.2f);
      float hue = wave * 0.5f + 0.5f + (u * 0.08f) + (v * 0.04f);
      hue -= std::floor(hue);
      const float sat = 0.65f + (0.25f * lum);
      const float val = 0.80f + (0.20f * lum);
      float hr = 0.f, hg = 0.f, hb = 0.f;
      hsvToRgb(hue, sat, val, hr, hg, hb);
      const float scan = 0.5f + (0.5f * std::sin(((u + (v * 0.35f)) * 35.f) + (phase * 2.f)));
      const float shimmer = intensity * ((scan * 0.55f) + 0.45f);
      float specBand = std::sin((u * 8.f) - (v * 5.f) + (phase * 1.5f));
      specBand = std::pow(std::max(0.f, specBand), 8.f) * specular;
      float overlayR = (hr * shimmer) + specBand;
      float overlayG = (hg * shimmer) + (specBand * 0.92f);
      float overlayB = (hb * shimmer) + (specBand * 1.08f);
      if (grain > 0.001f) {
        const float micro = hash01(x, y, seed);
        const float sparkle = std::pow(micro, 22.f) * grain * 1.8f;
        overlayR += sparkle;
        overlayG += sparkle * 0.95f;
        overlayB += sparkle * 1.1f;
      }
      const float blend = intensity * (0.42f + (0.58f * lum));
      const float screenedR = 1.f - ((1.f - sr) * (1.f - std::min(1.f, overlayR)));
      const float screenedG = 1.f - ((1.f - sg) * (1.f - std::min(1.f, overlayG)));
      const float screenedB = 1.f - ((1.f - sb) * (1.f - std::min(1.f, overlayB)));
      const float outR = lerp(sr, screenedR, blend);
      const float outG = lerp(sg, screenedG, blend);
      const float outB = lerp(sb, screenedB, blend);
      line[x] = qRgba(toByte(outR * 255.f), toByte(outG * 255.f), toByte(outB * 255.f), sa);
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- woodcut_print ---------------------------------------------------------------
EffectResult woodcutPrint(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int w = source.width(), h = source.height();
  const float threshold = floatOf(params, "threshold") / 100.f;
  const float edgeWeight = floatOf(params, "edge_strength") / 100.f;
  const float grainWeight = floatOf(params, "grain_amount") / 100.f;
  const QColor ink = colorOf(params, "ink_color");
  const QColor paper = colorOf(params, "paper_color");
  const size_t count = size_t(w) * size_t(h);
  std::vector<float> lum(count, 0.f);
  for (int y = 0; y < h; ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb c = line[x];
      lum[size_t(y) * size_t(w) + size_t(x)] =
          (0.299f * float(qRed(c)) + 0.587f * float(qGreen(c)) + 0.114f * float(qBlue(c))) / 255.f;
    }
  }
  // Sobel magnitude; the reference leaves the outer ring without an edge value.
  std::vector<float> edges(count, 0.f);
  for (int y = 1; y < h - 1; ++y) {
    for (int x = 1; x < w - 1; ++x) {
      const float gx = -lum[size_t(y - 1) * size_t(w) + size_t(x - 1)] + lum[size_t(y - 1) * size_t(w) + size_t(x + 1)] -
                       2.f * lum[size_t(y) * size_t(w) + size_t(x - 1)] + 2.f * lum[size_t(y) * size_t(w) + size_t(x + 1)] -
                       lum[size_t(y + 1) * size_t(w) + size_t(x - 1)] + lum[size_t(y + 1) * size_t(w) + size_t(x + 1)];
      const float gy = -lum[size_t(y - 1) * size_t(w) + size_t(x - 1)] - 2.f * lum[size_t(y - 1) * size_t(w) + size_t(x)] -
                       lum[size_t(y - 1) * size_t(w) + size_t(x + 1)] + lum[size_t(y + 1) * size_t(w) + size_t(x - 1)] +
                       2.f * lum[size_t(y + 1) * size_t(w) + size_t(x)] + lum[size_t(y + 1) * size_t(w) + size_t(x + 1)];
      edges[size_t(y) * size_t(w) + size_t(x)] = std::sqrt(gx * gx + gy * gy);
    }
  }
  std::vector<float> grain(count, 0.f);
  if (grainWeight > 0.001f) {
    // The reference seeds a fixed xorshift, so the grain does not depend on the effect seed.
    uint32_t rng = 7919u;
    std::vector<float> rowPhase(size_t(h), 0.f);
    for (int y = 0; y < h; ++y) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      rowPhase[size_t(y)] = float(rng & 0x7FFFFFFFu) / float(std::numeric_limits<int>::max()) * 6.28318530717959f;
    }
    for (int y = 0; y < h; ++y) {
      const float phase = rowPhase[size_t(y)];
      for (int x = 0; x < w; ++x) {
        const float wave = std::sin(float(x) * 0.08f + phase) * 0.5f + 0.5f;
        const float fineWave = std::sin(float(x) * 0.3f + float(y) * 0.15f + phase * 2.f) * 0.5f + 0.5f;
        grain[size_t(y) * size_t(w) + size_t(x)] = wave * 0.6f + fineWave * 0.4f;
      }
    }
  }
  const float inkR = float(ink.red()) / 255.f, inkG = float(ink.green()) / 255.f, inkB = float(ink.blue()) / 255.f;
  const float papR = float(paper.red()) / 255.f, papG = float(paper.green()) / 255.f, papB = float(paper.blue()) / 255.f;
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const size_t index = size_t(y) * size_t(w) + size_t(x);
      const float l = lum[index];
      const float e = edges[index];
      const float g = grain[index];
      const float localThreshold = threshold + (g - 0.5f) * grainWeight * 0.4f;
      const float score = l - e * edgeWeight;
      float isInk = 0.f;
      if (score < localThreshold - 0.05f) isInk = 1.f;
      else if (score > localThreshold + 0.05f) isInk = 0.f;
      else isInk = 1.f - (score - (localThreshold - 0.05f)) / 0.1f;
      const float outR = inkR * isInk + papR * (1.f - isInk);
      const float outG = inkG * isInk + papG * (1.f - isInk);
      const float outB = inkB * isInk + papB * (1.f - isInk);
      line[x] = qRgba(toByte(clamp01(outR) * 255.f), toByte(clamp01(outG) * 255.f), toByte(clamp01(outB) * 255.f),
                      qAlpha(reinterpret_cast<const QRgb *>(source.constScanLine(y))[x]));
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- rainbow_border ---------------------------------------------------------------
float perimeterHue(int x, int y, int newW, int newH, float totalPerim, float perimW, float perimH, float hueOffset) {
  const float clampX = std::clamp(float(x), 0.f, float(newW - 1));
  const float clampY = std::clamp(float(y), 0.f, float(newH - 1));
  const float distTop = clampY;
  const float distBottom = float(newH - 1) - clampY;
  const float distLeft = clampX;
  const float distRight = float(newW - 1) - clampX;
  const float minDist = std::min(std::min(distTop, distBottom), std::min(distLeft, distRight));
  float along = 0.f;
  if (minDist == distTop) along = clampX;
  else if (minDist == distRight) along = perimW + clampY;
  else if (minDist == distBottom) along = perimW + perimH + (float(newW - 1) - clampX);
  else along = perimW + perimH + float(newW - 1) + (float(newH - 1) - clampY);
  const float t = along / totalPerim;
  return std::fmod(t * 360.f + hueOffset, 360.f);
}

float crossRatio(int x, int y, int border, int srcW, int srcH) {
  const float innerL = float(border);
  const float innerT = float(border);
  const float innerR = float(border + srcW);
  const float innerB = float(border + srcH);
  const float clampX = std::clamp(float(x), innerL, innerR - 1.f);
  const float clampY = std::clamp(float(y), innerT, innerB - 1.f);
  const float dx = float(x) - clampX, dy = float(y) - clampY;
  return clamp01(std::sqrt(dx * dx + dy * dy) / float(border));
}

EffectResult rainbowBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int border = clampInt(intOf(params, "border_size"), 2, 300);
  const float sat = std::clamp(floatOf(params, "saturation"), 0.f, 100.f);
  const float brightness = std::clamp(floatOf(params, "brightness"), 0.f, 100.f);
  const float hueOff = std::fmod(floatOf(params, "hue_offset"), 360.f);
  const float glow = std::clamp(floatOf(params, "glow_strength"), 0.f, 100.f) / 100.f;
  const bool innerLine = boolOf(params, "inner_line");
  const int w = source.width(), h = source.height();
  const int newWidth = w + border * 2;
  const int newHeight = h + border * 2;
  const float perimW = float(w) + float(border) * 2.f;
  const float perimH = float(h) + float(border) * 2.f;
  const float totalPerim = 2.f * (perimW + perimH - 4.f);
  QImage result = makeSizedResult(source, newWidth, newHeight);
  if (result.isNull()) return {{}, kFailed};
  for (int y = 0; y < newHeight; ++y) {
    if (context.cancelled()) return {{}, kCancelled};
    const bool topBand = y < border;
    const bool bottomBand = y >= border + h;
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < newWidth; ++x) {
      const bool leftBand = x < border;
      const bool rightBand = x >= border + w;
      if (!topBand && !bottomBand && !leftBand && !rightBand) continue;
      const float hue = perimeterHue(x, y, newWidth, newHeight, totalPerim, perimW, perimH, hueOff);
      const float cross = crossRatio(x, y, border, w, h);
      const float brightnessScale = glow > 0.f ? 1.f - smoothStep(0.6f, 1.f, cross) * glow * 0.5f : 1.f;
      line[x] = fromHsv(hue, sat, brightness * brightnessScale);
    }
  }
  copyRows(result, source, border, border);
  if (innerLine) {
    // The reference intends a 1.5 px line centred on the picture edge.
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(255, 255, 255, 120));
    pen.setWidthF(1.5f);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(float(border) - 0.75f, float(border) - 0.75f, float(w) + 1.5f, float(h) + 1.5f));
  }
  return {result, {}};
}

// ---- minecraft_grass ----------------------------------------------------------------
EffectResult minecraftGrass(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int blockSize = clampInt(intOf(params, "block_size"), 4, 64);
  const int grassRows = clampInt(intOf(params, "grass_rows"), 1, 8);
  const int dirtRows = clampInt(intOf(params, "dirt_rows"), 1, 16);
  const int totalRows = grassRows + dirtRows;
  const int borderHeight = totalRows * blockSize;
  const int overlapPx = clampInt(intOf(params, "overlap"), 0, 8) * blockSize;
  const bool topEdge = boolOf(params, "top_edge");
  const QColor grass = colorOf(params, "grass_color");
  const QColor dirt = colorOf(params, "dirt_color");
  const int w = source.width(), h = source.height();
  const int newHeight = h + borderHeight - overlapPx;
  QImage result = makeSizedResult(source, w, newHeight);
  if (result.isNull()) return {{}, kFailed};
  result.fill(Qt::transparent);
  const int imageY = topEdge ? borderHeight - overlapPx : 0;
  copyRows(result, source, 0, imageY);
  const int borderY = topEdge ? 0 : h - overlapPx;
  const int cols = (w + blockSize - 1) / blockSize;
  for (int col = 0; col < cols; ++col) {
    if (context.cancelled()) return {{}, kCancelled};
    const float noise = hash01(col, 0, 42);
    const int jaggedOffset = noise > 0.5f ? blockSize : 0;
    for (int row = 0; row < totalRows; ++row) {
      const int x = col * blockSize;
      const int y = topEdge ? borderHeight - (row + 1) * blockSize : borderY + row * blockSize;
      const int rowFromTop = topEdge ? (totalRows - 1 - row) : row;
      if (rowFromTop == 0 && jaggedOffset > 0) continue;
      const bool isGrass = rowFromTop < grassRows;
      const float variation = hash01(col, row, 137);
      QRgb base = 0;
      if (isGrass) {
        const int rVar = int(variation * 20 - 10);
        const int gVar = int(variation * 30 - 15);
        base = qRgba(clampInt(grass.red() + rVar, 0, 255), clampInt(grass.green() + gVar, 0, 255),
                     clampInt(grass.blue() + rVar, 0, 255), 255);
      } else {
        const int dVar = int(variation * 24 - 12);
        base = qRgba(clampInt(dirt.red() + dVar, 0, 255), clampInt(dirt.green() + dVar, 0, 255),
                     clampInt(dirt.blue() + dVar + 4, 0, 255), 255);
      }
      const int drawWidth = std::min(blockSize, w - x);
      if (drawWidth <= 0) continue;
      fillRect(result, float(x), float(y), float(x + drawWidth), float(y + blockSize), base);
      const float darkSpeckle = hash01(col, row + 100, 271);
      if (darkSpeckle > 0.6f) {
        const int speckleSize = std::max(1, blockSize / 4);
        const float sx = float(x) + hash01(col, row, 311) * float(blockSize - speckleSize);
        const float sy = float(y) + hash01(col, row, 419) * float(blockSize - speckleSize);
        const QRgb dark = qRgba(clampInt(qRed(base) - 25, 0, 255), clampInt(qGreen(base) - 20, 0, 255),
                                clampInt(qBlue(base) - 20, 0, 255), 255);
        fillRect(result, sx, sy, sx + float(speckleSize), sy + float(speckleSize), dark);
      }
    }
  }
  return {result, {}};
}

// ---- crt ------------------------------------------------------------------------------
uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

EffectResult crt(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float scanline = floatOf(params, "scanlineStrength") / 100.f;
  const int shift = clampInt(intOf(params, "rgbShift"), 0, 8);
  const float noise = std::clamp(floatOf(params, "noiseAmount"), 0.f, 100.f);
  const float vignette = floatOf(params, "vignetteStrength") / 100.f;
  if (scanline <= 0.f && shift == 0 && noise <= 0.f && vignette <= 0.f) return {copyOf(source), {}};
  const int w = source.width(), h = source.height();
  const int right = w - 1;
  const int noiseAmplitude = int(std::nearbyintf(noise * 0.6f));
  const float cx = float(w - 1) * 0.5f;
  const float cy = float(h - 1) * 0.5f;
  const float invCx = cx > 0.f ? 1.f / cx : 1.f;
  const float invCy = cy > 0.f ? 1.f / cy : 1.f;
  constexpr float invSqrt2 = 0.70710678f;
  const uint32_t seed = uint32_t(1337 ^ seedOf(context)) * 747796405u;
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const Pixels src(source);
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    float lineFactor = 1.f;
    if (scanline > 0.f) {
      const float rowDarkness = (y & 1) == 0 ? 0.62f : 0.18f;
      lineFactor = 1.f - (scanline * rowDarkness);
    }
    const float dy = (float(y) - cy) * invCy;
    for (int x = 0; x < w; ++x) {
      const int idx = y * w + x;
      const int xr = clampInt(x + shift, 0, right);
      const int xb = clampInt(x - shift, 0, right);
      const QRgb sampleR = src.at(xr, y);
      const QRgb sampleG = src.at(x, y);
      const QRgb sampleB = src.at(xb, y);
      float maskR = 1.f, maskG = 1.f, maskB = 1.f;
      switch (x % 3) {
      case 0: maskR = 1.06f; maskG = 0.97f; maskB = 0.93f; break;
      case 1: maskR = 0.93f; maskG = 1.06f; maskB = 0.97f; break;
      default: maskR = 0.97f; maskG = 0.93f; maskB = 1.06f; break;
      }
      float vignetteFactor = 1.f;
      if (vignette > 0.f) {
        const float dx = (float(x) - cx) * invCx;
        const float distance01 = std::sqrt((dx * dx) + (dy * dy)) * invSqrt2;
        float t = (distance01 - 0.55f) / 0.45f;
        t = clamp01(t);
        const float smooth = t * t * (3.f - (2.f * t));
        vignetteFactor = 1.f - (vignette * smooth);
      }
      const float brightness = lineFactor * vignetteFactor;
      int nr = 0, ng = 0, nb = 0;
      if (noiseAmplitude > 0) {
        uint32_t h = hash32(uint32_t(idx) ^ seed);
        nr = (int(h & 0xFFu) - 128) * noiseAmplitude / 128;
        h = hash32(h ^ 0x9E3779B9u);
        ng = (int(h & 0xFFu) - 128) * noiseAmplitude / 128;
        h = hash32(h ^ 0x85EBCA6Bu);
        nb = (int(h & 0xFFu) - 128) * noiseAmplitude / 128;
      }
      const int r = int(std::nearbyintf(float(qRed(sampleR)) * maskR * brightness)) + nr;
      const int g = int(std::nearbyintf(float(qGreen(sampleG)) * maskG * brightness)) + ng;
      const int b = int(std::nearbyintf(float(qBlue(sampleB)) * maskB * brightness)) + nb;
      line[x] = qRgba(clampInt(r, 0, 255), clampInt(g, 0, 255), clampInt(b, 0, 255), qAlpha(sampleG));
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

// ---- plasma_energy_arcs ---------------------------------------------------------------
EffectResult plasmaEnergyArcs(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float energy = floatOf(params, "energy") / 100.f;
  const float arcDensity = floatOf(params, "arc_density") / 100.f;
  const float glow = floatOf(params, "glow") / 100.f;
  const float turbulence = floatOf(params, "turbulence") / 100.f;
  const float thickness = floatOf(params, "thickness") / 100.f;
  if (energy <= 0.0001f) return {copyOf(source), {}};
  const int w = source.width(), h = source.height();
  const float invWidth = 1.f / float(std::max(1, w - 1));
  const float invHeight = 1.f / float(std::max(1, h - 1));
  const float lineWidth = 0.020f + ((1.f - thickness) * 0.10f);
  const float densityFreq = 16.f + (arcDensity * 18.f);
  const float turbulenceFreq = 2.f + (turbulence * 6.f);
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const Pixels src(source);
  const bool finished = runRows(context, h, 0, 100, [&](int y) {
    auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
    const float v = float(y) * invHeight;
    for (int x = 0; x < w; ++x) {
      const QRgb c = src.at(x, y);
      const float sr = float(qRed(c)) / 255.f, sg = float(qGreen(c)) / 255.f, sb = float(qBlue(c)) / 255.f;
      const float u = float(x) * invWidth;
      const float fieldA = std::sin((u * densityFreq) +
                                    std::sin((v * turbulenceFreq * 2.4f) + 0.7f) * (1.8f + turbulence * 4.5f) + 0.8f);
      const float fieldB = std::cos((v * (densityFreq * 0.92f)) -
                                    std::sin((u * turbulenceFreq * 1.6f) - 1.1f) * (1.4f + turbulence * 4.2f) - 1.3f);
      const float fieldC = std::sin(((u + v) * (10.f + arcDensity * 12.f)) + 2.1f);
      const float coreField = (fieldA * 0.46f) + (fieldB * 0.34f) + (fieldC * 0.20f);
      const float branchField = (fieldA * 0.28f) - (fieldB * 0.44f) + (fieldC * 0.28f);
      const float core = 1.f - smoothStep(lineWidth, lineWidth + 0.08f, std::fabs(coreField));
      const float branch = 1.f - smoothStep(lineWidth * 0.70f, lineWidth * 0.70f + 0.09f, std::fabs(branchField));
      const float arc = std::max(core, branch * 0.72f);
      // Edge term from the four-neighbour luminance difference.
      const int left = std::max(0, x - 1), rRight = std::min(w - 1, x + 1);
      const int top = std::max(0, y - 1), bottom = std::min(h - 1, y + 1);
      const float lumLeft = luminance01(src.at(left, y));
      const float lumRight = luminance01(src.at(rRight, y));
      const float lumTop = luminance01(src.at(x, top));
      const float lumBottom = luminance01(src.at(x, bottom));
      const float edge = clamp01((std::fabs(lumRight - lumLeft) + std::fabs(lumBottom - lumTop)) * 1.1f);
      const float arcMask = arc * (0.72f + (edge * 0.28f));
      const float glowMask = std::pow(std::max(arcMask, 0.f), 0.34f) * glow;
      const float coreMask = arcMask * energy;
      const float overlayR = (coreMask * 0.20f) + (glowMask * 0.10f);
      const float overlayG = (coreMask * 0.70f) + (glowMask * 0.26f);
      const float overlayB = (coreMask * 1.00f) + (glowMask * 0.34f);
      float outR = screenOverlay(sr, overlayR);
      float outG = screenOverlay(sg, overlayG);
      float outB = screenOverlay(sb, overlayB);
      outR = lerp(outR, 1.f, coreMask * 0.18f);
      outG = lerp(outG, 1.f, coreMask * 0.22f);
      outB = lerp(outB, 1.f, coreMask * 0.28f);
      line[x] = qRgba(toByte(outR * 255.f), toByte(outG * 255.f), toByte(outB * 255.f), qAlpha(c));
    }
  });
  if (!finished) return {{}, kCancelled};
  return {result, {}};
}

}  // namespace

void registerFilterEffects4(EffectRegistry &registry) {
  add(registry, "anamorphic_lens_flare", "Anamorphic lens flare", "AnamorphicLensFlare",
      {floatParam("intensity", "Intensity", 60, 0, 100), floatParam("threshold", "Threshold", 72, 0, 100),
       floatParam("streak_length", "Streak length", 55, 0, 100), floatParam("warmth", "Warmth", 45, 0, 100),
       floatParam("ghosting", "Ghosting", 35, 0, 100)},
      anamorphicLensFlare);
  add(registry, "crystalize_shards", "Crystalize shards", "CrystalizeShards",
      {intParam("shardSize", "Shard size", 22, 6, 80), floatParam("jitter", "Jitter", 65, 0, 100),
       floatParam("edgeStrength", "Edge strength", 75, 0, 100), floatParam("shine", "Shine", 30, 0, 100)},
      crystalizeShards, true);
  add(registry, "inner_shadow", "Inner shadow", "InnerShadow",
      {floatParam("opacity", "Opacity", 70, 0, 100), intParam("size", "Size", 20, 0, 300),
       colorParam("color", "Color", QColor(0, 0, 0)), intParam("offset_x", "Offset X", 0, -300, 300),
       intParam("offset_y", "Offset Y", 0, -300, 300)},
      innerShadow);
  add(registry, "blueprint_drawing", "Blueprint drawing", "BlueprintDrawing",
      {floatParam("line_strength", "Line strength", 75, 0, 100), floatParam("detail", "Detail", 45, 0, 100),
       floatParam("grid_intensity", "Grid intensity", 30, 0, 100), floatParam("texture", "Texture", 25, 0, 100),
       floatParam("glow", "Glow", 35, 0, 100)},
      blueprintDrawing, true);
  add(registry, "holographic_foil_shimmer", "Holographic foil shimmer", "HolographicFoilShimmer",
      {floatParam("intensity", "Intensity", 65, 0, 100), floatParam("scale", "Scale", 120, 40, 300),
       floatParam("shift", "Shift", 0, 0, 360), floatParam("specular", "Specular", 45, 0, 100),
       floatParam("grain", "Grain", 20, 0, 100)},
      holographicFoilShimmer, true);
  add(registry, "woodcut_print", "Woodcut Print", "WoodcutPrint",
      {floatParam("threshold", "Threshold", 50, 0, 100), floatParam("edge_strength", "Edge Strength", 60, 0, 100),
       floatParam("grain_amount", "Wood Grain", 30, 0, 100), colorParam("ink_color", "Ink Color", QColor(15, 10, 5)),
       colorParam("paper_color", "Paper Color", QColor(235, 225, 200))},
      woodcutPrint);
  add(registry, "rainbow_border", "Rainbow border", "RainbowBorder",
      {intParam("border_size", "Border size", 24, 2, 300), floatParam("saturation", "Saturation", 100, 0, 100),
       floatParam("brightness", "Brightness", 95, 0, 100), floatParam("hue_offset", "Hue offset", 0, 0, 360),
       floatParam("glow_strength", "Glow", 40, 0, 100), boolParam("inner_line", "Inner line", true)},
      rainbowBorder, false, true);
  add(registry, "minecraft_grass", "Minecraft grass", "MinecraftGrass",
      {intParam("block_size", "Block size", 16, 4, 64), intParam("grass_rows", "Grass rows", 2, 1, 8),
       intParam("dirt_rows", "Dirt rows", 4, 1, 16), boolParam("top_edge", "Top edge", false),
       intParam("overlap", "Overlap", 1, 0, 8), colorParam("grass_color", "Grass color", QColor(89, 176, 48)),
       colorParam("dirt_color", "Dirt color", QColor(134, 96, 67))},
      minecraftGrass, false, true);
  add(registry, "crt", "CRT", "CRT",
      {floatParam("scanlineStrength", "Scanline strength", 35, 0, 100), intParam("rgbShift", "RGB shift", 1, 0, 8),
       floatParam("noiseAmount", "Noise amount", 8, 0, 100), floatParam("vignetteStrength", "Vignette strength", 18, 0, 100)},
      crt, true);
  add(registry, "plasma_energy_arcs", "Plasma energy arcs", "PlasmaEnergyArcs",
      {floatParam("energy", "Energy", 65, 0, 100), floatParam("arc_density", "Arc density", 46, 0, 100),
       floatParam("glow", "Glow", 70, 0, 100), floatParam("turbulence", "Turbulence", 52, 0, 100),
       floatParam("thickness", "Thickness", 38, 0, 100)},
      plasmaEnergyArcs);
}
}  // namespace xerahs::effects
