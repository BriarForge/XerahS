// EDITOR-EFFECTS-001: EE-003, EE-009, EE-010, EE-016, EE-018.
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QTest>
#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kPilots{"alpha", "brightness", "contrast", "grayscale", "invert", "saturation"};
QImage fixture() {
  QImage image(3, 3, QImage::Format_ARGB32);
  const QRgb pixels[]{qRgba(40, 100, 200, 128), qRgba(0, 255, 1, 255), qRgba(15, 20, 25, 0),
                      qRgba(255, 255, 255, 1), qRgba(0, 0, 0, 254), qRgba(127, 128, 129, 64),
                      qRgba(40, 100, 200, 0), qRgba(40, 100, 200, 255), qRgba(1, 2, 3, 17)};
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x) image.setPixel(x, y, pixels[y * 3 + x]);
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}
}
class AdjustmentEffectsTest : public QObject {
  Q_OBJECT
private slots:
  void referenceDefaultsAndRanges() {
    struct Expected { const char *id; const char *parameter; double initial, minimum, maximum; };
    const Expected expected[]{
      {"alpha", "amount", 100, 0, 100}, {"brightness", "amount", 0, -100, 100},
      {"contrast", "amount", 0, -100, 100}, {"grayscale", "strength", 100, 0, 100},
      {"saturation", "amount", 0, -100, 100}};
    for (const auto &e : expected) {
      const auto *effect = builtinEffects().find(e.id);
      QVERIFY(effect);
      const auto &spec = effect->spec();
      QCOMPARE(spec.parameters.size(), 1);
      const auto &p = spec.parameters.front();
      QCOMPARE(p.id, QString(e.parameter));
      QCOMPARE(p.defaultValue.toDouble(), e.initial);
      QCOMPARE(p.minimum, e.minimum);
      QCOMPARE(p.maximum, e.maximum);
      QVERIFY(!resolveParameters(spec, {{p.id, e.minimum - 0.01}}).ok());
      QVERIFY(!resolveParameters(spec, {{p.id, e.maximum + 0.01}}).ok());
    }
    const auto *invert = builtinEffects().find("invert");
    QVERIFY(invert->spec().parameters.isEmpty());
    QCOMPARE(invert->spec().mode, Mode::Immediate);
  }
  void neutralParametersAndInvertInvolution() {
    const auto image = fixture();
    for (const auto *spec : builtinEffects().specs()) {
      if (!kPilots.contains(spec->id)) continue;
      const auto *effect = builtinEffects().find(spec->id);
      ParamValues params;
      if (spec->id != "invert")
        params.insert(spec->parameters.front().id, spec->id == "alpha" ? 100.0 : 0.0);
      const auto result = runEffect(*effect, image, params, {});
      QVERIFY(result.ok());
      const QImage output = spec->id == "invert" ? runEffect(*effect, result.image, {}, {}).image : result.image;
      QCOMPARE(output, image);
      QCOMPARE(output.colorSpace(), image.colorSpace());
    }
  }
  void handComputedPixels_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<double>("amount");
    QTest::addColumn<QRgb>("expected");
    // Source (40,100,200,128). Matrix channels round to nearest and clip to 0..255.
    QTest::newRow("alpha-half") << QString("alpha") << 50.0 << qRgba(40, 100, 200, 64);
    QTest::newRow("alpha-quarter") << QString("alpha") << 25.0 << qRgba(40, 100, 200, 32);
    QTest::newRow("alpha-zero") << QString("alpha") << 0.0 << qRgba(40, 100, 200, 0);
    // Brightness +/-20 adds/subtracts 51 from RGB.
    QTest::newRow("brightness-up") << QString("brightness") << 20.0 << qRgba(91, 151, 251, 128);
    QTest::newRow("brightness-down") << QString("brightness") << -20.0 << qRgba(0, 49, 149, 128);
    QTest::newRow("brightness-max") << QString("brightness") << 100.0 << qRgba(255, 255, 255, 128);
    // Contrast: (channel - 127.5) * (1 + amount/100)^2 + 127.5.
    QTest::newRow("contrast-zero-scale") << QString("contrast") << -100.0 << qRgba(128, 128, 128, 128);
    QTest::newRow("contrast-half") << QString("contrast") << 50.0 << qRgba(0, 66, 255, 128);
    QTest::newRow("contrast-max") << QString("contrast") << 100.0 << qRgba(0, 18, 255, 128);
    // Grayscale luma = .2126*40 + .7152*100 + .0722*200 = 94.464.
    QTest::newRow("gray-full") << QString("grayscale") << 100.0 << qRgba(94, 94, 94, 128);
    QTest::newRow("gray-half") << QString("grayscale") << 50.0 << qRgba(67, 97, 147, 128);
    // Saturation uses its distinct reference luma: .3086*40+.6094*100+.082*200 = 89.684.
    QTest::newRow("saturation-min") << QString("saturation") << -100.0 << qRgba(90, 90, 90, 128);
    QTest::newRow("saturation-half") << QString("saturation") << 50.0 << qRgba(15, 105, 255, 128);
    QTest::newRow("saturation-max") << QString("saturation") << 100.0 << qRgba(0, 110, 255, 128);
    QTest::newRow("invert") << QString("invert") << 0.0 << qRgba(215, 155, 55, 128);
  }
  void handComputedPixels() {
    QFETCH(QString, id);
    QFETCH(double, amount);
    QFETCH(QRgb, expected);
    const auto *effect = builtinEffects().find(id);
    ParamValues params;
    if (id != "invert") params.insert(effect->spec().parameters.front().id, amount);
    const auto image = fixture();
    const auto result = runEffect(*effect, image, params, {});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image.pixel(0, 0), expected);
    QCOMPARE(result.image.colorSpace(), image.colorSpace());
    QCOMPARE(result.image.format(), QImage::Format_ARGB32);
    QCOMPARE(result.image.size(), image.size());
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        QCOMPARE(qAlpha(result.image.pixel(x, y)), id == "alpha" ?
                 int(std::floor(qAlpha(image.pixel(x, y)) * amount / 100 + 0.5)) : qAlpha(image.pixel(x, y)));
    QCOMPARE(image, fixture());
  }
  void invertOtherPixelsAndUntaggedSource() {
    auto image = fixture();
    image.setColorSpace({});
    const auto result = runEffect(*builtinEffects().find("invert"), image, {}, {});
    QVERIFY(result.ok());
    QCOMPARE(result.image.pixel(1, 0), qRgba(255, 0, 254, 255));
    QCOMPARE(result.image.pixel(2, 0), qRgba(240, 235, 230, 0));
    QVERIFY(!result.image.colorSpace().isValid());
  }
  void everyPilotCancelsWithoutPartialImage() {
    const auto image = fixture();
    for (const auto *spec : builtinEffects().specs()) {
      if (!kPilots.contains(spec->id)) continue;
      const auto *effect = builtinEffects().find(spec->id);
      const auto resolved = resolveParameters(*spec, {});
      int progress = 0;
      int lastProgress = -1;
      bool monotonic = true;
      EffectContext context{[&] { return progress >= 33; }, [&](int value) {
        monotonic = monotonic && value >= lastProgress && value >= 0 && value <= 100;
        progress = lastProgress = value;
      }};
      const auto direct = effect->apply(image, resolved.values, context);
      QCOMPARE(direct.error, QStringLiteral("effect-cancelled"));
      QVERIFY(direct.image.isNull());
      QVERIFY(progress >= 33 && progress < 100 && monotonic);
      progress = 0;
      lastProgress = -1;
      const auto result = runEffect(*effect, image, {}, context);
      QCOMPARE(result.error, QStringLiteral("effect-cancelled"));
      QVERIFY(result.image.isNull());
      QCOMPARE(image, fixture());
      const auto cancelled = effect->apply(image, resolved.values, {[] { return true; }});
      QCOMPARE(cancelled.error, QStringLiteral("effect-cancelled"));
      QVERIFY(cancelled.image.isNull());
      progress = 0;
      lastProgress = -1;
      context.cancelled = [] { return false; };
      QVERIFY(runEffect(*effect, image, {}, context).ok());
      QCOMPARE(progress, 100);
      QVERIFY(monotonic);
    }
  }
};
QTEST_GUILESS_MAIN(AdjustmentEffectsTest)
#include "AdjustmentEffectsTest.moc"
