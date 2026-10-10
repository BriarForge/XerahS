#include "image-editor/CanvasOperations.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace xerahs::editor {
namespace {

bool finite(PointF p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool validSize(qint64 width, qint64 height) {
  return width > 0 && height > 0 && width <= kMaxDimension && height <= kMaxDimension && width * height <= kMaxPixels;
}
bool cancelled(const CanvasControl &control) { return control.cancelled && control.cancelled(); }
void progress(const CanvasControl &control, int value) { if (control.progress) control.progress(value); }

CanvasResult failure(const QString &error) {
  CanvasResult result;
  result.error = error;
  return result;
}

double normalizedAngle(double angle) {
  angle = std::fmod(angle, 360.0);
  return angle < 0 ? angle + 360 : angle;
}

bool validRectangle(const RectangleAnnotation &r) {
  return finite(r.start) && finite(r.end) && r.right() > r.left() && r.bottom() > r.top() &&
         std::isfinite(r.right() - r.left()) && std::isfinite(r.bottom() - r.top()) &&
         std::isfinite(r.style.rotationDegrees) && std::isfinite(r.style.strokeWidth) && r.style.strokeWidth > 0 &&
         std::isfinite(r.style.opacity) && r.style.opacity >= 0 && r.style.opacity <= 1;
}

// Separating-axis intersection of the rotated, stroke-grown rectangle and
// the retained canvas. Touching edges have zero area and are removed. A
// partly intersecting object keeps its editable bounds; export clips it.
std::optional<bool> intersectsCanvas(const RectangleAnnotation &r, qint64 width, qint64 height) {
  const double radians = normalizedAngle(r.style.rotationDegrees) * (std::acos(-1.0) / 180);
  const double cosine = std::cos(radians), sine = std::sin(radians);
  const PointF centre{r.left() / 2 + r.right() / 2, r.top() / 2 + r.bottom() / 2};
  const double halfWidth = (r.right() - r.left()) / 2 + r.style.strokeWidth / 2;
  const double halfHeight = (r.bottom() - r.top()) / 2 + r.style.strokeWidth / 2;
  const std::array<PointF, 4> object{{
      {centre.x - halfWidth * cosine + halfHeight * sine, centre.y - halfWidth * sine - halfHeight * cosine},
      {centre.x + halfWidth * cosine + halfHeight * sine, centre.y + halfWidth * sine - halfHeight * cosine},
      {centre.x + halfWidth * cosine - halfHeight * sine, centre.y + halfWidth * sine + halfHeight * cosine},
      {centre.x - halfWidth * cosine - halfHeight * sine, centre.y - halfWidth * sine + halfHeight * cosine}}};
  const std::array<PointF, 4> canvas{{{0, 0}, {double(width), 0}, {double(width), double(height)}, {0, double(height)}}};
  for (PointF p : object) if (!finite(p)) return std::nullopt;
  for (PointF axis : std::array<PointF, 4>{{{1, 0}, {0, 1}, {cosine, sine}, {-sine, cosine}}}) {
    double objectMin = INFINITY, objectMax = -INFINITY, canvasMin = INFINITY, canvasMax = -INFINITY;
    for (PointF p : object) {
      const double projection = p.x * axis.x + p.y * axis.y;
      if (!std::isfinite(projection)) return std::nullopt;
      objectMin = std::min(objectMin, projection); objectMax = std::max(objectMax, projection);
    }
    for (PointF p : canvas) {
      const double projection = p.x * axis.x + p.y * axis.y;
      canvasMin = std::min(canvasMin, projection); canvasMax = std::max(canvasMax, projection);
    }
    if (objectMax <= canvasMin || canvasMax <= objectMin) return false;
  }
  return true;
}

Argb bilinear(const ArgbImage &source, double x, double y) {
  x = std::clamp(x, 0.0, double(source.width - 1));
  y = std::clamp(y, 0.0, double(source.height - 1));
  const qint64 left = qint64(std::floor(x)), top = qint64(std::floor(y));
  const double fx = x - left, fy = y - top;
  const std::array<Argb, 4> samples{{source.at(left, top), source.at(std::min(left + 1, source.width - 1), top),
      source.at(left, std::min(top + 1, source.height - 1)),
      source.at(std::min(left + 1, source.width - 1), std::min(top + 1, source.height - 1))}};
  const std::array<double, 4> weights{{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy}};
  double alpha = 0, red = 0, green = 0, blue = 0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const double a = (samples[i] >> 24) / 255.0;
    const double weight = a * weights[i];
    alpha += weight;
    red += ((samples[i] >> 16) & 255) * weight;
    green += ((samples[i] >> 8) & 255) * weight;
    blue += (samples[i] & 255) * weight;
  }
  if (alpha == 0) return 0;
  const auto byte = [](double value) { return quint32(std::clamp(std::floor(value + .5), 0.0, 255.0)); };
  return (byte(alpha * 255) << 24) | (byte(red / alpha) << 16) | (byte(green / alpha) << 8) | byte(blue / alpha);
}

CanvasResult apply(const AnnotationDocument &document, const ArgbImage &source,
                   const CanvasOperation &op, const CanvasControl &control) {
  if (!validSize(source.width, source.height) || document.canvasWidth != source.width || document.canvasHeight != source.height ||
      source.pixels.size() != std::size_t(source.width * source.height)) return failure(diagnostic::documentInvalid);
  if (cancelled(control)) return failure(QStringLiteral("canvas-cancelled"));
  if (op.action == CanvasAction::AutoCrop) {
    const AutoCropPolicy &policy = op.autoCrop;
    if (policy.alphaThreshold < 0 || policy.alphaThreshold > 255 || policy.tolerance < 0 || policy.tolerance > 255 ||
        (policy.border != CropBorder::Transparent && policy.border != CropBorder::TopLeftColor && policy.border != CropBorder::Color))
      return failure(diagnostic::documentInvalid);
    qint64 left = source.width, top = source.height, right = 0, bottom = 0;
    {
      std::optional<ArgbImage> composite;
      if (policy.includeAnnotations && !document.annotations.empty()) {
        for (const auto &annotation : document.annotations)
          if (!std::holds_alternative<RectangleAnnotation>(annotation))
            return failure(QStringLiteral("canvas-annotation-transform-unsupported"));
        auto rendered = render(source, document.annotations,
            {control.cancelled, [&](int p) { progress(control, p * 30 / 100); }});
        if (rendered.error) return failure(*rendered.error == RenderError::Cancelled ? QStringLiteral("canvas-cancelled") : diagnostic::documentInvalid);
        composite = std::move(rendered.image);
      }
      const ArgbImage &boundsImage = composite ? *composite : source;
      const Argb reference = policy.border == CropBorder::TopLeftColor ? boundsImage.at(0, 0) : policy.color;
      const auto background = [&](Argb pixel) {
        if (int(pixel >> 24) <= policy.alphaThreshold) return true;
        if (policy.border == CropBorder::Transparent) return false;
        for (int shift : {0, 8, 16, 24})
          if (std::abs(int((pixel >> shift) & 255) - int((reference >> shift) & 255)) > policy.tolerance) return false;
        return true;
      };
      for (qint64 y = 0; y < source.height; ++y) {
        if (cancelled(control)) return failure(QStringLiteral("canvas-cancelled"));
        for (qint64 x = 0; x < source.width; ++x) if (!background(boundsImage.at(x, y))) {
          left = std::min(left, x); top = std::min(top, y);
          right = std::max(right, x + 1); bottom = std::max(bottom, y + 1);
        }
        progress(control, 30 + int((y + 1) * 30 / source.height));
      }
    }  // Release the optional composite before allocating the cropped source.
    if (cancelled(control)) return failure(QStringLiteral("canvas-cancelled"));
    // With no foreground, retain the original valid canvas as a no-op.
    if (right == 0) { left = top = 0; right = source.width; bottom = source.height; }
    CanvasOperation crop;
    crop.action = CanvasAction::Crop;
    crop.start = {double(left), double(top)}; crop.end = {double(right), double(bottom)};
    auto result = apply(document, source, crop,
        {control.cancelled, [&](int p) { progress(control, 60 + p * 40 / 100); }});
    if (!result.error) progress(control, 100);
    return result;
  }
  if ((op.action == CanvasAction::ResizeCanvas || op.action == CanvasAction::ResizeImage) && !validSize(op.width, op.height))
    return failure(op.width < 1 || op.height < 1 ? diagnostic::documentInvalid : diagnostic::documentTooLarge);

  qint64 width = source.width, height = source.height;
  CanvasTransform transform;
  bool resample = false, clipAnnotations = false;
  double angleDelta = 0;
  bool reflection = false;
  switch (op.action) {
    case CanvasAction::Crop: {
      if (!finite(op.start) || !finite(op.end)) return failure(diagnostic::documentInvalid);
      const auto edge = [](double value, qint64 extent, bool upper) {
        value = std::clamp(value, 0.0, double(extent));
        return qint64(upper ? std::ceil(value) : std::floor(value));
      };
      if (op.start.x == op.end.x || op.start.y == op.end.y) return failure(diagnostic::documentInvalid);
      const qint64 left = edge(std::min(op.start.x, op.end.x), width, false);
      const qint64 top = edge(std::min(op.start.y, op.end.y), height, false);
      const qint64 right = edge(std::max(op.start.x, op.end.x), width, true);
      const qint64 bottom = edge(std::max(op.start.y, op.end.y), height, true);
      width = right - left; height = bottom - top;
      transform.dx = -left; transform.dy = -top;
      clipAnnotations = true;
      break;
    }
    case CanvasAction::ResizeCanvas: {
      width = op.width; height = op.height;
      const int anchor = int(op.anchor);
      if (anchor < 0 || anchor > 8) return failure(diagnostic::documentInvalid);
      transform.dx = std::floor((width - source.width) * ((anchor % 3) / 2.0));
      transform.dy = std::floor((height - source.height) * ((anchor / 3) / 2.0));
      clipAnnotations = true;
      break;
    }
    case CanvasAction::ResizeImage: {
      width = op.width; height = op.height;
      if (op.lockAspect && validSize(width, height)) height = std::max<qint64>(1, qint64(std::floor(double(width) * source.height / source.width + .5)));
      transform.xx = double(width) / source.width; transform.yy = double(height) / source.height;
      if (op.interpolation != Interpolation::Nearest && op.interpolation != Interpolation::Bilinear)
        return failure(diagnostic::documentInvalid);
      resample = true;
      break;
    }
    case CanvasAction::RotateClockwise:
      width = source.height; height = source.width;
      transform = {0, -1, 1, 0, double(source.height), 0}; angleDelta = 90;
      break;
    case CanvasAction::RotateCounterClockwise:
      width = source.height; height = source.width;
      transform = {0, 1, -1, 0, 0, double(source.width)}; angleDelta = -90;
      break;
    case CanvasAction::Rotate180:
      transform = {-1, 0, 0, -1, double(source.width), double(source.height)}; angleDelta = 180;
      break;
    case CanvasAction::FlipHorizontal:
      transform = {-1, 0, 0, 1, double(source.width), 0}; angleDelta = 180; reflection = true;
      break;
    case CanvasAction::FlipVertical:
      transform = {1, 0, 0, -1, 0, double(source.height)}; reflection = true;
      break;
    default: return failure(diagnostic::documentInvalid);
  }
  if (!validSize(width, height)) return failure(width < 1 || height < 1 ? diagnostic::documentInvalid : diagnostic::documentTooLarge);
  const bool identity = width == source.width && height == source.height && transform.xx == 1 && transform.yy == 1 &&
                        transform.xy == 0 && transform.yx == 0 && transform.dx == 0 && transform.dy == 0;
  CanvasResult result;
  result.transform = transform;
  if (identity) {
    result.document = document;
    return result;  // no history for unchanged crop or dimensions
  }

  AnnotationDocument next = document;
  next.canvasWidth = width; next.canvasHeight = height;
  std::vector<Annotation> annotations;
  for (const Annotation &a : document.annotations) {
    const auto *r = std::get_if<RectangleAnnotation>(&a);
    if (!r) return failure(QStringLiteral("canvas-annotation-transform-unsupported"));
    if (!validRectangle(*r)) return failure(diagnostic::documentInvalid);
    RectangleAnnotation moved = *r;
    const PointF centre{r->left() / 2 + r->right() / 2, r->top() / 2 + r->bottom() / 2};
    const PointF mapped = transform.map(centre);
    double halfWidth = (r->right() - r->left()) / 2, halfHeight = (r->bottom() - r->top()) / 2;
    if (resample) {
      const double angle = normalizedAngle(r->style.rotationDegrees);
      if (transform.xx != transform.yy && std::fmod(angle, 90.0) != 0)
        return failure(QStringLiteral("canvas-annotation-shear-unsupported"));
      const bool swapped = angle == 90 || angle == 270;
      halfWidth *= swapped ? transform.yy : transform.xx;
      halfHeight *= swapped ? transform.xx : transform.yy;
      // A single stroke width can preserve a uniformly scaled stroke only.
      if (transform.xx != transform.yy && r->style.strokeColor >> 24)
        return failure(QStringLiteral("canvas-annotation-shear-unsupported"));
      moved.style.strokeWidth *= std::sqrt(transform.xx * transform.yy);
    }
    moved.start = {mapped.x - halfWidth, mapped.y - halfHeight};
    moved.end = {mapped.x + halfWidth, mapped.y + halfHeight};
    const double oldAngle = normalizedAngle(r->style.rotationDegrees);
    moved.style.rotationDegrees = normalizedAngle((reflection ? -oldAngle : oldAngle) + angleDelta);
    if (!validRectangle(moved)) return failure(diagnostic::documentInvalid);
    if (clipAnnotations) {
      const auto intersects = intersectsCanvas(moved, width, height);
      if (!intersects) return failure(diagnostic::documentInvalid);
      if (!*intersects) continue;
    }
    annotations.emplace_back(std::move(moved));
  }
  next.annotations = std::move(annotations);
  progress(control, 0);
  ArgbImage image = solidImage(width, height, op.fill);
  if (resample) {
    for (qint64 y = 0; y < height; ++y) {
      if (cancelled(control)) return failure(QStringLiteral("canvas-cancelled"));
      for (qint64 x = 0; x < width; ++x) {
        const double sx = (x + .5) / transform.xx - .5, sy = (y + .5) / transform.yy - .5;
        image.pixels[std::size_t(y * width + x)] = op.interpolation == Interpolation::Bilinear ? bilinear(source, sx, sy) :
          source.at(std::clamp<qint64>(qint64(std::floor(sx + .5)), 0, source.width - 1),
                    std::clamp<qint64>(qint64(std::floor(sy + .5)), 0, source.height - 1));
      }
      progress(control, int((y + 1) * 100 / height));
    }
  } else {
    for (qint64 y = 0; y < source.height; ++y) {
      if (cancelled(control)) return failure(QStringLiteral("canvas-cancelled"));
      for (qint64 x = 0; x < source.width; ++x) {
        const PointF at = transform.mapPixel({double(x), double(y)});
        const qint64 dx = qint64(at.x), dy = qint64(at.y);
        if (dx >= 0 && dy >= 0 && dx < width && dy < height)
          image.pixels[std::size_t(dy * width + dx)] = source.at(x, y);
      }
      progress(control, int((y + 1) * 100 / source.height));
    }
  }
  if (cancelled(control)) return failure(QStringLiteral("canvas-cancelled"));
  result.document = std::move(next); result.image = std::move(image);
  result.changed = true; result.pixelsResampled = resample;
  return result;
}

}  // namespace

PointF CanvasTransform::map(PointF point) const {
  return {xx * point.x + xy * point.y + dx, yx * point.x + yy * point.y + dy};
}
PointF CanvasTransform::mapPixel(PointF point) const {
  const PointF mapped = map({point.x + .5, point.y + .5});
  return {mapped.x - .5, mapped.y - .5};
}

CanvasResult applyCanvasOperation(const AnnotationDocument &document, const ArgbImage &source,
                                  const CanvasOperation &operation, const CanvasControl &control) {
  try { return apply(document, source, operation, control); }
  catch (const std::bad_alloc &) { return failure(diagnostic::documentTooLarge); }
}

}  // namespace xerahs::editor
