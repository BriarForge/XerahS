#include "image-editor/AnnotationRenderer.h"

#include <algorithm>
#include <array>
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

using Polygon = std::vector<PointF>;

Polygon rotatedBox(const Box &box, PointF centre, double cosine, double sine) {
  Polygon polygon;
  polygon.reserve(4);
  for (const PointF point : std::array<PointF, 4>{{{box.left, box.top}, {box.right, box.top},
                                                {box.right, box.bottom}, {box.left, box.bottom}}}) {
    const double x = point.x - centre.x;
    const double y = point.y - centre.y;
    polygon.push_back({centre.x + x * cosine - y * sine, centre.y + x * sine + y * cosine});
  }
  return polygon;
}

bool finite(const Polygon &polygon) {
  return std::all_of(polygon.begin(), polygon.end(), [](PointF p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
  });
}

// Sutherland-Hodgman clipping against a pixel's four half-planes. Subtract
// the pixel origin first so area evaluation never subtracts large products
// in document coordinates. Pixel edges of zero area do not affect coverage.
double coverage(const Polygon &polygon, qint64 i, qint64 j) {
  Polygon clipped;
  clipped.reserve(8);
  for (PointF p : polygon) clipped.push_back({p.x - i, p.y - j});
  for (int edge = 0; edge < 4 && !clipped.empty(); ++edge) {
    const bool horizontal = edge >= 2;
    const double boundary = edge % 2;
    const auto coordinate = [horizontal](PointF p) { return horizontal ? p.y : p.x; };
    const auto inside = [&](PointF p) {
      return edge % 2 == 0 ? coordinate(p) >= boundary : coordinate(p) <= boundary;
    };
    Polygon next;
    next.reserve(8);
    PointF previous = clipped.back();
    bool previousInside = inside(previous);
    for (PointF current : clipped) {
      const bool currentInside = inside(current);
      if (previousInside != currentInside) {
        const double fraction = (boundary - coordinate(previous)) / (coordinate(current) - coordinate(previous));
        PointF intersection{previous.x + fraction * (current.x - previous.x),
                            previous.y + fraction * (current.y - previous.y)};
        if (horizontal) intersection.y = boundary;
        else intersection.x = boundary;
        next.push_back(intersection);
      }
      if (currentInside) next.push_back(current);
      previous = current;
      previousInside = currentInside;
    }
    clipped = std::move(next);
  }
  double twiceArea = 0;
  for (std::size_t n = 1; n + 1 < clipped.size(); ++n) {
    const PointF a{clipped[n].x - clipped[0].x, clipped[n].y - clipped[0].y};
    const PointF b{clipped[n + 1].x - clipped[0].x, clipped[n + 1].y - clipped[0].y};
    twiceArea += a.x * b.y - a.y * b.x;
  }
  return std::clamp(std::abs(twiceArea) / 2, 0.0, 1.0);
}

double compose(double layerColour, double a, double cd, double ad, double ao) {
  if (ao == 0) return 0;
  return (layerColour * a + cd * ad * (1 - a)) / ao;
}

}  // namespace

ArgbImage solidImage(qint64 width, qint64 height, Argb color) {
  return ArgbImage{width, height, std::vector<Argb>(static_cast<std::size_t>(width * height), color)};
}

