// EDITOR-EFFECTS-001: the Filters category, batch 7 (outline, shadow) and the
// Manipulations scale effect. EE-003, EE-006, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census source
// path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Skia's dilate filter (SKImageFilter.CreateDilate) is a separable maximum filter over the
//    alpha channel with a (2r+1)x(2r+1) window, so the shape grows by exactly r pixels as in Skia.
//  - Skia's blur (SKImageFilter.CreateBlur) is the shared approximateGaussianBlur helper: a true
//    separable Gaussian on premultiplied float pixels with the reference's sigma.
//  - Skia's Catmull-Rom resample (SKCubicResampler.CatmullRom) is the editor canvas bilinear
//    resample, the same substitution resize_image makes, so geometry and effects share pixels.
//  - Skia's SrcIn, DstOut and SrcOver blends are straight-alpha maths on premultiplied channels.
#include "Effect.h"
#include "EffectSupport.h"
#include "image-editor/CanvasOperations.h"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

namespace xerahs::effects {
namespace {
using namespace support;
namespace ed = xerahs::editor;

const QString kFilterReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/");
const QString kManipulationReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Manipulations/");

// Skia rounds a filtered channel to the nearest byte.
int byte(float value) { return int(std::floor(std::clamp(value, 0.f, 255.f) + 0.5f)); }

// Premultiplied straight-alpha pixel: the outline and shadow composites mix channels the way
// Skia mixes its premultiplied surfaces.
struct Premul {
  float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
};
Premul sourceOver(const Premul &src, const Premul &dst) {
  Premul out;
  const float keep = 1.f - src.a;
  out.r = src.r + dst.r * keep;
  out.g = src.g + dst.g * keep;
  out.b = src.b + dst.b * keep;
  out.a = src.a + dst.a * keep;
  return out;
}
QRgb toStraight(const Premul &pixel) {
  if (pixel.a <= 0.f) return 0;
  return qRgba(byte(pixel.r / pixel.a * 255.f), byte(pixel.g / pixel.a * 255.f),
               byte(pixel.b / pixel.a * 255.f), byte(pixel.a * 255.f));
}
Premul straightPixel(QRgb pixel) {
  const float alpha = qAlpha(pixel) / 255.f;
  return {qRed(pixel) / 255.f * alpha, qGreen(pixel) / 255.f * alpha, qBlue(pixel) / 255.f * alpha, alpha};
}

// Reports a sub-range of the overall progress.
EffectContext phase(const EffectContext &context, int lo, int hi) {
  EffectContext part = context;
  part.progress = [=](int percent) { context.progress(lo + percent * (hi - lo) / 100); };
  return part;
}

// A copy of the source that keeps its colour space (the reference's source.Copy()).
EffectResult identity(const QImage &source, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const bool done = runRows(context, source.height(), 0, 100, [&](int y) {
    std::memcpy(result.scanLine(y), source.constScanLine(y), size_t(source.width()) * 4);
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// Sliding maximum: out[i] is the largest src sample in [i + lo, i + hi] clamped to [0, n - 1].
void slidingMax(const uint8_t *src, int n, int lo, int hi, int outLength, uint8_t *out) {
  std::deque<int> window;
  int next = 0;
  for (int i = 0; i < outLength; ++i) {
    const int upper = std::min(n - 1, i + hi);
    while (next <= upper) {
      while (!window.empty() && src[window.back()] <= src[next]) window.pop_back();
      window.push_back(next);
      ++next;
    }
    const int lower = std::max(0, i + lo);
    while (!window.empty() && window.front() < lower) window.pop_front();
    out[i] = window.empty() ? 0 : src[window.front()];
  }
}

// Morphological dilation of the source alpha (Skia SKImageFilter.CreateDilate): the map is
// (w + 2r) x (h + 2r) and pixel (u, v) holds the largest source alpha within radius r of source
// pixel (u - r, v - r). Samples outside the source are transparent, so the shape grows by r pixels.
std::vector<uint8_t> dilateAlpha(const QImage &source, int radius, const EffectContext &context) {
  const int width = source.width(), height = source.height();
  const int extendedWidth = width + radius * 2, extendedHeight = height + radius * 2;
  std::vector<uint8_t> horizontal(size_t(extendedWidth) * size_t(height));
  if (!runRows(context, height, 0, 50, [&](int y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        std::vector<uint8_t> alpha(static_cast<size_t>(width), 0);
        for (int x = 0; x < width; ++x) alpha[size_t(x)] = qAlpha(row[x]);
        slidingMax(alpha.data(), width, -2 * radius, 0, extendedWidth,
                    horizontal.data() + size_t(y) * size_t(extendedWidth));
      }))
    return {};
  std::vector<uint8_t> result(size_t(extendedWidth) * size_t(extendedHeight));
  std::vector<uint8_t> column(static_cast<size_t>(height), 0);
  std::vector<uint8_t> filtered(static_cast<size_t>(extendedHeight), 0);
  for (int x = 0; x < extendedWidth; ++x) {
    if (context.cancelled()) return {};
    for (int y = 0; y < height; ++y) column[size_t(y)] = horizontal[size_t(y) * size_t(extendedWidth) + size_t(x)];
    slidingMax(column.data(), height, -2 * radius, 0, extendedHeight, filtered.data());
    for (int v = 0; v < extendedHeight; ++v) result[size_t(v) * size_t(extendedWidth) + size_t(x)] = filtered[size_t(v)];
  }
  return result;
}

// ---- outline --------------------------------------------------------------------------------------
// The reference draws the source on a canvas that grows by (size + padding) on every side, tints
// the source alpha dilated by (size + padding) with the outline colour, erases the source alpha
// dilated by padding (the gap) and finally either erases or draws the source itself.
EffectResult outlineEffect(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int size = intOf(params, "size");
  const int padding = intOf(params, "padding");
  const bool outlineOnly = boolOf(params, "outline_only");
  const QColor color = colorOf(params, "color");
  if (size <= 0) return identity(source, context);  // parameter validation already rejects size < 1
  const int expand = size + padding;  // reference: totalExpand
  const int width = source.width(), height = source.height();
  const int canvasWidth = width + expand * 2, canvasHeight = height + expand * 2;
  const std::vector<uint8_t> outer = dilateAlpha(source, expand, phase(context, 0, 40));
  if (outer.empty()) return {{}, kCancelled};
  std::vector<uint8_t> gap;
  const int gapWidth = width + padding * 2, gapHeight = height + padding * 2;
  if (padding > 0) {
    gap = dilateAlpha(source, padding, phase(context, 40, 60));
    if (gap.empty()) return {{}, kCancelled};
  }
  // The tint is a SrcIn blend of the outline colour: the colour replaces the dilated RGB and its
  // own alpha scales the dilated alpha.
  const float tintAlpha = color.alpha() / 255.f;
  const int gapOffset = expand - padding;  // the gap filter is drawn at (size, size)
  QImage result(canvasWidth, canvasHeight, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  const bool done = runRows(context, canvasHeight, 60, 100, [&](int y) {
    auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < canvasWidth; ++x) {
      Premul footprint;
      const float dilated = outer[size_t(y) * size_t(canvasWidth) + size_t(x)] / 255.f;
      footprint.a = dilated * tintAlpha;
      footprint.r = color.red() / 255.f * footprint.a;
      footprint.g = color.green() / 255.f * footprint.a;
      footprint.b = color.blue() / 255.f * footprint.a;
      if (padding > 0) {
        const int gx = x - gapOffset, gy = y - gapOffset;
        if (gx >= 0 && gy >= 0 && gx < gapWidth && gy < gapHeight) {
          const float keep = 1.f - gap[size_t(gy) * size_t(gapWidth) + size_t(gx)] / 255.f;
          footprint.r *= keep;
          footprint.g *= keep;
          footprint.b *= keep;
          footprint.a *= keep;
        }
      }
      const int sx = x - expand, sy = y - expand;
      const bool inside = sx >= 0 && sy >= 0 && sx < width && sy < height;
      const Premul picture = inside ? straightPixel(source.pixel(sx, sy)) : Premul{};
      if (outlineOnly) {
        // DstOut of the source leaves only the ring.
        const float keep = 1.f - picture.a;
        footprint.r *= keep;
        footprint.g *= keep;
        footprint.b *= keep;
        footprint.a *= keep;
      } else {
        footprint = sourceOver(picture, footprint);
      }
      out[x] = toStraight(footprint);
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// ---- shadow ---------------------------------------------------------------------------------------
// The reference tints the source with the shadow colour (alpha = opacity) and blurs it with
// sigma = size / 2, then draws the source over the shadow. Tinting and blurring commute, so the
// shadow is the blurred source alpha tinted with the shadow colour.
EffectResult shadowEffect(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float opacity = floatOf(params, "opacity") / 100.f;
  const int size = intOf(params, "size");
  const int offsetX = intOf(params, "offset_x");
  const int offsetY = intOf(params, "offset_y");
  const bool autoResize = boolOf(params, "auto_resize");
  const QColor color = colorOf(params, "color");
  const int width = source.width(), height = source.height();
  const int expandLeft = autoResize ? std::max(0, -offsetX) + size : 0;
  const int expandRight = autoResize ? std::max(0, offsetX) + size : 0;
  const int expandTop = autoResize ? std::max(0, -offsetY) + size : 0;
  const int expandBottom = autoResize ? std::max(0, offsetY) + size : 0;
  const int canvasWidth = width + expandLeft + expandRight;
  const int canvasHeight = height + expandTop + expandBottom;
  const int imageX = expandLeft, imageY = expandTop;
  const int shadowX = imageX + offsetX, shadowY = imageY + offsetY;
  const float sigma = size / 2.f;
  // Skia blurs the source with a transparent surround, so the blurred tail reaches past the source
  // edges; padding the source keeps that tail.
  const int pad = sigma > 0.f ? int(std::ceil(double(sigma) * 3.0)) + 1 : 0;
  QImage padded(width + pad * 2, height + pad * 2, QImage::Format_ARGB32);
  if (padded.isNull()) return {{}, kFailed};
  padded.setColorSpace(source.colorSpace());
  padded.fill(0);
  for (int y = 0; y < height; ++y)
    std::memcpy(reinterpret_cast<QRgb *>(padded.scanLine(y + pad)) + pad, source.constScanLine(y), size_t(width) * 4);
  QImage blurred = padded;
  if (sigma > 0.f) {
    blurred = approximateGaussianBlur(padded, sigma, BlurBorder::Decal, phase(context, 0, 60), 0, 100);
    if (blurred.isNull()) return {{}, kCancelled};
  }
  // The shadow colour carries the opacity in its alpha (the reference builds it that way).
  const int shadowAlpha = int(255.f * opacity);
  const float alphaScale = shadowAlpha / 255.f;
  QImage result(canvasWidth, canvasHeight, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  const bool done = runRows(context, canvasHeight, 60, 100, [&](int y) {
    auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < canvasWidth; ++x) {
      Premul shadow;
      const int bx = x - shadowX + pad, by = y - shadowY + pad;
      if (bx >= 0 && by >= 0 && bx < blurred.width() && by < blurred.height()) {
        shadow.a = qAlpha(blurred.pixel(bx, by)) / 255.f * alphaScale;
        shadow.r = color.red() / 255.f * shadow.a;
        shadow.g = color.green() / 255.f * shadow.a;
        shadow.b = color.blue() / 255.f * shadow.a;
      }
      Premul picture;
      const int ix = x - imageX, iy = y - imageY;
      if (ix >= 0 && iy >= 0 && ix < width && iy < height) picture = straightPixel(source.pixel(ix, iy));
      out[x] = toStraight(sourceOver(picture, shadow));
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// ---- scale ----------------------------------------------------------------------------------------
// The reference scales by percentage and derives the missing percentage from the aspect ratio,
// then resamples with Catmull-Rom. The editor canvas resample (bilinear) replaces it.
EffectResult scaleEffect(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float widthPercentage = floatOf(params, "width_percentage");
  const float heightPercentage = floatOf(params, "height_percentage");
  if (widthPercentage <= 0.f && heightPercentage <= 0.f) return identity(source, context);
  // Math.Round is half to even, as is nearbyint.
  int width = int(std::nearbyint(widthPercentage / 100.f * float(source.width())));
  int height = int(std::nearbyint(heightPercentage / 100.f * float(source.height())));
  if (width == 0) width = int(std::nearbyint(float(height) / float(source.height()) * float(source.width())));
  else if (height == 0) height = int(std::nearbyint(float(width) / float(source.width()) * float(source.height())));
  if (width <= 0 || height <= 0) return identity(source, context);
  ed::CanvasOperation operation;
  operation.action = ed::CanvasAction::ResizeImage;
  operation.width = width;
  operation.height = height;
  operation.lockAspect = false;
  operation.interpolation = ed::Interpolation::Bilinear;
  ed::ArgbImage input{source.width(), source.height(), {}};
  input.pixels.resize(size_t(source.width()) * size_t(source.height()));
  if (!runRows(context, source.height(), 0, 10, [&](int y) {
        std::copy_n(reinterpret_cast<const QRgb *>(source.constScanLine(y)), source.width(),
                    input.pixels.begin() + size_t(y) * size_t(source.width()));
      }))
    return {{}, kCancelled};
  ed::AnnotationDocument document;
  document.canvasWidth = source.width();
  document.canvasHeight = source.height();
  const auto canvas = ed::applyCanvasOperation(
      document, input, operation, {context.cancelled, [&](int percent) { context.progress(10 + percent * 80 / 100); }});
  if (canvas.error) return {{}, *canvas.error == "canvas-cancelled" ? kCancelled : kInvalidInput};
  if (!canvas.image) return identity(source, context);
  const auto &image = *canvas.image;
  QImage result(int(image.width), int(image.height), QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  if (!runRows(context, result.height(), 90, 100, [&](int y) {
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) out[x] = image.at(x, y);
      }))
    return {{}, kCancelled};
  return {result, {}};
}

void addFilter(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference,
               QVector<ParamSpec> parameters, ApplyFn fn, bool changesSize = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Filters;
  spec.mode = Mode::Previewable;
  spec.parameters = std::move(parameters);
  spec.changesSize = changesSize;
  spec.usesSeed = false;
  spec.referencePath = kFilterReferenceDirectory + reference;
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}

void addManipulation(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference,
                     QVector<ParamSpec> parameters, ApplyFn fn, bool changesSize = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Manipulations;
  spec.mode = Mode::Previewable;
  spec.parameters = std::move(parameters);
  spec.changesSize = changesSize;
  spec.usesSeed = false;
  spec.referencePath = kManipulationReferenceDirectory + reference;
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}
}  // namespace

void registerFilterEffects7(EffectRegistry &registry) {
  addFilter(registry, "outline", "Outline", "OutlineImageEffect.cs",
            {intParam("size", "Size", 3, 1, 50),
             intParam("padding", "Padding", 0, 0, 100),
             boolParam("outline_only", "Outline only", false),
             colorParam("color", "Color", QColor(0, 0, 0))},
            outlineEffect, true);
  addFilter(registry, "shadow", "Shadow", "ShadowImageEffect.cs",
            {floatParam("opacity", "Opacity", 80, 0, 100),
             intParam("size", "Size", 20, 0, 100),
             intParam("offset_x", "Offset X", 5, -1000, 1000),
             intParam("offset_y", "Offset Y", 5, -1000, 1000),
             colorParam("color", "Color", QColor(0, 0, 0)),
             boolParam("auto_resize", "Auto resize", true)},
            shadowEffect, true);
  addManipulation(registry, "scale", "Scale", "ScaleImageEffect.cs",
                  {floatParam("width_percentage", "Width %", 100, 0, 500),
                   floatParam("height_percentage", "Height %", 0, 0, 500)},
                  scaleEffect, true);
}
}  // namespace xerahs::effects
