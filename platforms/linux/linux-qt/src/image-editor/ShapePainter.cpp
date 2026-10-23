#include "image-editor/ShapePainter.h"

#include <QFont>
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
  return shape.type() == shape::blur ? 8.0 : 12.0;
}

void paintShape(QPainter &painter, const RectangleAnnotation &shape, const QImage &source) {
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