RenderResult render(const ArgbImage &source, const std::vector<Annotation> &annotations, const RenderControl &control) {
  RenderResult result;
  const auto cancelled = [&] {
    if (!control.cancelled || !control.cancelled()) return false;
    result.error = RenderError::Cancelled;
    return true;
  };
  if (cancelled()) return result;
  if (source.width < 1 || source.height < 1 || source.width > kMaxDimension || source.height > kMaxDimension ||
      source.width * source.height > kMaxPixels ||
      source.pixels.size() != static_cast<std::size_t>(source.width * source.height)) {
    result.error = RenderError::InvalidSource;
    return result;
  }
  ArgbImage image = source;  // ES-009: the source itself is never modified

  for (std::size_t index = 0; index < annotations.size(); ++index) {
    if (cancelled()) return result;
    if (control.progress) control.progress(int(index * 100 / annotations.size()));
    const Annotation &annotation = annotations[index];
    const auto *rectangle = std::get_if<RectangleAnnotation>(&annotation);
    if (!rectangle || !rectangle->visible) continue;
    const RectangleStyle &style = rectangle->style;
    if (!std::isfinite(rectangle->left()) || !std::isfinite(rectangle->top()) ||
        !std::isfinite(rectangle->right()) || !std::isfinite(rectangle->bottom()) ||
        !std::isfinite(style.rotationDegrees) || !std::isfinite(style.strokeWidth) || style.strokeWidth <= 0 ||
        !std::isfinite(style.opacity) || style.opacity < 0 || style.opacity > 1 ||
        rectangle->right() <= rectangle->left() || rectangle->bottom() <= rectangle->top()) {
      result.error = RenderError::InvalidGeometry;
      return result;
    }
    const std::optional<int> turns = quarterTurns(style.rotationDegrees);

    // ES-024: rotation is about the bounds centre; a quarter turn swaps the
    // extents.
    Box bounds{rectangle->left(), rectangle->top(), rectangle->right(), rectangle->bottom()};
    if (turns && *turns % 2 == 1) {
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

    Box clipBounds = grown;
    Polygon fillPolygon, outerPolygon, innerPolygon;
    if (!turns) {
      const PointF centre{bounds.left / 2 + bounds.right / 2, bounds.top / 2 + bounds.bottom / 2};
      const double radians = std::fmod(style.rotationDegrees, 360.0) * (std::acos(-1.0) / 180.0);
      const double cosine = std::cos(radians), sine = std::sin(radians);
      fillPolygon = rotatedBox(bounds, centre, cosine, sine);
      outerPolygon = rotatedBox(grown, centre, cosine, sine);
      if (!shrunkEmpty) innerPolygon = rotatedBox(shrunk, centre, cosine, sine);
      if (!finite(fillPolygon) || !finite(outerPolygon) || !finite(innerPolygon)) {
        result.error = RenderError::InvalidGeometry;
        return result;
      }
      clipBounds = {outerPolygon[0].x, outerPolygon[0].y, outerPolygon[0].x, outerPolygon[0].y};
      for (PointF p : outerPolygon) {
        clipBounds.left = std::min(clipBounds.left, p.x);
        clipBounds.top = std::min(clipBounds.top, p.y);
        clipBounds.right = std::max(clipBounds.right, p.x);
        clipBounds.bottom = std::max(clipBounds.bottom, p.y);
      }
    }
    if (!std::isfinite(clipBounds.left) || !std::isfinite(clipBounds.top) ||
        !std::isfinite(clipBounds.right) || !std::isfinite(clipBounds.bottom)) {
      result.error = RenderError::InvalidGeometry;
      return result;
    }

    // ES-025 composites every pixel. Where the layer alpha is 0 the result is
    // the destination, except that a fully transparent destination becomes
    // 0 in every channel; applying that first lets the loop skip A = 0 pixels.
    for (std::size_t k = 0; k < image.pixels.size(); ++k) {
      if (k % 16384 == 0 && cancelled()) return result;
      Argb &pixel = image.pixels[k];
      if ((pixel >> 24) == 0) pixel = 0;
    }

    const Channels stroke = channels(style.strokeColor);
    const Channels fill = channels(style.fillColor);

    // ES-010: clip to the canvas.
    // Clamp in floating point before integer conversion, including far-off
    // finite annotations. This also avoids undefined casts of large values.
    const qint64 x0 = static_cast<qint64>(std::floor(std::clamp(clipBounds.left, 0.0, double(image.width))));
    const qint64 y0 = static_cast<qint64>(std::floor(std::clamp(clipBounds.top, 0.0, double(image.height))));
    const qint64 x1 = static_cast<qint64>(std::ceil(std::clamp(clipBounds.right, 0.0, double(image.width))));
    const qint64 y1 = static_cast<qint64>(std::ceil(std::clamp(clipBounds.bottom, 0.0, double(image.height))));

    for (qint64 j = y0; j < y1; ++j) {
      if (cancelled()) return result;
      for (qint64 i = x0; i < x1; ++i) {
        const double coverageFill = turns ? coverage(bounds, i, j) : coverage(fillPolygon, i, j);
        const double outer = turns ? coverage(grown, i, j) : coverage(outerPolygon, i, j);
        const double inner = shrunkEmpty ? 0 : (turns ? coverage(shrunk, i, j) : coverage(innerPolygon, i, j));
        const double coverageStroke = std::clamp(outer - inner, 0.0, 1.0);
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
      if (control.progress) control.progress(int((index * 100 + (j - y0 + 1) * 100 / (y1 - y0)) / annotations.size()));
    }
  }
  if (cancelled()) return result;
  if (control.progress) control.progress(100);
  result.image = std::move(image);
  return result;
}

}  // namespace xerahs::editor
