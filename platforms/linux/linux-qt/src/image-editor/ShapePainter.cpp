#include "image-editor/ShapePainter.h"

#include "image-editor/FreehandPath.h"
#include "image-editor/RectangleGeometry.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QGlyphRun>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <vector>

namespace xerahs::editor {

bool nativeFontFamilyAvailable(const QString &family) {
  return !family.isEmpty() && (QFontDatabase::hasFamily(family) ||
      QFontDatabase::families().contains(family, Qt::CaseInsensitive));
}

namespace {

QColor color(Argb value) { return QColor::fromRgba(value); }

QRectF bounds(const RectangleAnnotation &shape) {
  return QRectF(QPointF(shape.left(), shape.top()), QPointF(shape.right(), shape.bottom()));
}

QPointF mapped(const RectangleAnnotation &shape, const PointF &fraction) {
  return {shape.left() + fraction.x * (shape.right() - shape.left()), shape.top() + fraction.y * (shape.bottom() - shape.top())};
}

// Three box-blur passes approximate a Gaussian of the same radius.
void boxBlur(QImage &image, int radius) {
  if (radius < 1 || image.isNull()) return;
  const int w = image.width(), h = image.height();
  std::vector<quint32> line(std::size_t(std::max(w, h)));
  const auto pass = [&](bool horizontal) {
    const int outer = horizontal ? h : w, inner = horizontal ? w : h;
    for (int o = 0; o < outer; ++o) {
      const auto at = [&](int i) -> QRgb & {
        return reinterpret_cast<QRgb *>(image.scanLine(horizontal ? o : i))[horizontal ? i : o];
      };
      for (int i = 0; i < inner; ++i) line[std::size_t(i)] = at(i);
      std::array<qint64, 4> sum{0, 0, 0, 0};
      const auto add = [&](int i, int sign) {
        const QRgb p = line[std::size_t(std::clamp(i, 0, inner - 1))];
        sum[0] += sign * qAlpha(p); sum[1] += sign * qRed(p); sum[2] += sign * qGreen(p); sum[3] += sign * qBlue(p);
      };
      for (int i = -radius; i <= radius; ++i) add(i, 1);
      const qint64 count = 2 * radius + 1;
      for (int i = 0; i < inner; ++i) {
        at(i) = qRgba(int(sum[1] / count), int(sum[2] / count), int(sum[3] / count), int(sum[0] / count));
        add(i + radius + 1, 1);
        add(i - radius, -1);
      }
    }
  };
  for (int n = 0; n < 3; ++n) { pass(true); pass(false); }
}

// EA-011: the declared region of an effect in document coordinates: the
// bounds (rectangle or inscribed ellipse) rotated about the bounds centre.
QPainterPath effectRegion(const RectangleAnnotation &shape) {
  QPainterPath path;
  if (shape.content.isEllipse) path.addEllipse(bounds(shape)); else path.addRect(bounds(shape));
  return shapeRotation(shape).map(path);
}

// Document pixels the effect may read or write: the region's integer bounding
// box clipped to the source. Empty when the region lies outside the canvas.
QRect effectTarget(const QPainterPath &region, const QImage &source) {
  const QRectF box = region.boundingRect();
  if (!std::isfinite(box.left()) || !std::isfinite(box.top()) || !std::isfinite(box.right()) || !std::isfinite(box.bottom())) return {};
  const QRectF canvas(source.rect());
  const QRectF clipped = box.intersected(canvas);
  if (clipped.isEmpty()) return {};
  return clipped.toAlignedRect().intersected(source.rect());
}

// Every pixel is read from `source`, the immutable source image. The result
// replaces (not blends with) the canvas inside the region, so overlapping
// effects never see each other's output (EA-011).
void paintRegion(QPainter &painter, const RectangleAnnotation &shape, const QImage &source) {
  const QPainterPath region = effectRegion(shape);
  const QRect target = effectTarget(region, source);
  if (target.isEmpty()) return;
  const int strength = std::max(1, int(std::lround(std::min(shapeStrength(shape), 1000.0))));
  QImage pixels;
  if (shape.type() == shape::blur) {
    // Pad by the blur's reach so the result inside the region does not depend on
    // where the region's own edge happens to be; padding stops at the canvas.
    const int pad = std::min(3 * strength, std::max(source.width(), source.height()));
    const QRect padded = target.adjusted(-pad, -pad, pad, pad).intersected(source.rect());
    QImage work = source.copy(padded).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    boxBlur(work, strength);
    pixels = work.copy(target.translated(-padded.topLeft()));
  } else {
    pixels = source.copy(target).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QSize reduced(std::max(1, pixels.width() / strength), std::max(1, pixels.height() / strength));
    pixels = pixels.scaled(reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .scaled(target.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
  }
  painter.save();
  painter.setClipPath(region, Qt::IntersectClip);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.drawImage(target.topLeft(), pixels);
  painter.restore();
}

void paintHighlight(QPainter &painter, const RectangleAnnotation &shape, const QImage &source) {
  const QPainterPath region = effectRegion(shape);
  const QRect target = effectTarget(region, source);
  if (target.isEmpty()) return;
  const QColor tint = qAlpha(shape.style.fillColor) > 0 ? color(shape.style.fillColor) : QColor(255, 235, 59);
  QImage pixels = source.copy(target).convertToFormat(QImage::Format_ARGB32_Premultiplied);
  {
    QPainter inner(&pixels);
    inner.setCompositionMode(QPainter::CompositionMode_Multiply);
    inner.fillRect(pixels.rect(), tint);
  }
  painter.save();
  painter.setClipPath(region, Qt::IntersectClip);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.drawImage(target.topLeft(), pixels);
  painter.restore();
}

// Dims everything on the canvas outside the region; `blurAmount` first
// replaces that area with a blur of the source.
void paintSpotlight(QPainter &painter, const RectangleAnnotation &shape, const QImage &source) {
  QPainterPath outside;
  outside.addRect(QRectF(source.rect()));
  outside = outside.subtracted(effectRegion(shape));
  if (outside.isEmpty()) return;
  painter.save();
  painter.setClipPath(outside, Qt::IntersectClip);
  const int radius = int(std::lround(std::min(shape.content.blurAmount, kMaxBlurAmount)));
  if (radius >= 1) {
    QImage blurred = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    boxBlur(blurred, radius);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPoint(0, 0), blurred);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  }
  painter.fillRect(source.rect(), QColor(0, 0, 0, std::clamp(int(std::lround(shapeStrength(shape) * 2.55)), 0, 255)));
  painter.restore();
}

// Magnify: the centre of the bounds, enlarged by `strength`, read from the
// source and drawn only inside the (possibly elliptical, rotated) region.
void paintMagnify(QPainter &painter, const RectangleAnnotation &shape, const QImage &source) {
  const QRectF box = bounds(shape);
  const double zoom = std::max(1.0, std::min(shapeStrength(shape), 1000.0));
  const QSizeF size(box.width() / zoom, box.height() / zoom);
  const QRectF from(box.center() - QPointF(size.width() / 2, size.height() / 2), size);
  const QRectF readable = from.intersected(QRectF(source.rect()));
  if (readable.isEmpty()) return;
  // Map only the part of `from` that lies on the canvas; never read outside it.
  const QRectF to(box.left() + (readable.left() - from.left()) * zoom, box.top() + (readable.top() - from.top()) * zoom,
                  readable.width() * zoom, readable.height() * zoom);
  QPainterPath local;
  if (shape.content.isEllipse) local.addEllipse(box); else local.addRect(box);
  painter.save();
  painter.setWorldTransform(shapeRotation(shape), true);
  painter.setClipPath(local, Qt::IntersectClip);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.drawImage(to, source, readable);
  painter.restore();
}

// Mean colour of the one-pixel ring just outside the box: the documented
// deterministic SmartEraser fill (EA-012). It is not content-aware removal.
QColor ringColour(const QImage &source, const QRect &box) {
  const QRect ring = box.adjusted(-1, -1, 1, 1);
  qint64 sum[4] = {0, 0, 0, 0}, count = 0;
  const auto add = [&](int x, int y) {
    if (!source.rect().contains(x, y)) return;
    const QRgb p = source.pixel(x, y);
    sum[0] += qAlpha(p); sum[1] += qRed(p); sum[2] += qGreen(p); sum[3] += qBlue(p); ++count;
  };
  for (int x = ring.left(); x <= ring.right(); ++x) { add(x, ring.top()); add(x, ring.bottom()); }
  for (int y = ring.top() + 1; y < ring.bottom(); ++y) { add(ring.left(), y); add(ring.right(), y); }
  if (count == 0) return Qt::white;
  return QColor(int(sum[1] / count), int(sum[2] / count), int(sum[3] / count), int(sum[0] / count));
}

int horizontalAlignment(const ShapeContent &content, int fallback) {
  if (content.textAlignment == u"left") return Qt::AlignLeft;
  if (content.textAlignment == u"center") return Qt::AlignHCenter;
  if (content.textAlignment == u"right") return Qt::AlignRight;
  return fallback;
}

QFont textFont(QFont font, const RectangleAnnotation &shape) {
  // Resolve against installed native families without changing the document.
  // An unavailable saved family uses exactly the same font as legacy content.
  if (!shape.content.fontFamily.isEmpty() && nativeFontFamilyAvailable(shape.content.fontFamily)) font.setFamily(shape.content.fontFamily);
  font.setPixelSize(std::max(1, int(std::lround(shapeFontSize(shape)))));
  if (shape.content.textBold.has_value()) font.setBold(*shape.content.textBold);
  if (shape.content.textItalic.has_value()) font.setItalic(*shape.content.textItalic);
  return font;
}


// EA-008: the reference draws Text with an outline behind the letters: the glyph outlines stroked with the
// annotation's border colour and thickness, then filled with the text colour on top. One layout supplies
// both the outline and the fill so they cannot drift apart.
void paintOutlinedText(QPainter &painter, const QRectF &box, const QString &text, Qt::Alignment alignment,
                       const QColor &fill, const QColor &border, double thickness) {
  QTextLayout layout(text, painter.font());
  QTextOption option(alignment | Qt::AlignTop); option.setWrapMode(QTextOption::WordWrap);
  layout.setTextOption(option);
  double y = 0;
  layout.beginLayout();
  for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
    line.setLineWidth(box.width()); line.setPosition({0, y}); y += line.height();
  }
  layout.endLayout();
  QPainterPath outline;
  for (const QGlyphRun &run : layout.glyphRuns()) {
    const QRawFont raw = run.rawFont(); const auto indexes = run.glyphIndexes(); const auto positions = run.positions();
    for (qsizetype i = 0; i < indexes.size(); ++i) outline.addPath(raw.pathForGlyph(indexes[i]).translated(positions[i] + box.topLeft()));
  }
  painter.save();
  painter.setClipRect(box.adjusted(-thickness, -thickness, thickness, thickness), Qt::IntersectClip);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(border, thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(outline);
  painter.setPen(fill);
  layout.draw(&painter, box.topLeft());
  painter.restore();
}

Qt::PenStyle penStyleOf(BorderStyle style) {
  switch (style) {
    case BorderStyle::Dash: return Qt::DashLine;
    case BorderStyle::Dot: return Qt::DotLine;
    case BorderStyle::DashDot: return Qt::DashDotLine;
    case BorderStyle::DashDotDot: return Qt::DashDotDotLine;
    case BorderStyle::Solid: break;
  }
  return Qt::SolidLine;
}

// EA-005: the stroke of a Rectangle or Ellipse. Dashed styles use flat caps,
// otherwise round caps would fuse dashes and dots back into a solid line.
QPen borderPen(const RectangleStyle &style, bool rectangle) {
  const bool solid = style.borderStyle == BorderStyle::Solid;
  QPen pen(color(style.strokeColor), style.strokeWidth, penStyleOf(style.borderStyle),
           solid ? (rectangle ? Qt::SquareCap : Qt::RoundCap) : Qt::FlatCap, rectangle ? Qt::MiterJoin : Qt::RoundJoin);
  return pen;
}

void drawBox(QPainter &painter, const RectangleAnnotation &shape, const QRectF &box) {
  painter.setBrush(color(shape.style.fillColor));
  painter.setPen(borderPen(shape.style, shape.type() == shape::rectangle));
  if (shape.type() == shape::ellipse) { painter.drawEllipse(box); return; }
  // Corner radius is Rectangle only; clamp so opposite corners never overlap.
  const double radius = std::min({shape.style.cornerRadius, box.width() / 2, box.height() / 2});
  if (radius > 0) painter.drawRoundedRect(box, radius, radius); else painter.drawRect(box);
}

// EA-005: the shadow is the shape's own silhouette (fill and stroke alpha),
// offset in document axes, blurred, then tinted. It is rasterized at document
// resolution so the canvas (any zoom) and the exporter produce the same
// pixels; the result is drawn under the shape before it.
// `extent` is the unrotated area the silhouette occupies and `draw` paints it.
void paintShadow(QPainter &painter, const RectangleAnnotation &shape, const QRectF &extent, const std::function<void(QPainter &)> &draw) {
  const ShapeShadow &shadow = shape.style.shadow;
  const int radius = std::max(0, int(std::lround(shadow.blurRadius)));
  const QTransform rotation = shapeRotation(shape);
  const double reach = shape.style.strokeWidth / 2 + 3.0 * radius + 2;
  const QRect region = rotation.mapRect(extent).adjusted(-reach, -reach, reach, reach).toAlignedRect();
  constexpr qint64 kMaxShadowPixels = 64LL * 1024 * 1024;
  if (region.width() < 1 || region.height() < 1 || qint64(region.width()) * region.height() > kMaxShadowPixels) return;
  QImage mask(region.size(), QImage::Format_ARGB32_Premultiplied);
  mask.fill(Qt::transparent);
  {
    QPainter inner(&mask);
    inner.setRenderHint(QPainter::Antialiasing);
    inner.translate(-region.topLeft());
    inner.setWorldTransform(rotation, true);
    draw(inner);
  }
  boxBlur(mask, radius);
  QPainter tint(&mask);
  tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
  QColor c = color(shadow.color);
  c.setAlphaF(std::clamp(c.alphaF() * shadow.opacity, 0.0, 1.0));
  tint.fillRect(mask.rect(), c);
  tint.end();
  painter.save();
  painter.drawImage(QPointF(region.topLeft()) + QPointF(shadow.offsetX, shadow.offsetY), mask);
  painter.restore();
}

void paintBalloon(QPainter &painter, const RectangleAnnotation &shape) {
  const RectangleStyle &style = shape.style;
  const QRectF box = bounds(shape);
  const double radius = std::min(12.0, std::min(box.width(), box.height()) / 4);
  QPainterPath body;
  body.addRoundedRect(box, radius, radius);
  if (!shape.content.points.empty()) {
    const QPointF tip = mapped(shape, shape.content.points[0]);
    const QPointF centre = box.center();
    const bool vertical = std::abs(tip.y() - centre.y()) / box.height() >= std::abs(tip.x() - centre.x()) / box.width();
    // The attachment must fit between the rounded corners, including tiny
    // valid boxes. Otherwise std::clamp receives reversed bounds.
    const double available = (vertical ? box.width() : box.height()) / 2 - radius;
    const double half = std::min(std::max(6.0, std::min(box.width(), box.height()) / 6), available);
    QPainterPath tail;
    tail.moveTo(tip);
    if (vertical) { tail.lineTo(std::clamp(tip.x(), box.left() + radius + half, box.right() - radius - half) - half, centre.y());
                    tail.lineTo(std::clamp(tip.x(), box.left() + radius + half, box.right() - radius - half) + half, centre.y()); }
    else { tail.lineTo(centre.x(), std::clamp(tip.y(), box.top() + radius + half, box.bottom() - radius - half) - half);
           tail.lineTo(centre.x(), std::clamp(tip.y(), box.top() + radius + half, box.bottom() - radius - half) + half); }
    tail.closeSubpath();
    body = body.united(tail);
  }
  const QColor fill = shape.content.textFillColor.has_value() ? color(*shape.content.textFillColor) :
      (qAlpha(style.fillColor) > 0 ? color(style.fillColor) : QColor(Qt::white));
  painter.setBrush(fill);
  painter.setPen(QPen(color(shape.content.balloonBorderColor.value_or(style.strokeColor)), style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(body);
  painter.setFont(textFont(painter.font(), shape));
  painter.setPen(shape.content.textColor.has_value() ? color(*shape.content.textColor) : (fill.lightnessF() > 0.5 ? QColor(Qt::black) : QColor(Qt::white)));
  const double pad = std::min(8.0, std::min(box.width(), box.height()) / 4);
  painter.drawText(box.adjusted(pad, pad, -pad, -pad), horizontalAlignment(shape.content, Qt::AlignHCenter) | Qt::AlignVCenter | Qt::TextWordWrap, shape.content.text);
}

// EA-006: the drawable parts of a Line or Arrow, in document coordinates before
// the shape's own rotation. `shaft` takes the border style; `filled` and
// `stroked` are the arrow heads and always use a solid pen (dashing a head
// would break its outline). The head geometry follows the reference XerahS
// ArrowAnnotation: head half width is stroke width times 2 (classic, double),
// 3 (modern) or 1.75 (basic, line), head length is three times that.
struct LineParts {
  QPainterPath shaft, filled, stroked;
  QRectF extent() const { return shaft.boundingRect().united(filled.boundingRect()).united(stroked.boundingRect()); }
};

QPointF qpoint(PointF p) { return {p.x, p.y}; }

bool unit(QPointF tangent, QPointF &u) {
  const double length = std::hypot(tangent.x(), tangent.y());
  if (!(length > 0)) return false;
  u = tangent / length;
  return true;
}

// Filled classic cap: tip, left base, a quadratic back curve to the right base.
void addClassicCap(QPainterPath &out, QPointF tip, QPointF tangent, double halfWidth) {
  QPointF u;
  if (!unit(tangent, u)) return;
  const QPointF perpendicular(-u.y(), u.x());
  const QPointF base = tip - u * (halfWidth * 3);
  out.moveTo(tip);
  out.lineTo(base + perpendicular * halfWidth);
  out.quadTo(tip - u * (halfWidth * 2), base - perpendicular * halfWidth);
  out.closeSubpath();
}

LineParts lineParts(const RectangleAnnotation &shape) {
  LineParts parts;
  const auto &points = shape.content.points;
  const bool arrow = shape.type() == shape::arrow;
  const ArrowStyle style = shape.style.arrowStyle;
  if (points.size() > 3) {  // legacy polyline: every point in order, head on the last segment
    for (std::size_t i = 0; i < points.size(); ++i) {
      const QPointF p = mapped(shape, points[i]);
      if (i == 0) parts.shaft.moveTo(p); else parts.shaft.lineTo(p);
    }
    if (arrow) {
      const QPointF tip = mapped(shape, points.back());
      addClassicCap(parts.filled, tip, tip - mapped(shape, points[points.size() - 2]), std::max(shape.style.strokeWidth * 2, 6.0));
    }
    return parts;
  }
  const QPointF start = qpoint(lineStart(shape)), end = qpoint(lineEnd(shape));
  const bool curved = lineHasCurve(shape);
  QPointF controlPoint;
  parts.shaft.moveTo(start);
  if (curved) {
    controlPoint = qpoint(quadraticControl({start.x(), start.y()}, {end.x(), end.y()}, *lineAnchor(shape)));
    parts.shaft.quadTo(controlPoint, end);
  } else {
    parts.shaft.lineTo(end);
  }
  if (!arrow) return parts;
  // Tangents follow the curve (reference: control point, falling back to the chord).
  const auto tangentAt = [&](bool atEnd) {
    QPointF t = atEnd ? end - (curved ? controlPoint : start) : (curved ? controlPoint : end) - start;
    if (std::hypot(t.x(), t.y()) < 0.001) t = end - start;
    return t;
  };
  const double width = shape.style.strokeWidth;
  const QPointF endTangent = tangentAt(true);
  QPointF u;
  switch (style) {
    case ArrowStyle::Modern: {
      // Straight, tapered: a polygon from the start to a wide head (never curved).
      if (!unit(end - start, u)) break;
      parts.shaft = QPainterPath();
      const double size = width * 3, headHeight = size * 2.5, baseWidth = size * 1.5;
      const double wing = baseWidth * std::tan(3.14159265358979323846 / 5.14), shaftWidth = baseWidth * 0.25;
      const QPointF perpendicular(-u.y(), u.x()), base = end - u * headHeight;
      parts.filled.moveTo(start);
      parts.filled.lineTo(base + perpendicular * shaftWidth);
      parts.filled.lineTo(base + perpendicular * wing);
      parts.filled.lineTo(end);
      parts.filled.lineTo(base - perpendicular * wing);
      parts.filled.lineTo(base - perpendicular * shaftWidth);
      parts.filled.closeSubpath();
      break;
    }
    case ArrowStyle::Basic: {
      if (!unit(endTangent, u)) break;
      const double half = width * 1.75;
      const QPointF perpendicular(-u.y(), u.x()), base = end - u * (half * 3);
      parts.filled.moveTo(end); parts.filled.lineTo(base + perpendicular * half); parts.filled.lineTo(base - perpendicular * half);
      parts.filled.closeSubpath();
      break;
    }
    case ArrowStyle::Line: {
      if (!unit(endTangent, u)) break;
      const double half = width * 1.75;
      const QPointF perpendicular(-u.y(), u.x()), base = end - u * (half * 3);
      parts.stroked.moveTo(base + perpendicular * half); parts.stroked.lineTo(end); parts.stroked.lineTo(base - perpendicular * half);
      break;
    }
    case ArrowStyle::Double:
      addClassicCap(parts.filled, start, -tangentAt(false), width * 2);
      addClassicCap(parts.filled, end, endTangent, width * 2);
      break;
    case ArrowStyle::Classic:
      addClassicCap(parts.filled, end, endTangent, width * 2);
      break;
  }
  return parts;
}

void drawLineParts(QPainter &painter, const RectangleAnnotation &shape, const LineParts &parts) {
  const RectangleStyle &style = shape.style;
  const QColor ink = color(style.strokeColor);
  painter.setBrush(Qt::NoBrush);
  // Dashed styles use flat caps, otherwise round caps would fuse dashes back into a solid line.
  const bool solid = style.borderStyle == BorderStyle::Solid;
  QPen shaftPen(ink, style.strokeWidth, penStyleOf(style.borderStyle), solid ? Qt::RoundCap : Qt::FlatCap, Qt::RoundJoin);
  painter.setPen(shaftPen);
  painter.drawPath(parts.shaft);
  const QPen headPen(ink, style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(headPen);
  painter.setBrush(ink);
  painter.drawPath(parts.filled);
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(parts.stroked);
}

}  // namespace

QTransform shapeRotation(const RectangleAnnotation &r) {
  const QPointF centre{r.left() / 2 + r.right() / 2, r.top() / 2 + r.bottom() / 2};
  QTransform transform;
  transform.translate(centre.x(), centre.y());
  transform.rotate(std::fmod(r.style.rotationDegrees, 360.0));
  transform.translate(-centre.x(), -centre.y());
  return transform;
}

double shapeFontSize(const RectangleAnnotation &shape) {
  if (shape.content.fontSize > 0) return shape.content.fontSize;
  return shape.type() == shape::step ? std::max(8.0, (shape.bottom() - shape.top()) * 0.55) : 24.0;
}

double shapeStrength(const RectangleAnnotation &shape) {
  if (shape.content.strength > 0) return shape.content.strength;
  if (shape.type() == shape::blur) return 8.0;
  if (shape.type() == shape::magnify) return 2.0;
  if (shape.type() == shape::spotlight) return 60.0;
  return 12.0;
}

QImage decodeAsset(const QByteArray &png) {
  // Per-thread cache: renders run on workers and the canvas repaints often.
  thread_local QHash<QByteArray, QImage> cache;
  const QByteArray digest = QCryptographicHash::hash(png, QCryptographicHash::Sha1);
  if (const auto found = cache.constFind(digest); found != cache.cend()) return *found;
  QBuffer buffer;
  buffer.setData(png);
  QImageReader reader(&buffer, "png");
  const QSize size = reader.size();
  if (!size.isValid() || size.width() > kMaxDimension || size.height() > kMaxDimension ||
      qint64(size.width()) * size.height() > 64LL * 1024 * 1024) return {};
  QImage image = reader.read();
  if (image.isNull()) return {};
  if (cache.size() >= 64) cache.clear();
  cache.insert(digest, image);
  return image;
}

void paintShape(QPainter &painter, const RectangleAnnotation &shape, const QImage &source, const EmbeddedImages *assets) {
  if (!shape.visible) return;
  const RectangleStyle &style = shape.style;
  const QRectF box = bounds(shape);
  const QString &type = shape.type();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setOpacity(painter.opacity() * style.opacity);
  // EA-005: the shadow offset follows document axes, so it is drawn before the
  // shape's own rotation is applied.
  std::optional<LineParts> line;  // EA-006: Line and Arrow share the shape painter
  if (type == shape::line || type == shape::arrow) line = lineParts(shape);
  if (hasShapeDecoration(type) && style.shadow.enabled) {
    if (line) paintShadow(painter, shape, line->extent(), [&](QPainter &inner) { drawLineParts(inner, shape, *line); });
    else paintShadow(painter, shape, box, [&](QPainter &inner) { drawBox(inner, shape, box); });
  }
  // EA-011: effect types work in document space (their region is already
  // rotated) and never draw outside it.
  if (type == shape::highlight || type == shape::blur || type == shape::pixelate || type == shape::magnify ||
      type == shape::spotlight) {
    if (type == shape::highlight) paintHighlight(painter, shape, source);
    else if (type == shape::magnify) paintMagnify(painter, shape, source);
    else if (type == shape::spotlight) paintSpotlight(painter, shape, source);
    else paintRegion(painter, shape, source);
    painter.restore();
    return;
  }
  painter.setWorldTransform(shapeRotation(shape), true);
  const QPen stroke(color(style.strokeColor), style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  if (type == shape::rectangle || type == shape::ellipse) {
    drawBox(painter, shape, box);
  } else if (line) {
    drawLineParts(painter, shape, *line);
  } else if (isPathShape(type)) {
    QPainterPath path;
    for (std::size_t i = 0; i < shape.content.points.size(); ++i) {
      const QPointF p = mapped(shape, shape.content.points[i]);
      if (i == 0) path.moveTo(p); else path.lineTo(p);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(stroke);
    painter.drawPath(path);
  } else if (type == shape::text) {
    const Argb fill = shape.content.textFillColor.value_or(style.fillColor);
    if (qAlpha(fill) > 0) painter.fillRect(box, color(fill));
    painter.setFont(textFont(painter.font(), shape));
    const QColor textColor = color(shape.content.textColor.value_or(style.strokeColor));
    const double pad = std::min(4.0, std::min(box.width(), box.height()) / 4);
    const QRectF textBox = box.adjusted(pad, pad, -pad, -pad);
    const auto alignment = Qt::Alignment(horizontalAlignment(shape.content, Qt::AlignLeft));
    if (shape.content.textBorderColor && qAlpha(*shape.content.textBorderColor) > 0 && style.strokeWidth > 0)
      paintOutlinedText(painter, textBox, shape.content.text, alignment, textColor, color(*shape.content.textBorderColor), style.strokeWidth);
    else {
      painter.setPen(textColor);
      painter.drawText(textBox, int(alignment | Qt::AlignTop) | Qt::TextWordWrap, shape.content.text);
    }
  } else if (type == shape::step) {
    const QColor fill = qAlpha(style.fillColor) > 0 ? color(style.fillColor) : color(style.strokeColor);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    // EA-009: the optional tail is the two tangents from the tail tip to the circle (reference
    // NumberAnnotation.TryGetTailPolygon). It is drawn with the circle as one silhouette, and is
    // omitted while the tip is inside or on the circle.
    QPainterPath silhouette; silhouette.addEllipse(box);
    if (!shape.content.points.empty()) {
      const QPointF tip = mapped(shape, shape.content.points[0]), centre = box.center();
      const double radius = std::min(box.width(), box.height()) / 2, dx = tip.x() - centre.x(), dy = tip.y() - centre.y();
      const double length = std::hypot(dx, dy);
      if (radius > 0.001 && length > radius + 0.001) {
        const QPointF direction(dx / length, dy / length), perpendicular(-direction.y(), direction.x());
        const double projection = radius * radius / length, offset = radius * std::sqrt(length * length - radius * radius) / length;
        const QPointF tangentCentre = centre + direction * projection;
        QPainterPath tail; tail.moveTo(tangentCentre + perpendicular * offset); tail.lineTo(tip); tail.lineTo(tangentCentre - perpendicular * offset); tail.closeSubpath();
        silhouette = silhouette.united(tail);
      }
    }
    painter.drawPath(silhouette);
    QFont font = painter.font();
    font.setPixelSize(std::max(1, int(std::lround(shapeFontSize(shape)))));
    font.setBold(true);
    const QString label = formatStepValue(shape.content.stepValue, shape.content.stepStyle);
    painter.setFont(font);
    // Alphabetic and Roman labels are wider than a digit: shrink them to stay inside the circle.
    const double available = box.width() * 0.8, wide = QFontMetricsF(font).horizontalAdvance(label);
    if (wide > available && available > 0) {
      font.setPixelSize(std::max(1, int(std::floor(font.pixelSize() * available / wide))));
      painter.setFont(font);
    }
    painter.setPen(fill.lightnessF() > 0.6 ? Qt::black : Qt::white);
    painter.drawText(box, Qt::AlignCenter, label);
  } else if (type == shape::speechBalloon) {
    paintBalloon(painter, shape);
  } else if (isAssetShape(type)) {
    QImage asset;
    if (assets)
      for (const auto &[key, png] : *assets) if (key == shape.content.imageKey) { asset = decodeAsset(png); break; }
    if (asset.isNull()) {
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
      painter.drawRect(box);
      painter.drawLine(box.topLeft(), box.bottomRight());
      painter.drawLine(box.topRight(), box.bottomLeft());
    } else {
      // EA-010: keep the asset's aspect ratio, centred in its bounds.
      const QSizeF fitted = QSizeF(asset.size()).scaled(box.size(), Qt::KeepAspectRatio);
      const QRectF target(box.center() - QPointF(fitted.width() / 2, fitted.height() / 2), fitted);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(target, asset);
    }
  } else if (type == shape::smartEraser) {
    painter.fillRect(box, ringColour(source, box.toAlignedRect()));
  }
  painter.restore();
}

std::optional<ShapeGeometry> shapeGeometry(const QString &type, const std::vector<PointF> &gesture) {
  if (gesture.empty() || !shapeTypes().contains(type)) return std::nullopt;
  for (const PointF &p : gesture) if (!std::isfinite(p.x) || !std::isfinite(p.y)) return std::nullopt;
  const PointF first = gesture.front(), last = gesture.back();
  if (isPathShape(type)) {
    std::vector<PointF> kept;
    if (type == shape::freehand) {
      kept = simplifyFreehand(gesture);  // EA-007: documented tolerance, corners retained
    } else {
      kept = {first, last};
    }
    if (kept.size() < 2) return std::nullopt;
    double left = kept[0].x, top = kept[0].y, right = left, bottom = top;
    for (const PointF &p : kept) {
      left = std::min(left, p.x); top = std::min(top, p.y); right = std::max(right, p.x); bottom = std::max(bottom, p.y);
    }
    if (right - left < 1 && bottom - top < 1) return std::nullopt;  // a click is not a line
    if (right - left < 1) right = left + 1;
    if (bottom - top < 1) bottom = top + 1;
    ShapeGeometry geometry{{left, top}, {right, bottom}, {}};
    for (const PointF &p : kept) geometry.points.push_back({(p.x - left) / (right - left), (p.y - top) / (bottom - top)});
    return geometry;
  }
  double left = std::min(first.x, last.x), top = std::min(first.y, last.y);
  double right = std::max(first.x, last.x), bottom = std::max(first.y, last.y);
  const bool click = right - left < 4 && bottom - top < 4;
  if (click && type == shape::step) return ShapeGeometry{{first.x - 16, first.y - 16}, {first.x + 16, first.y + 16}, {}};
  if (click && type == shape::speechBalloon) return ShapeGeometry{{first.x, first.y}, {first.x + 240, first.y + 80}, {}};
  if (click && type == shape::text) return ShapeGeometry{{first.x, first.y}, {first.x + 320, first.y + 48}, {}};
  if (!(right > left) || !(bottom > top)) return std::nullopt;
  return ShapeGeometry{{left, top}, {right, bottom}, {}};
}

qint64 nextStepValue(const AnnotationDocument &document, const StepSequence &sequence) {
  qint64 highest = 0;
  for (const auto &annotation : document.annotations)
    if (const auto *shape = std::get_if<RectangleAnnotation>(&annotation); shape && shape->type() == shape::step && !sequence.retired.contains(shape->id))
      highest = std::max(highest, shape->content.stepValue);
  return std::max(highest + 1, std::clamp(sequence.start, kMinStepStart, kMaxStepStart));
}

void resetStepSequence(StepSequence &sequence, const AnnotationDocument &document) {
  for (const auto &annotation : document.annotations)
    if (const auto *shape = std::get_if<RectangleAnnotation>(&annotation); shape && shape->type() == shape::step) sequence.retired.insert(shape->id);
}

}  // namespace xerahs::editor
