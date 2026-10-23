// EDITOR-EFFECTS-001: EE-003, EE-009, EE-010, EE-016, EE-018.
// Flip, rotation and rounded-corner behaviour ported from ShareX ImageEditor
// (ShareX Team, GPLv3); individual census source paths are recorded in
// EffectSpec::referencePath.
//
// The reference mirrors and rotates through a Skia canvas, which moves whole
// pixels without interpolating; this file moves the same pixels directly. The
// reference rounds the corners with an anti-aliased clip path, so the arc edge
// carries anti-aliased alpha here too.
#include "Effect.h"
#include "EffectSupport.h"

#include <QColorSpace>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QRectF>
#include <functional>
#include <memory>
#include <vector>

namespace xerahs::effects {
namespace {
const QString kReferenceDirectory =
    QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Manipulations/");
using namespace support;

// ---- pixel moves -----------------------------------------------------------
// Places every source pixel into a new image. The mapping gives the source
// (column, row) for each destination (column, row); the destination may have a
// different size, which is how the quarter turns swap width and height.
template <class Map>
EffectResult remap(const QImage &source, const QSize &size, const EffectContext &context, Map map) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result(size, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  std::vector<const QRgb *> rows;
  rows.reserve(size_t(source.height()));
  for (int y = 0; y < source.height(); ++y)
    rows.push_back(reinterpret_cast<const QRgb *>(source.constScanLine(y)));
  const bool done = runRows(context, size.height(), 0, 100, [&](int y) {
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < size.width(); ++x) {
      const QPoint from = map(x, y);
      output[x] = rows[size_t(from.y())][from.x()];
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

// The reference mirrors through a scaled canvas: with neither axis selected it
// returns an unchanged copy of the source.
EffectResult flipImage(const QImage &source, bool horizontal, bool vertical, const EffectContext &context) {
  const int width = source.width(), height = source.height();
  return remap(source, source.size(), context, [=](int x, int y) {
    return QPoint(horizontal ? width - 1 - x : x, vertical ? height - 1 - y : y);
  });
}

// The reference turns the canvas by half a turn, which maps the opposite
// corner onto every pixel.
EffectResult rotate180(const QImage &source, const EffectContext &context) {
  const int width = source.width(), height = source.height();
  return remap(source, source.size(), context, [=](int x, int y) {
    return QPoint(width - 1 - x, height - 1 - y);
  });
}

// The reference draws the source into a canvas turned a quarter turn, so the
// result is as wide as the source is high.
EffectResult rotate90Clockwise(const QImage &source, const EffectContext &context) {
  const int height = source.height();
  return remap(source, QSize(height, source.width()), context, [=](int x, int y) {
    return QPoint(y, height - 1 - x);
  });
}

EffectResult rotate90CounterClockwise(const QImage &source, const EffectContext &context) {
  const int width = source.width();
  return remap(source, QSize(source.height(), width), context, [=](int x, int y) {
    return QPoint(width - 1 - y, x);
  });
}

// The reference clips the source to an anti-aliased rounded rectangle: the
// corners lose their alpha and the arc edge is anti-aliased.
EffectResult roundedCorners(const QImage &source, int radius, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int width = source.width(), height = source.height();
  // The reference returns an unchanged copy when no radius is selected.
  if (radius <= 0) return remap(source, source.size(), context, [](int x, int y) { return QPoint(x, y); });
  // A radius larger than the image is scaled down to the shape that fits, as
  // the reference's rounded rectangle does.
  const double rx = qMin(double(radius), width / 2.0);
  const double ry = qMin(double(radius), height / 2.0);
  // The mask holds the anti-aliased coverage of the rounded rectangle.
  QImage mask(width, height, QImage::Format_ARGB32_Premultiplied);
  if (mask.isNull()) return {{}, kFailed};
  mask.fill(Qt::transparent);
  {
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width, height), rx, ry);
    painter.drawPath(path);
  }
  QImage result(width, height, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    const auto *coverage = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const int alpha = qAlpha(input[x]);
      const int covered = qAlpha(coverage[x]);
      output[x] = qRgba(qRed(input[x]), qGreen(input[x]), qBlue(input[x]), (alpha * covered + 127) / 255);
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference, Mode mode,
         QVector<ParamSpec> parameters, ApplyFn fn, bool changesSize = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Manipulations;
  spec.mode = mode;
  spec.parameters = std::move(parameters);
  spec.changesSize = changesSize;
  spec.referencePath = kReferenceDirectory + reference + QStringLiteral("ImageEffect.cs");
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}
}  // namespace

void registerManipulationEffects(EffectRegistry &registry) {
  add(registry, "flip", "Flip", "Flip", Mode::Previewable,
      {boolParam("horizontally", "Horizontally", false),
       boolParam("vertically", "Vertically", false)},
      [](const QImage &source, const ParamValues &params, const EffectContext &context) {
        return flipImage(source, boolOf(params, "horizontally"), boolOf(params, "vertically"), context);
      });
  add(registry, "flip_horizontal", "Flip horizontal", "FlipHorizontal", Mode::Immediate, {},
      [](const QImage &source, const ParamValues &, const EffectContext &context) {
        return flipImage(source, true, false, context);
      });
  add(registry, "flip_vertical", "Flip vertical", "FlipVertical", Mode::Immediate, {},
      [](const QImage &source, const ParamValues &, const EffectContext &context) {
        return flipImage(source, false, true, context);
      });
  add(registry, "rotate_180", "Rotate 180°", "Rotate180", Mode::Immediate, {},
      [](const QImage &source, const ParamValues &, const EffectContext &context) {
        return rotate180(source, context);
      });
  add(registry, "rotate_90_clockwise", "Rotate 90° clockwise", "Rotate90CW", Mode::Immediate, {},
      [](const QImage &source, const ParamValues &, const EffectContext &context) {
        return rotate90Clockwise(source, context);
      },
      true);
  add(registry, "rotate_90_counter_clockwise", "Rotate 90° counter clockwise", "Rotate90CCW", Mode::Immediate, {},
      [](const QImage &source, const ParamValues &, const EffectContext &context) {
        return rotate90CounterClockwise(source, context);
      },
      true);
  add(registry, "rounded_corners", "Rounded Corners", "RoundedCorners", Mode::Previewable,
      {intParam("corner_radius", "Corner radius", 20, 0, 500)},
      [](const QImage &source, const ParamValues &params, const EffectContext &context) {
        return roundedCorners(source, intOf(params, "corner_radius"), context);
      });
}
}  // namespace xerahs::effects
