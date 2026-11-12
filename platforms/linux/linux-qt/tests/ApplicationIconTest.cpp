// CORE-028: the application mark is a real multi-size icon, transparent
// outside its plate, and still readable on a light panel and a dark panel.
// Checks are properties of the rendered icon, not a pixel copy of the painter.
#include "ApplicationIcon.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <utility>

using namespace xerahs::app;

namespace {

constexpr int kSizes[] = {16, 24, 32, 48, 64, 128, 256};

int luminance(QRgb pixel) {
  return (qRed(pixel) * 299 + qGreen(pixel) * 587 + qBlue(pixel) * 114) / 1000;
}

QImage iconImage(const QIcon &icon, int size) {
  return icon.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32);
}

QImage composited(const QImage &mark, const QColor &backdrop) {
  QImage canvas(mark.size(), QImage::Format_ARGB32);
  canvas.fill(backdrop);
  QPainter painter(&canvas);
  painter.drawImage(0, 0, mark);
  painter.end();
  return canvas;
}

QRect fraction(const QImage &image, double left, double top, double right, double bottom) {
  const int width = image.width();
  const int height = image.height();
  const int x = std::clamp(int(width * left), 0, width - 1);
  const int y = std::clamp(int(height * top), 0, height - 1);
  const int r = std::clamp(int(width * right) - 1, x, width - 1);
  const int b = std::clamp(int(height * bottom) - 1, y, height - 1);
  return QRect(QPoint(x, y), QPoint(r, b));
}

struct RegionStats {
  int minLuminance = 255;
  int maxLuminance = 0;
  bool warm = false;
};

RegionStats measure(const QImage &image, const QRect &region) {
  RegionStats stats;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      const int tone = luminance(pixel);
      stats.minLuminance = std::min(stats.minLuminance, tone);
      stats.maxLuminance = std::max(stats.maxLuminance, tone);
      if (qRed(pixel) >= 140 && qRed(pixel) >= qGreen(pixel) && qRed(pixel) >= qBlue(pixel) + 30) stats.warm = true;
    }
  }
  return stats;
}

}  // namespace

class ApplicationIconTest : public QObject {
  Q_OBJECT

private slots:
  void everyRequiredSizeIsARealIcon() {
    const QIcon icon = applicationIcon();
    QVERIFY(!icon.isNull());
    const QList<QSize> available = icon.availableSizes();
    for (const int size : kSizes) {
      QVERIFY2(available.contains(QSize(size, size)), qPrintable(QString("missing %1").arg(size)));
      const QPixmap pixmap = icon.pixmap(size, size);
      QVERIFY(!pixmap.isNull());
      QCOMPARE(pixmap.deviceIndependentSize().toSize(), QSize(size, size));
      QCOMPARE(pixmap.size(), QSize(size, size));
      QVERIFY(qFuzzyCompare(pixmap.devicePixelRatio(), 1));
    }
    const QImage first = iconImage(applicationIcon(), 64);
    const QImage second = iconImage(applicationIcon(), 64);
    QCOMPARE(first, second);
  }

  void plateIsTransparentOutsideAndLegibleOnLightAndDark() {
    for (const int size : kSizes) {
      const QImage mark = iconImage(applicationIcon(), size);
      QCOMPARE(mark.size(), QSize(size, size));
      const QPoint corners[] = {{0, 0}, {size - 1, 0}, {0, size - 1}, {size - 1, size - 1}};
      for (const QPoint &corner : corners) {
        QVERIFY2(qAlpha(mark.pixel(corner)) == 0, qPrintable(QString("size %1 corner %2,%3 alpha %4")
            .arg(size).arg(corner.x()).arg(corner.y()).arg(qAlpha(mark.pixel(corner)))));
      }

      int opaque = 0;
      for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
          if (qAlpha(mark.pixel(x, y)) > 200) ++opaque;
        }
      }
      QVERIFY2(opaque > (size * size) / 4, qPrintable(QString("size %1 has too little painted plate").arg(size)));
      QVERIFY2(opaque < (size * size * 19) / 20, qPrintable(QString("size %1 leaves no clear outside").arg(size)));

      const QImage onLight = composited(mark, QColor(248, 248, 246));
      const QImage onDark = composited(mark, QColor(18, 20, 24));
      const RegionStats light = measure(onLight, onLight.rect());
      const RegionStats dark = measure(onDark, onDark.rect());
      QVERIFY2(light.minLuminance < 90, qPrintable(QString("size %1 disappears on a light panel").arg(size)));
      QVERIFY2(dark.maxLuminance > 190, qPrintable(QString("size %1 disappears on a dark panel").arg(size)));
      QVERIFY2(light.maxLuminance - light.minLuminance > 80, qPrintable(QString("size %1 is flat on light").arg(size)));
      QVERIFY2(dark.maxLuminance - dark.minLuminance > 80, qPrintable(QString("size %1 is flat on dark").arg(size)));

      const QRect center = fraction(mark, 0.34, 0.34, 0.66, 0.66);
      QVERIFY2(measure(mark, center).warm, qPrintable(QString("size %1 center has no warm X").arg(size)));
      QVERIFY2(measure(onLight, center).minLuminance < 100, qPrintable(QString("size %1 center has no ink").arg(size)));

      const QRect cornerZones[] = {
          fraction(mark, 0.12, 0.12, 0.34, 0.34),
          fraction(mark, 0.66, 0.12, 0.88, 0.34),
          fraction(mark, 0.12, 0.66, 0.34, 0.88),
          fraction(mark, 0.66, 0.66, 0.88, 0.88),
      };
      for (const QRect &zone : cornerZones) {
        QVERIFY2(measure(onDark, zone).maxLuminance > 170,
            qPrintable(QString("size %1 capture corner is missing").arg(size)));
      }
    }
  }

  void copiesAndRepeatedCallsOwnTheirPixmaps() {
    QPixmap kept;
    QImage fromCopy;
    {
      QIcon original = applicationIcon();
      QIcon copy = original;
      original = QIcon();
      fromCopy = copy.pixmap(48, 48).toImage();
      kept = copy.pixmap(64, 64);
    }
    QVERIFY(!kept.isNull());
    QCOMPARE(kept.size(), QSize(64, 64));
    QVERIFY(qAlpha(kept.toImage().pixel(kept.width() / 2, kept.height() / 2)) > 200);
    QCOMPARE(fromCopy.size(), QSize(48, 48));
    QVERIFY(!fromCopy.isNull());

    QIcon donor = applicationIcon();
    QIcon taken = std::move(donor);
    const QImage moved = taken.pixmap(32, 32).toImage();
    QCOMPARE(moved.size(), QSize(32, 32));
    QVERIFY(qAlpha(moved.pixel(moved.width() / 2, moved.height() / 2)) > 200);

    QImage first;
    for (int i = 0; i < 24; ++i) {
      const QIcon icon = applicationIcon();
      QVERIFY(!icon.isNull());
      const QImage image = icon.pixmap(16, 16).toImage();
      QCOMPARE(image.size(), QSize(16, 16));
      if (i == 0) first = image;
      else QCOMPARE(image, first);
    }
    QCOMPARE(kept.toImage().size(), QSize(64, 64));
  }
};

QTEST_MAIN(ApplicationIconTest)
#include "ApplicationIconTest.moc"
