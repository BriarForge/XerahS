#include "image-editor/AnnotationRenderer.h"

#include <algorithm>
#include <cmath>

// ES-025 requires binary64 evaluated in the order written, without fused
// multiply-add; CMakeLists.txt builds this library with -ffp-contract=off.

namespace xerahs::editor {

namespace {

struct Box {
  double left;
  double top;
  double right;
  double bottom;
};

struct Channels {
  double a;
  double r;
  double g;
  double b;
};

Channels channels(Argb c) {
  return {((c >> 24) & 0xFF) / 255.0, ((c >> 16) & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, (c & 0xFF) / 255.0};
}

quint32 quantize(double value) {
  // ES-025: floor(value * 255 + 0.5) after every annotation.
  return static_cast<quint32>(std::clamp(std::floor(value * 255 + 0.5), 0.0, 255.0));
}

Argb pack(const Channels &c) {
  return (quantize(c.a) << 24) | (quantize(c.r) << 16) | (quantize(c.g) << 8) | quantize(c.b);
}

// Exact area of [i, i+1) x [j, j+1) intersected with `box`.
double coverage(const Box &box, qint64 i, qint64 j) {
  const double x0 = static_cast<double>(i);
  const double y0 = static_cast<double>(j);
  const double ox = std::min(x0 + 1, box.right) - std::max(x0, box.left);
  const double oy = std::min(y0 + 1, box.bottom) - std::max(y0, box.top);
  if (ox <= 0 || oy <= 0) return 0;
  return ox * oy;
}

// Rotation normalized to [0, 360); nullopt unless a multiple of 90.
std::optional<int> quarterTurns(double degrees) {
  double r = std::fmod(degrees, 360.0);
  if (r < 0) r += 360.0;
  if (r == 0) return 0;
  if (r == 90) return 1;
  if (r == 180) return 2;
  if (r == 270) return 3;
  return std::nullopt;
}

double compose(double layerColour, double a, double cd, double ad, double ao) {
  if (ao == 0) return 0;
  return (layerColour * a + cd * ad * (1 - a)) / ao;
}

}  // namespace

ArgbImage solidImage(qint64 width, qint64 height, Argb color) {
  return ArgbImage{width, height, std::vector<Argb>(static_cast<std::size_t>(width * height), color)};
}

RenderResult render(const ArgbImage &source, const std::vector<Annotation> &annotations) {
  RenderResult result;
  ArgbImage image = source;  // ES-009: the source itself is never modified

  for (const Annotation &annotation : annotations) {
    const auto *rectangle = std::get_if<RectangleAnnotation>(&annotation);
    if (!rectangle || !rectangle->visible) continue;
    const RectangleStyle &style = rectangle->style;
    const std::optional<int> turns = quarterTurns(style.rotationDegrees);
    if (!turns) {
      result.error = RenderError::UnsupportedRotation;
      return result;
    }

    // ES-024: rotation is about the bounds centre; a quarter turn swaps the
    // extents.
    Box bounds{rectangle->left(), rectangle->top(), rectangle->right(), rectangle->bottom()};
    if (*turns % 2 == 1) {
      const double cx = (bounds.left + bounds.right) / 2;
      const double cy = (bounds.top + bounds.bottom) / 2;
      const double halfWidth = (bounds.right - bounds.left) / 2;
      const double halfHeight = (bounds.bottom - bounds.top) / 2;
      bounds = {cx - halfHeight, cy - halfWidth, cx + halfHeight, cy + halfWidth};
    }
    // ES-010, ES-025: stroke centred on the path.
    const double half = style.strokeWidth / 2;
    const Box grown{bounds.left - half, bounds.top - half, bounds.right + half, bounds.bottom + half};
    const Box shrunk{bounds.left + half, bounds.top + half, bounds.right - half, bounds.bottom - half};
    const bool shrunkEmpty = !(shrunk.right - shrunk.left > 0) || !(shrunk.bottom - shrunk.top > 0);

    // ES-025 composites every pixel. Where the layer alpha is 0 the result is
    // the destination, except that a fully transparent destination becomes
    // 0 in every channel; applying that first lets the loop skip A = 0 pixels.
    for (Argb &pixel : image.pixels) {
      if ((pixel >> 24) == 0) pixel = 0;
    }

    const Channels stroke = channels(style.strokeColor);
    const Channels fill = channels(style.fillColor);

    // ES-010: clip to the canvas.
    const qint64 x0 = std::max<qint64>(0, static_cast<qint64>(std::floor(grown.left)));
    const qint64 y0 = std::max<qint64>(0, static_cast<qint64>(std::floor(grown.top)));
    const qint64 x1 = std::min<qint64>(image.width, static_cast<qint64>(std::ceil(grown.right)));
    const qint64 y1 = std::min<qint64>(image.height, static_cast<qint64>(std::ceil(grown.bottom)));

    for (qint64 j = y0; j < y1; ++j) {
      for (qint64 i = x0; i < x1; ++i) {
        const double coverageFill = coverage(bounds, i, j);
        const double coverageStroke = coverage(grown, i, j) - (shrunkEmpty ? 0 : coverage(shrunk, i, j));
        const double af = coverageFill * fill.a;
        const double as = coverageStroke * stroke.a;
        const double layerAlpha = as + af * (1 - as);
        if (layerAlpha == 0) continue;  // A = 0: destination unchanged (see above)
        // ES-010: fill before stroke; opacity applied once.
        const Channels layer{layerAlpha, (stroke.r * as + fill.r * af * (1 - as)) / layerAlpha,
                             (stroke.g * as + fill.g * af * (1 - as)) / layerAlpha,
                             (stroke.b * as + fill.b * af * (1 - as)) / layerAlpha};
        const double a = layerAlpha * style.opacity;

        Argb &pixel = image.pixels[static_cast<std::size_t>(j * image.width + i)];
        const Channels d = channels(pixel);
        const double ao = a + d.a * (1 - a);
        pixel = pack(Channels{ao, compose(layer.r, a, d.r, d.a, ao), compose(layer.g, a, d.g, d.a, ao),
                              compose(layer.b, a, d.b, d.a, ao)});
      }
    }
  }
  result.image = std::move(image);
  return result;
}

}  // namespace xerahs::editor
