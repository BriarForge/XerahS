#include "image-editor/ImageComparison.h"
#include "ComparisonImages.h"
#include <QColorSpace>
#include <QTest>
#include <cmath>
#include <limits>

using namespace xerahs::editor;
using namespace xerahs::app;
class ImageComparisonTest : public QObject {
  Q_OBJECT
private slots:
  void topLeftComparisonHasIndependentExpectedPixels() {
    const ArgbImage first{3, 2, {1, 2, 3, 4, 5, 6}}, second{2, 3, {7, 8, 9, 10, 11, 12}};
    const auto result = composeComparison(first, second, ComparisonAlignment::TopLeft, 500);
    QVERIFY(result.image && result.layout);
    QCOMPARE(result.image->width, 3);
    QCOMPARE(result.image->height, 3);
    QVERIFY(result.image->pixels == (std::vector<Argb>{1, 8, 0, 4, 10, 0, 0, 12, 0}));
    QVERIFY(first.pixels == (std::vector<Argb>{1, 2, 3, 4, 5, 6}));
    QVERIFY(second.pixels == (std::vector<Argb>{7, 8, 9, 10, 11, 12}));
  }
  void centeredAlignmentUsesNativeSizesAndOddPadding() {
    const ArgbImage first{5, 2, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
        second{2, 5, {21, 22, 23, 24, 25, 26, 27, 28, 29, 30}};
    const auto result = composeComparison(first, second, ComparisonAlignment::Center, 200);
    QVERIFY(result.image && result.layout);
    QCOMPARE(result.layout->firstOffset, (PointF{0, 1}));
    QCOMPARE(result.layout->secondOffset, (PointF{1, 0}));
    QVERIFY(result.image->pixels ==
            (std::vector<Argb>{0, 21, 22, 0, 0, 1, 23, 24, 0, 0, 6, 25, 26, 0, 0, 0, 27, 28, 0, 0, 0, 29, 30, 0, 0}));
  }
  void dividerEndpointsPreserveAlphaAndShowExactlyOneSource() {
    const ArgbImage first{2, 1, {0x80445566, 0x00ABCDEF}}, second{1, 1, {0x40FEDCBA}};
    auto result = composeComparison(first, second, ComparisonAlignment::TopLeft, 0);
    QVERIFY(result.image);
    QVERIFY(result.image->pixels == (std::vector<Argb>{0x40FEDCBA, 0}));
    result = composeComparison(first, second, ComparisonAlignment::TopLeft, 1000);
    QVERIFY(result.image);
    QVERIFY(result.image->pixels == first.pixels);
  }
  void invalidInputsAndUnionLimitsFailBeforeAllocation() {
    QCOMPARE(comparisonLayout({0, 1}, {1, 1}, ComparisonAlignment::TopLeft).error,
             std::optional<QString>(diagnostic::documentInvalid));
    QCOMPARE(comparisonLayout({100001, 1}, {1, 1}, ComparisonAlignment::TopLeft).error,
             std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(comparisonLayout({std::numeric_limits<qint64>::max(), 1}, {1, 1}, ComparisonAlignment::TopLeft).error,
             std::optional<QString>(diagnostic::documentTooLarge));
    // Both thin inputs individually fit the limits; their union does not.
    QCOMPARE(comparisonLayout({100000, 1}, {1, 100000}, ComparisonAlignment::TopLeft).error,
             std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(comparisonLayout({1, 1}, {1, 1}, ComparisonAlignment(-1)).error,
             std::optional<QString>(diagnostic::documentInvalid));
    QVERIFY(!composeComparison({1, 1, {}}, {1, 1, {1}}, ComparisonAlignment::TopLeft, 500).image);
    QVERIFY(!composeComparison({1, 1, {1}}, {1, 1, {2}}, ComparisonAlignment::TopLeft, 1001).image);
  }
  void cancellationReturnsNoPartialLayoutOrPixels() {
    int percent = 0;
    const auto result = composeComparison(solidImage(10, 10, 1), solidImage(10, 10, 2), ComparisonAlignment::Center,
                                          500, {[&] { return percent >= 50; }, [&](int p) { percent = p; }});
    QCOMPARE(result.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
    QVERIFY(!result.image && !result.layout);
  }
  void displayCopiesConvertToSrgbWithoutChangingSourceOrAnnotations() {
    QImage source(4, 4, QImage::Format_ARGB32);
    source.fill(qRgba(128, 0, 0, 255));
    source.setColorSpace(QColorSpace::SRgbLinear);
    const QImage original = source;
    auto prepared = prepareComparisonImage(source);
    QVERIFY(prepared.diagnostic.isEmpty());
    QCOMPARE(prepared.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    // The sRGB transfer function maps linear 128/255 to approximately 188/255.
    QVERIFY(std::abs(qRed(prepared.image.pixel(0, 0)) - 188) <= 1);
    QCOMPARE(qAlpha(prepared.image.pixel(0, 0)), 255);
    QCOMPARE(source, original);
    QCOMPARE(source.colorSpace(), original.colorSpace());
    RectangleAnnotation r;
    r.id = QUuid::createUuid();
    r.start = {1, 1};
    r.end = {3, 3};
    r.style.strokeColor = 0;
    r.style.fillColor = 0xFF00FF00;
    const std::vector<Annotation> annotations{r};
    prepared = prepareComparisonImage(source, annotations);
    QVERIFY(prepared.diagnostic.isEmpty());
    QCOMPARE(prepared.image.pixel(1, 1), QRgb(0xFF00FF00));
    QCOMPARE(source, original);
    QCOMPARE(std::get<RectangleAnnotation>(annotations[0]).start, r.start);
  }
  void unsupportedVisibleObjectsAndCancelledRenderingPreserveInputs() {
    QImage source(40, 40, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    const QImage original = source;
    const QUuid id = QUuid::createUuid();
    UnsupportedAnnotation unknown{id, {{"type", "future"}}};
    auto prepared = prepareComparisonImage(source, {unknown});
    QCOMPARE(prepared.diagnostic, QStringLiteral("comparison-annotation-unsupported"));
    QVERIFY(prepared.image.isNull());
    unknown.raw.insert(QStringLiteral("visible"), false);
    prepared = prepareComparisonImage(source, {unknown});
    QVERIFY(prepared.diagnostic.isEmpty());
    RectangleAnnotation r;
    r.id = QUuid::createUuid();
    r.start = {2, 2};
    r.end = {38, 38};
    r.style.fillColor = 0xFFFFFFFF;
    int percent = 0;
    prepared = prepareComparisonImage(source, {r}, {[&] { return percent >= 50; }, [&](int p) { percent = p; }});
    QCOMPARE(prepared.diagnostic, QStringLiteral("canvas-cancelled"));
    QVERIFY(prepared.image.isNull());
    QCOMPARE(source, original);
  }
};
QTEST_GUILESS_MAIN(ImageComparisonTest)
#include "ImageComparisonTest.moc"
