#include "AppIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace xerahs::app {
namespace {

void drawIconGeometry(QPainter &p, AppIcon icon) {
  switch (icon) {
    case AppIcon::Camera:
      p.drawRoundedRect(QRectF(2, 7, 20, 13), 2.5, 2.5);
      {
        QPainterPath top;
        top.moveTo(7, 7); top.lineTo(9, 4); top.lineTo(15, 4); top.lineTo(17, 7);
        p.drawPath(top);
      }
      p.drawEllipse(QPointF(12, 13.5), 3.5, 3.5);
      break;

    case AppIcon::Film:
      p.drawRoundedRect(QRectF(2.5, 3.5, 19, 17), 2, 2);
      p.drawLine(QPointF(7, 3.5), QPointF(7, 20.5));
      p.drawLine(QPointF(17, 3.5), QPointF(17, 20.5));
      p.drawLine(QPointF(2.5, 12), QPointF(21.5, 12));
      p.drawLine(QPointF(2.5, 8), QPointF(7, 8));
      p.drawLine(QPointF(2.5, 16), QPointF(7, 16));
      p.drawLine(QPointF(17, 8), QPointF(21.5, 8));
      p.drawLine(QPointF(17, 16), QPointF(21.5, 16));
      break;

    case AppIcon::Pencil: {
      QPainterPath path;
      path.moveTo(17, 3);
      path.lineTo(21, 7);
      path.lineTo(8, 20);
      path.lineTo(3, 21);
      path.lineTo(4, 16);
      path.closeSubpath();
      p.drawPath(path);
      p.drawLine(QPointF(14.5, 5.5), QPointF(18.5, 9.5));
      break;
    }

    case AppIcon::HardDrive:
      p.drawRoundedRect(QRectF(2.5, 13, 19, 7.5), 2, 2);
      {
        QPainterPath top;
        top.moveTo(3, 13.5); top.lineTo(6.2, 5.2); top.lineTo(17.8, 5.2); top.lineTo(21, 13.5);
        p.drawPath(top);
      }
      p.drawPoint(QPointF(6.5, 16.8));
      p.drawPoint(QPointF(10, 16.8));
      break;

    case AppIcon::ChartColumn:
      p.drawLine(QPointF(3, 3), QPointF(3, 20));
      p.drawLine(QPointF(3, 20), QPointF(21, 20));
      p.drawLine(QPointF(8, 16), QPointF(8, 11));
      p.drawLine(QPointF(13, 16), QPointF(13, 6));
      p.drawLine(QPointF(18, 16), QPointF(18, 9));
      break;

    case AppIcon::Combine:
      p.drawRoundedRect(QRectF(3, 3, 7, 7), 1.5, 1.5);
      p.drawRoundedRect(QRectF(14, 14, 7, 7), 1.5, 1.5);
      p.drawRoundedRect(QRectF(3, 14, 7, 7), 1.5, 1.5);
      p.drawLine(QPointF(10, 6.5), QPointF(17.5, 6.5));
      p.drawLine(QPointF(17.5, 6.5), QPointF(17.5, 14));
      break;

    case AppIcon::CloudUpload: {
      QPainterPath cloud;
      cloud.moveTo(6.5, 18);
      cloud.cubicTo(3.5, 18, 2, 15.8, 2.5, 13.2);
      cloud.cubicTo(3, 11, 4.8, 9.8, 6.8, 9.8);
      cloud.cubicTo(7.5, 6.2, 10.5, 4.5, 13.8, 5.2);
      cloud.cubicTo(16.6, 5.8, 18.2, 8.2, 18.2, 10.5);
      cloud.cubicTo(20.5, 10.8, 22, 12.6, 21.5, 15);
      cloud.cubicTo(21.1, 16.8, 19.6, 18, 17.5, 18);
      p.drawPath(cloud);
      p.drawLine(QPointF(12, 12), QPointF(12, 21));
      p.drawLine(QPointF(9, 15), QPointF(12, 12));
      p.drawLine(QPointF(15, 15), QPointF(12, 12));
      break;
    }

    case AppIcon::WandSparkles:
      p.drawLine(QPointF(3.5, 20.5), QPointF(17, 7));
      p.drawLine(QPointF(14, 4), QPointF(20, 10));
      p.drawLine(QPointF(6, 4), QPointF(6, 8));
      p.drawLine(QPointF(4, 6), QPointF(8, 6));
      p.drawLine(QPointF(18, 15), QPointF(18, 19));
      p.drawLine(QPointF(16, 17), QPointF(20, 17));
      break;

    case AppIcon::SlidersVertical:
      p.drawLine(QPointF(5, 21), QPointF(5, 14));
      p.drawLine(QPointF(5, 10), QPointF(5, 3));
      p.drawLine(QPointF(3, 14), QPointF(7, 14));
      p.drawLine(QPointF(12, 21), QPointF(12, 12));
      p.drawLine(QPointF(12, 8), QPointF(12, 3));
      p.drawLine(QPointF(10, 8), QPointF(14, 8));
      p.drawLine(QPointF(19, 21), QPointF(19, 16));
      p.drawLine(QPointF(19, 12), QPointF(19, 3));
      p.drawLine(QPointF(17, 16), QPointF(21, 16));
      break;

    case AppIcon::Terminal: {
      QPainterPath chev;
      chev.moveTo(4, 17); chev.lineTo(10, 11); chev.lineTo(4, 5);
      p.drawPath(chev);
      p.drawLine(QPointF(12, 19), QPointF(20, 19));
      break;
    }

    case AppIcon::Aperture:
      p.drawEllipse(QPointF(12, 12), 9.5, 9.5);
      p.drawLine(QPointF(14.3, 8), QPointF(20, 17.8));
      p.drawLine(QPointF(9.7, 8), QPointF(21, 8));
      p.drawLine(QPointF(7.4, 12), QPointF(13.1, 2.5));
      p.drawLine(QPointF(9.7, 16), QPointF(4, 6.2));
      p.drawLine(QPointF(14.3, 16), QPointF(3, 16));
      p.drawLine(QPointF(16.6, 12), QPointF(10.9, 21.5));
      break;

    case AppIcon::Search:
      p.drawEllipse(QPointF(10.5, 10.5), 6.8, 6.8);
      p.drawLine(QPointF(15.6, 15.6), QPointF(20.5, 20.5));
      break;

    case AppIcon::ChevronDown: {
      QPainterPath c;
      c.moveTo(6, 9); c.lineTo(12, 15); c.lineTo(18, 9);
      p.drawPath(c);
      break;
    }

    case AppIcon::ChevronRight: {
      QPainterPath c;
      c.moveTo(9, 6); c.lineTo(15, 12); c.lineTo(9, 18);
      p.drawPath(c);
      break;
    }

    case AppIcon::Play: {
      QPainterPath tri;
      tri.moveTo(6, 4.5); tri.lineTo(19.5, 12); tri.lineTo(6, 19.5); tri.closeSubpath();
      p.drawPath(tri);
      break;
    }

    case AppIcon::Square:
      p.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
      break;

    case AppIcon::Pause:
      p.drawRoundedRect(QRectF(5, 4, 4.5, 16), 1, 1);
      p.drawRoundedRect(QRectF(14.5, 4, 4.5, 16), 1, 1);
      break;

    case AppIcon::RotateCcw: {
      QPainterPath arc;
      arc.arcMoveTo(QRectF(4, 4, 16, 16), 150);
      arc.arcTo(QRectF(4, 4, 16, 16), 150, -300);
      p.drawPath(arc);
      p.drawLine(QPointF(4, 4), QPointF(4, 9));
      p.drawLine(QPointF(4, 9), QPointF(9, 9));
      break;
    }

    case AppIcon::RotateCw: {
      QPainterPath arc;
      arc.arcMoveTo(QRectF(4, 4, 16, 16), 30);
      arc.arcTo(QRectF(4, 4, 16, 16), 30, 300);
      p.drawPath(arc);
      p.drawLine(QPointF(20, 4), QPointF(20, 9));
      p.drawLine(QPointF(20, 9), QPointF(15, 9));
      break;
    }

    case AppIcon::CloseX:
      p.drawLine(QPointF(5.5, 5.5), QPointF(18.5, 18.5));
      p.drawLine(QPointF(18.5, 5.5), QPointF(5.5, 18.5));
      break;

    case AppIcon::TriangleAlert: {
      QPainterPath tri;
      tri.moveTo(12, 3.2); tri.lineTo(21.2, 19.8); tri.lineTo(2.8, 19.8); tri.closeSubpath();
      p.drawPath(tri);
      p.drawLine(QPointF(12, 9.5), QPointF(12, 14));
      p.drawPoint(QPointF(12, 17));
      break;
    }

    case AppIcon::Link:
      p.drawRoundedRect(QRectF(3, 8.5, 9, 7), 3.5, 3.5);
      p.drawRoundedRect(QRectF(12, 8.5, 9, 7), 3.5, 3.5);
      p.drawLine(QPointF(8.5, 12), QPointF(15.5, 12));
      break;

    case AppIcon::Users:
      p.drawEllipse(QPointF(9, 7.5), 3.5, 3.5);
      {
        QPainterPath a;
        a.moveTo(3, 20); a.cubicTo(3, 15.5, 6, 14, 9, 14); a.cubicTo(12, 14, 15, 15.5, 15, 20);
        p.drawPath(a);
      }
      p.drawEllipse(QPointF(16.5, 8.5), 2.5, 2.5);
      {
        QPainterPath b;
        b.moveTo(16.5, 14.2); b.cubicTo(19, 14.5, 21, 16, 21, 20);
        p.drawPath(b);
      }
      break;

    case AppIcon::ExternalLink:
      p.drawLine(QPointF(14, 4), QPointF(20, 4));
      p.drawLine(QPointF(20, 4), QPointF(20, 10));
      p.drawLine(QPointF(11, 13), QPointF(20, 4));
      {
        QPainterPath box;
        box.moveTo(18, 14); box.lineTo(18, 19.5); box.lineTo(4.5, 19.5); box.lineTo(4.5, 6); box.lineTo(10, 6);
        p.drawPath(box);
      }
      break;

    case AppIcon::Plus:
      p.drawLine(QPointF(12, 5), QPointF(12, 19));
      p.drawLine(QPointF(5, 12), QPointF(19, 12));
      break;

    case AppIcon::RefreshCw: {
      QPainterPath a;
      a.arcMoveTo(QRectF(4, 4, 16, 16), 30);
      a.arcTo(QRectF(4, 4, 16, 16), 30, 140);
      p.drawPath(a);
      QPainterPath b;
      b.arcMoveTo(QRectF(4, 4, 16, 16), 210);
      b.arcTo(QRectF(4, 4, 16, 16), 210, 140);
      p.drawPath(b);
      p.drawLine(QPointF(19.5, 4), QPointF(19.5, 8.5));
      p.drawLine(QPointF(15.5, 8.5), QPointF(19.5, 8.5));
      p.drawLine(QPointF(4.5, 20), QPointF(4.5, 15.5));
      p.drawLine(QPointF(4.5, 15.5), QPointF(8.5, 15.5));
      break;
    }

    case AppIcon::Share2:
      p.drawEllipse(QPointF(18, 5.5), 2.5, 2.5);
      p.drawEllipse(QPointF(6, 12), 2.5, 2.5);
      p.drawEllipse(QPointF(18, 18.5), 2.5, 2.5);
      p.drawLine(QPointF(8.3, 10.8), QPointF(15.7, 6.7));
      p.drawLine(QPointF(8.3, 13.2), QPointF(15.7, 17.3));
      break;

    case AppIcon::List:
      p.drawLine(QPointF(8, 6), QPointF(21, 6));
      p.drawLine(QPointF(8, 12), QPointF(21, 12));
      p.drawLine(QPointF(8, 18), QPointF(21, 18));
      p.drawPoint(QPointF(4, 6));
      p.drawPoint(QPointF(4, 12));
      p.drawPoint(QPointF(4, 18));
      break;

    case AppIcon::LayoutGrid:
      p.drawRoundedRect(QRectF(3.5, 3.5, 7, 7), 1.2, 1.2);
      p.drawRoundedRect(QRectF(13.5, 3.5, 7, 7), 1.2, 1.2);
      p.drawRoundedRect(QRectF(3.5, 13.5, 7, 7), 1.2, 1.2);
      p.drawRoundedRect(QRectF(13.5, 13.5, 7, 7), 1.2, 1.2);
      break;

    case AppIcon::FolderOpen: {
      QPainterPath f;
      f.moveTo(3, 6); f.lineTo(9.5, 6); f.lineTo(11.5, 8.5); f.lineTo(20.5, 8.5);
      f.lineTo(20.5, 11); f.lineTo(5.5, 11); f.lineTo(3, 19); f.closeSubpath();
      p.drawPath(f);
      p.drawLine(QPointF(3, 19), QPointF(19, 19));
      p.drawLine(QPointF(19, 19), QPointF(21.5, 11));
      break;
    }

    case AppIcon::FilePlus: {
      QPainterPath doc;
      doc.moveTo(6, 3); doc.lineTo(14.5, 3); doc.lineTo(19, 7.5); doc.lineTo(19, 21); doc.lineTo(6, 21); doc.closeSubpath();
      p.drawPath(doc);
      p.drawLine(QPointF(14.5, 3), QPointF(14.5, 7.5));
      p.drawLine(QPointF(14.5, 7.5), QPointF(19, 7.5));
      p.drawLine(QPointF(12.5, 11.5), QPointF(12.5, 17.5));
      p.drawLine(QPointF(9.5, 14.5), QPointF(15.5, 14.5));
      break;
    }

    case AppIcon::Command:
      p.drawRoundedRect(QRectF(9, 9, 6, 6), 0.5, 0.5);
      p.drawEllipse(QRectF(3.5, 3.5, 5.5, 5.5));
      p.drawEllipse(QRectF(15, 3.5, 5.5, 5.5));
      p.drawEllipse(QRectF(3.5, 15, 5.5, 5.5));
      p.drawEllipse(QRectF(15, 15, 5.5, 5.5));
      break;

    case AppIcon::Palette:
      p.drawEllipse(QPointF(12, 12), 9, 9);
      p.drawPoint(QPointF(8.5, 9));
      p.drawPoint(QPointF(13.5, 7.5));
      p.drawPoint(QPointF(16.5, 11));
      p.drawPoint(QPointF(9, 15));
      break;

    case AppIcon::QrCode:
      p.drawRect(QRectF(3.5, 3.5, 6, 6));
      p.drawRect(QRectF(14.5, 3.5, 6, 6));
      p.drawRect(QRectF(3.5, 14.5, 6, 6));
      p.drawLine(QPointF(14.5, 14.5), QPointF(14.5, 20.5));
      p.drawLine(QPointF(14.5, 17.5), QPointF(20.5, 17.5));
      p.drawLine(QPointF(20.5, 14.5), QPointF(20.5, 20.5));
      break;

    case AppIcon::ScanLine:
      p.drawLine(QPointF(4, 8), QPointF(4, 4)); p.drawLine(QPointF(4, 4), QPointF(8, 4));
      p.drawLine(QPointF(16, 4), QPointF(20, 4)); p.drawLine(QPointF(20, 4), QPointF(20, 8));
      p.drawLine(QPointF(4, 16), QPointF(4, 20)); p.drawLine(QPointF(4, 20), QPointF(8, 20));
      p.drawLine(QPointF(16, 20), QPointF(20, 20)); p.drawLine(QPointF(20, 20), QPointF(20, 16));
      p.drawLine(QPointF(5, 12), QPointF(19, 12));
      break;

    case AppIcon::FileText: {
      QPainterPath doc;
      doc.moveTo(6, 3); doc.lineTo(14.5, 3); doc.lineTo(19, 7.5); doc.lineTo(19, 21); doc.lineTo(6, 21); doc.closeSubpath();
      p.drawPath(doc);
      p.drawLine(QPointF(9, 12), QPointF(16, 12));
      p.drawLine(QPointF(9, 16), QPointF(16, 16));
      break;
    }

    case AppIcon::Hash:
      p.drawLine(QPointF(4, 9), QPointF(20, 9));
      p.drawLine(QPointF(4, 15), QPointF(20, 15));
      p.drawLine(QPointF(10, 4), QPointF(8, 20));
      p.drawLine(QPointF(16, 4), QPointF(14, 20));
      break;

    case AppIcon::ImageIcon:
      p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
      p.drawEllipse(QPointF(8.5, 9), 1.6, 1.6);
      {
        QPainterPath m;
        m.moveTo(21, 16); m.lineTo(15.5, 10.5); m.lineTo(6.5, 20);
        p.drawPath(m);
      }
      break;

    case AppIcon::Scissors:
      p.drawEllipse(QPointF(6.5, 6.5), 2.8, 2.8);
      p.drawEllipse(QPointF(6.5, 17.5), 2.8, 2.8);
      p.drawLine(QPointF(8.7, 8.5), QPointF(19.5, 19));
      p.drawLine(QPointF(8.7, 15.5), QPointF(19.5, 5));
      break;

    case AppIcon::Sparkles: {
      QPainterPath star;
      star.moveTo(12, 3); star.quadTo(12, 12, 21, 12); star.quadTo(12, 12, 12, 21);
      star.quadTo(12, 12, 3, 12); star.quadTo(12, 12, 12, 3);
      p.drawPath(star);
      break;
    }

    case AppIcon::MousePointer2:
    case AppIcon::MousePointer: {
      QPainterPath ptr;
      ptr.moveTo(5, 3.5); ptr.lineTo(19, 11); ptr.lineTo(12.5, 12.8); ptr.lineTo(10.5, 19.5); ptr.closeSubpath();
      p.drawPath(ptr);
      if (icon == AppIcon::MousePointer) p.drawLine(QPointF(13, 13), QPointF(19, 19));
      break;
    }

    case AppIcon::Circle:
      p.drawEllipse(QPointF(12, 12), 8, 8);
      break;

    case AppIcon::Minus:
      p.drawLine(QPointF(4, 12), QPointF(20, 12));
      break;

    case AppIcon::ArrowRight:
      p.drawLine(QPointF(4, 12), QPointF(19.5, 12));
      p.drawLine(QPointF(13.5, 6), QPointF(19.5, 12));
      p.drawLine(QPointF(13.5, 18), QPointF(19.5, 12));
      break;

    case AppIcon::Highlighter: {
      QPainterPath hl;
      hl.moveTo(9, 11); hl.lineTo(16, 4); hl.lineTo(20, 8); hl.lineTo(13, 15); hl.closeSubpath();
      p.drawPath(hl);
      p.drawLine(QPointF(4, 20), QPointF(12, 20));
      p.drawLine(QPointF(9, 11), QPointF(6.5, 16.5));
      p.drawLine(QPointF(13, 15), QPointF(6.5, 16.5));
      break;
    }

    case AppIcon::Type:
      p.drawLine(QPointF(5, 5), QPointF(19, 5));
      p.drawLine(QPointF(12, 5), QPointF(12, 19.5));
      p.drawLine(QPointF(8.5, 19.5), QPointF(15.5, 19.5));
      break;

    case AppIcon::MessageSquare: {
      QPainterPath msg;
      msg.moveTo(4, 4); msg.lineTo(20, 4); msg.lineTo(20, 16.5); msg.lineTo(9, 16.5); msg.lineTo(4, 21); msg.closeSubpath();
      p.drawPath(msg);
      break;
    }

    case AppIcon::Droplet: {
      QPainterPath drop;
      drop.moveTo(12, 3);
      drop.cubicTo(17, 9.5, 19, 12.5, 19, 15);
      drop.cubicTo(19, 18.8, 15.9, 21, 12, 21);
      drop.cubicTo(8.1, 21, 5, 18.8, 5, 15);
      drop.cubicTo(5, 12.5, 7, 9.5, 12, 3);
      p.drawPath(drop);
      break;
    }

    case AppIcon::Grid2x2:
      p.drawRoundedRect(QRectF(3.5, 3.5, 17, 17), 2, 2);
      p.drawLine(QPointF(12, 3.5), QPointF(12, 20.5));
      p.drawLine(QPointF(3.5, 12), QPointF(20.5, 12));
      break;

    case AppIcon::Lightbulb:
      p.drawArc(QRectF(6, 3, 12, 12), -30 * 16, 240 * 16);
      p.drawLine(QPointF(9, 16.5), QPointF(15, 16.5));
      p.drawLine(QPointF(9.5, 20), QPointF(14.5, 20));
      break;

    case AppIcon::Eraser: {
      QPainterPath e;
      e.moveTo(7, 20); e.lineTo(3.5, 16.5); e.lineTo(13, 7); e.lineTo(19.5, 13.5); e.lineTo(13, 20); e.closeSubpath();
      p.drawPath(e);
      p.drawLine(QPointF(13, 20), QPointF(20.5, 20));
      break;
    }

    case AppIcon::Crop:
      p.drawLine(QPointF(7, 3), QPointF(7, 17));
      p.drawLine(QPointF(7, 17), QPointF(21, 17));
      p.drawLine(QPointF(3, 7), QPointF(17, 7));
      p.drawLine(QPointF(17, 7), QPointF(17, 21));
      break;

    case AppIcon::Smile:
      p.drawEllipse(QPointF(12, 12), 9, 9);
      p.drawPoint(QPointF(9, 9.5));
      p.drawPoint(QPointF(15, 9.5));
      p.drawArc(QRectF(8, 10, 8, 6.5), 200 * 16, 140 * 16);
      break;

    case AppIcon::Undo2: {
      p.drawLine(QPointF(9, 5), QPointF(4, 10));
      p.drawLine(QPointF(4, 10), QPointF(9, 15));
      QPainterPath u;
      u.moveTo(4, 10); u.lineTo(15, 10); u.cubicTo(19.5, 10, 19.5, 18.5, 15, 18.5); u.lineTo(10, 18.5);
      p.drawPath(u);
      break;
    }

    case AppIcon::Redo2: {
      p.drawLine(QPointF(15, 5), QPointF(20, 10));
      p.drawLine(QPointF(20, 10), QPointF(15, 15));
      QPainterPath r;
      r.moveTo(20, 10); r.lineTo(9, 10); r.cubicTo(4.5, 10, 4.5, 18.5, 9, 18.5); r.lineTo(14, 18.5);
      p.drawPath(r);
      break;
    }

    case AppIcon::Trash2:
      p.drawLine(QPointF(3.5, 6), QPointF(20.5, 6));
      p.drawLine(QPointF(8.5, 6), QPointF(9.5, 3.5));
      p.drawLine(QPointF(9.5, 3.5), QPointF(14.5, 3.5));
      p.drawLine(QPointF(14.5, 3.5), QPointF(15.5, 6));
      p.drawRoundedRect(QRectF(5.5, 6, 13, 14.5), 1.5, 1.5);
      p.drawLine(QPointF(10, 10), QPointF(10, 16.5));
      p.drawLine(QPointF(14, 10), QPointF(14, 16.5));
      break;

    case AppIcon::BrushCleaning:
      p.drawLine(QPointF(12, 3), QPointF(12, 11));
      p.drawRoundedRect(QRectF(5, 11, 14, 9.5), 2, 2);
      p.drawLine(QPointF(9.5, 15.5), QPointF(9.5, 20.5));
      p.drawLine(QPointF(14.5, 15.5), QPointF(14.5, 20.5));
      break;

    case AppIcon::Clipboard:
      p.drawRoundedRect(QRectF(8, 3, 8, 3.5), 1, 1);
      p.drawRoundedRect(QRectF(5, 5, 14, 16), 2, 2);
      break;

    case AppIcon::Save:
    case AppIcon::SaveAll: {
      QPainterPath s;
      s.moveTo(4, 4); s.lineTo(16.5, 4); s.lineTo(20, 7.5); s.lineTo(20, 20); s.lineTo(4, 20); s.closeSubpath();
      p.drawPath(s);
      p.drawRect(QRectF(8, 4, 7, 5));
      p.drawRect(QRectF(7.5, 13, 9, 7));
      if (icon == AppIcon::SaveAll) p.drawLine(QPointF(10, 16.5), QPointF(14, 16.5));
      break;
    }

    case AppIcon::Pin:
      p.drawLine(QPointF(12, 15), QPointF(12, 21));
      p.drawLine(QPointF(6, 15), QPointF(18, 15));
      p.drawLine(QPointF(8.5, 15), QPointF(9.5, 7));
      p.drawLine(QPointF(15.5, 15), QPointF(14.5, 7));
      p.drawLine(QPointF(7.5, 4.5), QPointF(16.5, 4.5));
      break;

    case AppIcon::Printer:
      p.drawRect(QRectF(6.5, 3.5, 11, 5));
      p.drawRoundedRect(QRectF(3.5, 8.5, 17, 8), 2, 2);
      p.drawRect(QRectF(6.5, 14, 11, 6.5));
      break;

    case AppIcon::Wallpaper:
      p.drawRoundedRect(QRectF(3, 4, 18, 13), 2, 2);
      p.drawLine(QPointF(8, 20.5), QPointF(16, 20.5));
      p.drawLine(QPointF(12, 17), QPointF(12, 20.5));
      p.drawEllipse(QPointF(8.5, 8.5), 1.5, 1.5);
      break;

    case AppIcon::Layers2: {
      QPainterPath top;
      top.moveTo(12, 3.5); top.lineTo(20.5, 8.5); top.lineTo(12, 13.5); top.lineTo(3.5, 8.5); top.closeSubpath();
      p.drawPath(top);
      QPainterPath bot;
      bot.moveTo(3.5, 14.5); bot.lineTo(12, 19.5); bot.lineTo(20.5, 14.5);
      p.drawPath(bot);
      break;
    }

    case AppIcon::ZoomIn:
      p.drawEllipse(QPointF(10.5, 10.5), 6.8, 6.8);
      p.drawLine(QPointF(15.6, 15.6), QPointF(20.5, 20.5));
      p.drawLine(QPointF(10.5, 7.5), QPointF(10.5, 13.5));
      p.drawLine(QPointF(7.5, 10.5), QPointF(13.5, 10.5));
      break;

    case AppIcon::ZoomOut:
      p.drawEllipse(QPointF(10.5, 10.5), 6.8, 6.8);
      p.drawLine(QPointF(15.6, 15.6), QPointF(20.5, 20.5));
      p.drawLine(QPointF(7.5, 10.5), QPointF(13.5, 10.5));
      break;

    case AppIcon::Maximize:
      p.drawLine(QPointF(4, 9), QPointF(4, 4)); p.drawLine(QPointF(4, 4), QPointF(9, 4));
      p.drawLine(QPointF(15, 4), QPointF(20, 4)); p.drawLine(QPointF(20, 4), QPointF(20, 9));
      p.drawLine(QPointF(4, 15), QPointF(4, 20)); p.drawLine(QPointF(4, 20), QPointF(9, 20));
      p.drawLine(QPointF(15, 20), QPointF(20, 20)); p.drawLine(QPointF(20, 20), QPointF(20, 15));
      break;

    case AppIcon::Settings:
      p.drawEllipse(QPointF(12, 12), 3.2, 3.2);
      p.drawEllipse(QPointF(12, 12), 7.2, 7.2);
      break;

    case AppIcon::LogOut:
      p.drawLine(QPointF(9, 20), QPointF(4.5, 20));
      p.drawLine(QPointF(4.5, 20), QPointF(4.5, 4));
      p.drawLine(QPointF(4.5, 4), QPointF(9, 4));
      p.drawLine(QPointF(15, 16.5), QPointF(20, 12));
      p.drawLine(QPointF(20, 12), QPointF(15, 7.5));
      p.drawLine(QPointF(20, 12), QPointF(9, 12));
      break;

    case AppIcon::Check: {
      QPainterPath c;
      c.moveTo(4.5, 12.5); c.lineTo(9.5, 17.5); c.lineTo(19.5, 6.5);
      p.drawPath(c);
      break;
    }
  }
}

QPixmap renderAppIcon(AppIcon icon, const QColor &color, int size, qreal dpr) {
  const int px = qMax(1, qRound(size * dpr));
  QPixmap pm(px, px);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.scale(size / 24.0, size / 24.0);
  QPen pen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  drawIconGeometry(p, icon);
  p.end();
  return pm;
}

}  // namespace

