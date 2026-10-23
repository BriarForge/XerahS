#include "image-editor/ShapePainter.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFont>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace xerahs::editor {

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

QImage sampled(const QImage &source, const QRectF &box, QRect &target) {
  target = box.toAlignedRect().intersected(source.rect());
  if (target.isEmpty()) return {};
  return source.copy(target).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void paintRegion(QPainter &painter, const RectangleAnnotation &shape, const QImage &source) {
  QRect target;
  QImage pixels = sampled(source, bounds(shape), target);
  if (pixels.isNull()) return;
  const int strength = std::max(1, int(std::lround(shapeStrength(shape))));
  if (shape.type() == shape::blur) {
    boxBlur(pixels, strength);
  } else {
    const QSize reduced(std::max(1, pixels.width() / strength), std::max(1, pixels.height() / strength));
    pixels = pixels.scaled(reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .scaled(pixels.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
  }
  painter.save();
  painter.setClipRect(bounds(shape), Qt::IntersectClip);
  painter.drawImage(target.topLeft(), pixels);
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

void paintBalloon(QPainter &painter, const RectangleAnnotation &shape) {
  const RectangleStyle &style = shape.style;
  const QRectF box = bounds(shape);
  const double radius = std::min(12.0, std::min(box.width(), box.height()) / 4);
  QPainterPath body;
  body.addRoundedRect(box, radius, radius);
  if (!shape.content.points.empty()) {
    const QPointF tip = mapped(shape, shape.content.points[0]);
    const QPointF centre = box.center();
    const double half = std::max(6.0, std::min(box.width(), box.height()) / 6);
    const bool vertical = std::abs(tip.y() - centre.y()) / box.height() >= std::abs(tip.x() - centre.x()) / box.width();
    QPainterPath tail;
    tail.moveTo(tip);
    if (vertical) { tail.lineTo(std::clamp(tip.x(), box.left() + radius + half, box.right() - radius - half) - half, centre.y());
                    tail.lineTo(std::clamp(tip.x(), box.left() + radius + half, box.right() - radius - half) + half, centre.y()); }
    else { tail.lineTo(centre.x(), std::clamp(tip.y(), box.top() + radius + half, box.bottom() - radius - half) - half);
           tail.lineTo(centre.x(), std::clamp(tip.y(), box.top() + radius + half, box.bottom() - radius - half) + half); }
    tail.closeSubpath();
    body = body.united(tail);
  }
  const QColor fill = qAlpha(style.fillColor) > 0 ? color(style.fillColor) : QColor(Qt::white);
  painter.setBrush(fill);
  painter.setPen(QPen(color(style.strokeColor), style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(body);
  QFont font = painter.font();
  font.setPixelSize(std::max(1, int(std::lround(shapeFontSize(shape)))));
  painter.setFont(font);
  painter.setPen(fill.lightnessF() > 0.5 ? QColor(Qt::black) : QColor(Qt::white));
  const double pad = std::min(8.0, std::min(box.width(), box.height()) / 4);
  painter.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignCenter | Qt::TextWordWrap, shape.content.text);
}

void paintArrowHead(QPainter &painter, QPointF from, QPointF tip, double width, const QColor &fill) {
  const double dx = tip.x() - from.x(), dy = tip.y() - from.y();
  const double length = std::hypot(dx, dy);
  if (!(length > 0)) return;
  const double size = std::max(width * 3, 12.0);
  const double ux = dx / length, uy = dy / length;
  const QPointF base(tip.x() - ux * size, tip.y() - uy * size);
  const QPointF side(-uy * size / 2, ux * size / 2);
  QPainterPath head;
  head.moveTo(tip); head.lineTo(base + side); head.lineTo(base - side); head.closeSubpath();
  painter.fillPath(head, fill);
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
  painter.setWorldTransform(shapeRotation(shape), true);
  painter.setOpacity(painter.opacity() * style.opacity);
  const QPen stroke(color(style.strokeColor), style.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  if (type == shape::rectangle) {
    painter.setBrush(color(style.fillColor));
    painter.setPen(QPen(color(style.strokeColor), style.strokeWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.drawRect(box);
  } else if (type == shape::ellipse) {
    painter.setBrush(color(style.fillColor));
    painter.setPen(stroke);
    painter.drawEllipse(box);
  } else if (isPathShape(type)) {
    QPainterPath path;
    for (std::size_t i = 0; i < shape.content.points.size(); ++i) {
      const QPointF p = mapped(shape, shape.content.points[i]);
      if (i == 0) path.moveTo(p); else path.lineTo(p);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(stroke);
    painter.drawPath(path);
    const auto &points = shape.content.points;
    if (type == shape::arrow && points.size() >= 2)
      paintArrowHead(painter, mapped(shape, points[points.size() - 2]), mapped(shape, points.back()), style.strokeWidth,
                     color(style.strokeColor));
  } else if (type == shape::text) {
    if (qAlpha(style.fillColor) > 0) painter.fillRect(box, color(style.fillColor));
    QFont font = painter.font();
    font.setPixelSize(std::max(1, int(std::lround(shapeFontSize(shape)))));
    painter.setFont(font);
    painter.setPen(color(style.strokeColor));
    const double pad = std::min(4.0, std::min(box.width(), box.height()) / 4);
    painter.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, shape.content.text);
  } else if (type == shape::step) {
    const QColor fill = qAlpha(style.fillColor) > 0 ? color(style.fillColor) : color(style.strokeColor);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(box);
    QFont font = painter.font();
    font.setPixelSize(std::max(1, int(std::lround(shapeFontSize(shape)))));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(fill.lightnessF() > 0.6 ? Qt::black : Qt::white);
    painter.drawText(box, Qt::AlignCenter, QString::number(shape.content.stepValue));
  } else if (type == shape::highlight) {
    const QColor tint = qAlpha(style.fillColor) > 0 ? color(style.fillColor) : QColor(255, 235, 59);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.fillRect(box, tint);
  } else if (type == shape::blur || type == shape::pixelate) {
    paintRegion(painter, shape, source);
  } else if (type == shape::speechBalloon) {
    paintBalloon(painter, shape);
  } else if (type == shape::magnify) {
    // EA-011: the centre of the bounds enlarged by `strength` from the source.
    const double zoom = std::max(1.0, shapeStrength(shape));
    const QSizeF size(box.width() / zoom, box.height() / zoom);
    const QRectF from(box.center() - QPointF(size.width() / 2, size.height() / 2), size);
    painter.save();
    painter.setClipRect(box, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(box, source, from);
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color(style.strokeColor), style.strokeWidth));
    painter.drawRect(box);
  } else if (type == shape::spotlight) {
    // Dims the whole canvas except the ellipse; strength is the dim percent.
    QPainterPath outside;
    outside.addRect(QRectF(source.rect()).united(box));
    QPainterPath hole;
    hole.addEllipse(box);
    painter.fillPath(outside.subtracted(hole), QColor(0, 0, 0, std::clamp(int(std::lround(shapeStrength(shape) * 2.55)), 0, 255)));
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
      kept.push_back(first);
      for (const PointF &p : gesture) if (std::hypot(p.x - kept.back().x, p.y - kept.back().y) >= 1) kept.push_back(p);
      if (kept.back().x != last.x || kept.back().y != last.y) kept.push_back(last);
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

qint64 nextStepValue(const AnnotationDocument &document) {
  qint64 highest = 0;
  for (const auto &annotation : document.annotations)
    if (const auto *shape = std::get_if<RectangleAnnotation>(&annotation); shape && shape->type() == shape::step)
      highest = std::max(highest, shape->content.stepValue);
  return highest + 1;
}

}  // namespace xerahs::editor
