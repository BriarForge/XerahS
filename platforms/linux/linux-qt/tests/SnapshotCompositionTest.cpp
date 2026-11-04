#include "SnapshotComposition.h"
#include <QColorSpace>
#include <QTest>
using namespace xerahs::app;
using namespace xerahs::capture;
class SnapshotCompositionTest final : public QObject {
  Q_OBJECT
private slots:
  void nativePixelsPreservePlacementGapsColourMetadataAndOwnership() {
    QImage desktop(8, 4, QImage::Format_ARGB32); desktop.setColorSpace(QColorSpace::DisplayP3);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) desktop.setPixel(x, y, qRgb(x, y, x < 4 ? 1 : 2));
    const Topology displays{{"a", {0, 0, 4, 2}, {0, 0, 4, 2}, 1}, {"b", {4, 0, 8, 4}, {4, 0, 6, 2}, 2}};
    const auto before = desktop.copy(); auto result = composeSnapshot(desktop, {2, 0, 6, 3}, displays);
    QVERIFY(result.diagnostic.isEmpty()); QVERIFY(!result.cancelled); QCOMPARE(result.image.size(), QSize(4, 3));
    QCOMPARE(result.image.pixel(0, 0), qRgb(2, 0, 1)); QCOMPARE(result.image.pixel(2, 2), qRgb(4, 2, 2));
    QCOMPARE(result.image.pixel(0, 2), QRgb(0)); QCOMPARE(result.image.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
    QCOMPARE(desktop, before); desktop = QImage();
    QCOMPARE(result.image.pixel(2, 2), qRgb(4, 2, 2));
  }
  void negativeDesktopOriginUsesNormalizedBitmapOffsetsWithoutResampling() {
    QImage desktop(8, 4, QImage::Format_ARGB32);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) desktop.setPixel(x, y, qRgb(x, y, 9));
    const Topology displays{{"a", {-8, -4, -4, 0}, {-8, -4, -4, 0}, 1},
                            {"b", {-4, -4, 0, 0}, {-4, -4, -2, -2}, 2}};
    const auto result = composeSnapshot(desktop, {-6, -3, -2, 0}, displays);
    QVERIFY(result.diagnostic.isEmpty()); QCOMPARE(result.image, desktop.copy(2, 1, 4, 3));
  }
  void semiTransparentSourceChannelsSurviveCompositionExactly() {
    QImage desktop(8, 4, QImage::Format_ARGB32); desktop.setColorSpace(QColorSpace::DisplayP3);
    const int alphas[] = {0, 1, 17, 155, 254, 255, 93, 128};
    for (int y = 0; y < desktop.height(); ++y) for (int x = 0; x < desktop.width(); ++x)
      desktop.setPixel(x, y, qRgba(37 + x, 83 + y, 179 - x - y, alphas[x]));
    const auto before = desktop.copy();
    const Topology displays{{"a", {0, 0, 4, 4}, {0, 0, 4, 4}, 1},
                            {"b", {4, 0, 8, 4}, {4, 0, 6, 2}, 2}};
    auto result = composeSnapshot(desktop, {0, 1, 8, 4}, displays);
    QVERIFY(result.diagnostic.isEmpty()); QVERIFY(!result.cancelled);
    QCOMPARE(result.image.size(), QSize(8, 3));
    QCOMPARE(result.image.colorSpace(), desktop.colorSpace());
    QCOMPARE(desktop, before); desktop = QImage(); // result owns its copied pixels
    for (int y = 0; y < result.image.height(); ++y) for (int x = 0; x < result.image.width(); ++x)
      QCOMPARE(result.image.pixel(x, y), before.pixel(x, y + 1));
    QCOMPARE(result.image, before.copy(0, 1, 8, 3));
  }
  void missingOutOfBoundsAndCancelledSourcesReturnNoPixels() {
    const Topology topology{{"a", {0, 0, 8, 6}, {0, 0, 8, 6}, 1}};
    const auto missing = composeSnapshot({}, {0, 0, 8, 6}, topology);
    QCOMPARE(missing.diagnostic, diagnostic::captureSourceFailed); QVERIFY(missing.image.isNull());
    QImage small(4, 4, QImage::Format_RGB32); small.fill(Qt::red);
    const auto outside = composeSnapshot(small, {0, 0, 8, 6}, topology);
    QCOMPARE(outside.diagnostic, diagnostic::captureSourceFailed); QVERIFY(outside.image.isNull());
    const auto cancelled = composeSnapshot(small, {0, 0, 4, 4}, topology, {[] { return true; }, {}});
    QVERIFY(cancelled.cancelled); QVERIFY(cancelled.diagnostic.isEmpty()); QVERIFY(cancelled.image.isNull());
    bool stop = false;
    const auto partial = composeSnapshot(small, {0, 0, 4, 4}, topology, {[&] { return stop; }, [&](int percent) { if (percent >= 50) stop = true; }});
    QVERIFY(partial.cancelled); QVERIFY(partial.image.isNull()); QVERIFY(partial.diagnostic.isEmpty());
  }
};
QTEST_GUILESS_MAIN(SnapshotCompositionTest)
#include "SnapshotCompositionTest.moc"
