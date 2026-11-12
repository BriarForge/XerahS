// CORE-028: painter for the original application mark.
// Geometry is the 256 viewBox shared with packaging/icons/com.xerahs.app.svg.
// Change both together. Nothing here reads that file.
#include "ApplicationIcon.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace xerahs::app {
namespace {

constexpr int kDesign = 256;

// Paper reads on a dark panel. Ink reads on a light panel.
// The amber X sits on the ink so it stays distinct from both.
const QColor kPaper(243, 238, 228);  // #F3EEE4
const QColor kInk(36, 48, 68);       // #243044
const QColor kAmber(227, 155, 18);   // #E39B12

void paintMark(QPainter &painter) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(kPaper);
  painter.drawRoundedRect(QRectF(16, 16, 224, 224), 52, 52);
  painter.setBrush(kInk);
  painter.drawRoundedRect(QRectF(32, 32, 192, 192), 40, 40);

  QPainterPath frame;
  frame.moveTo(100, 48);
  frame.lineTo(48, 48);
  frame.lineTo(48, 100);
  frame.moveTo(156, 48);
  frame.lineTo(208, 48);
  frame.lineTo(208, 100);
  frame.moveTo(100, 208);
  frame.lineTo(48, 208);
  frame.lineTo(48, 156);
  frame.moveTo(156, 208);
  frame.lineTo(208, 208);
  frame.lineTo(208, 156);

  QPen framePen(kPaper, 28, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
  framePen.setMiterLimit(2);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(framePen);
  painter.drawPath(frame);

  QPainterPath mark;
  mark.moveTo(92, 92);
  mark.lineTo(164, 164);
  mark.moveTo(164, 92);
  mark.lineTo(92, 164);
  QPen markPen(kAmber, 30, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(markPen);
  painter.drawPath(mark);
}

QImage renderMark(int size) {
  QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(qreal(size) / qreal(kDesign), qreal(size) / qreal(kDesign));
  paintMark(painter);
  painter.end();
  return image;
}

}  // namespace

QIcon applicationIcon() {
  QIcon icon;
  for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
    QPixmap pixmap = QPixmap::fromImage(renderMark(size));
    pixmap.setDevicePixelRatio(1);
    icon.addPixmap(pixmap);
  }
  return icon;
}

}  // namespace xerahs::app