QIcon appIcon(AppIcon icon, const QColor &color, int size) {
  QIcon out;
  out.addPixmap(renderAppIcon(icon, color, size, 1.0), QIcon::Normal, QIcon::Off);
  out.addPixmap(renderAppIcon(icon, color, size, 2.0), QIcon::Normal, QIcon::Off);
  QColor disabled = color;
  disabled.setAlpha(105);
  out.addPixmap(renderAppIcon(icon, disabled, size, 1.0), QIcon::Disabled, QIcon::Off);
  return out;
}

QIcon swatchIcon(const QColor &color, bool hollowBorder, int size) {
  QIcon out;
  for (qreal dpr : {1.0, 2.0}) {
    const int px = qMax(1, qRound(size * dpr));
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(2, 2, size - 4, size - 4);
    if (hollowBorder) {
      p.setPen(QPen(color, 3.0));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 3, 3);
    } else {
      // Checkerboard under translucent colors.
      p.save();
      QPainterPath clip;
      clip.addRoundedRect(r, 3, 3);
      p.setClipPath(clip);
      p.fillRect(r, QColor(0xD0, 0xD0, 0xD0));
      p.fillRect(QRectF(r.left(), r.top(), r.width() / 2, r.height() / 2), QColor(0x9A, 0x9A, 0x9A));
      p.fillRect(QRectF(r.center().x(), r.center().y(), r.width() / 2, r.height() / 2), QColor(0x9A, 0x9A, 0x9A));
      p.fillRect(r, color);
      p.restore();
      p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r, 3, 3);
    }
    p.end();
    out.addPixmap(pm);
  }
  return out;
}

}  // namespace xerahs::app
