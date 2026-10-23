// EDITOR-EFFECTS-001: the Filters category, first batch (the simple filters).
// EE-003, EE-008..EE-010, EE-016, EE-018.
// The maths-defined filters assert hand-computed pixels; the stylised and procedural ones
// assert regions and statistics instead, so the tests do not depend on the RNG stream.
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QSet>
#include <QTest>
#include <algorithm>

using namespace xerahs::effects;
namespace {
const QStringList kIds{"emboss", "mean_removal", "smooth", "edge_detect", "frosted_glass", "scanline",
                       "tv_static", "palette_map", "pixelate", "color_depth", "sharpen"};
const QStringList kSeeded{"frosted_glass", "tv_static"};
const QStringList kImmediate{"emboss", "mean_removal", "smooth", "edge_detect"};

// 3x3 grey fixture used for the hand-computed convolution pixels:
//   10 200  20
//   30  55  70
//   40  10  85
QImage grey3x3() {
  const QRgb pixels[]{qRgba(10, 10, 10, 255), qRgba(200, 200, 200, 255), qRgba(20, 20, 20, 255),
                      qRgba(30, 30, 30, 255), qRgba(55, 55, 55, 255), qRgba(70, 70, 70, 255),
                      qRgba(40, 40, 40, 255), qRgba(10, 10, 10, 255), qRgba(85, 85, 85, 255)};
  QImage image(3, 3, QImage::Format_ARGB32);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x) image.setPixel(x, y, pixels[y * 3 + x]);
  return image;
}

// 4x4 grey ramp used for the hand-computed pixelate pixels:
//    0   1   2   3
//    4   5   6   7
//    8   9  10  11
//   12  13  14  15
QImage grey4x4() {
  QImage image(4, 4, QImage::Format_ARGB32);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) image.setPixel(x, y, qRgba(y * 4 + x, y * 4 + x, y * 4 + x, 255));
  return image;
}

// A non-uniform 32x32 image in a wide colour space.
QImage fixture() {
  QImage image(32, 32, QImage::Format_ARGB32);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x)
      image.setPixel(x, y, qRgba((x * 7 + 3) & 255, (y * 5 + 11) & 255, (x * y) & 255, 255));
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}

QImage solid(int width, int height, QRgb color) {
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}

EffectResult run(const QString &id, const QImage &image, const ParamValues &params = {},
                  const EffectContext &context = {}) {
  const auto *effect = builtinEffects().find(id);
  return effect ? runEffect(*effect, image, params, context) : EffectResult{{}, "missing"};
}

EffectContext seeded(quint64 seed) {
  EffectContext context;
  context.seed = seed;
  return context;
}

bool samePixels(const QImage &a, const QImage &b) {
  if (a.size() != b.size() || a.format() != b.format()) return false;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (a.pixel(x, y) != b.pixel(x, y)) return false;
  return true;
}
}

class FilterEffects1Test : public QObject {
  Q_OBJECT
private slots:
  void elevenRegisteredWithReferenceAndValidDefaults() {
    QCOMPARE(kIds.size(), 11);
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.id, id);
      QCOMPARE(spec.category, Category::Filters);
      QCOMPARE(spec.mode, kImmediate.contains(id) ? Mode::Immediate : Mode::Previewable);
      QCOMPARE(spec.usesSeed, kSeeded.contains(id));
      QVERIFY(!spec.changesSize);
      QVERIFY(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/"));
      QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
      QVERIFY2(resolveParameters(spec, {}).ok(), qPrintable(id));
      for (const auto &parameter : spec.parameters) {
        QVERIFY2(parameter.defaultValue.toDouble() >= parameter.minimum, qPrintable(id + "/" + parameter.id));
        QVERIFY2(parameter.defaultValue.toDouble() <= parameter.maximum, qPrintable(id + "/" + parameter.id));
        QVERIFY(!resolveParameters(spec, {{parameter.id, parameter.minimum - 0.5}}).ok());
        QVERIFY(!resolveParameters(spec, {{parameter.id, parameter.maximum + 0.5}}).ok());
      }
    }
  }

  void referenceDefaults() {
    struct Expected { const char *id; const char *parameter; double initial, minimum, maximum; };
    const Expected expected[]{
      {"frosted_glass", "radius", 5, 1, 20},         {"scanline", "spacing", 4, 2, 20},
      {"scanline", "opacity", 50, 0, 100},             {"tv_static", "amount", 40, 0, 100},
      {"palette_map", "levels", 6, 2, 32},             {"pixelate", "size", 10, 1, 200},
      {"color_depth", "bitsPerChannel", 4, 1, 8},      {"sharpen", "strength", 50, 0, 100}};
    for (const auto &entry : expected) {
      const auto &spec = builtinEffects().find(entry.id)->spec();
      const auto parameter = std::find_if(spec.parameters.cbegin(), spec.parameters.cend(),
                                          [&](const ParamSpec &p) { return p.id == entry.parameter; });
      QVERIFY2(parameter != spec.parameters.cend(), entry.parameter);
      QCOMPARE(parameter->defaultValue.toDouble(), entry.initial);
      QCOMPARE(parameter->minimum, entry.minimum);
      QCOMPARE(parameter->maximum, entry.maximum);
    }
    QCOMPARE(builtinEffects().find("frosted_glass")->spec().parameters.size(), 1);
    QCOMPARE(builtinEffects().find("scanline")->spec().parameters.size(), 2);
    QCOMPARE(builtinEffects().find("emboss")->spec().parameters.size(), 0);
  }

  void runEffectKeepsThePixelContract() {
    const auto image = fixture();
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto result = runEffect(*effect, image, {}, {});
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.size(), image.size());
      QCOMPARE(result.image.colorSpace(), image.colorSpace());
    }
  }

  void defaultsChangeTheImage() {
    const auto image = fixture();
    for (const QString &id : kIds) {
      const auto result = run(id, image);
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QVERIFY2(!samePixels(result.image, image), qPrintable(id));
    }
  }

  void neutralSettingsGiveIdenticalImages() {
    const auto image = fixture();
    struct Case { const char *id; ParamValues params; };
    const Case cases[]{
      {"scanline", {{"opacity", 0.0}}},   {"tv_static", {{"amount", 0.0}}}, {"pixelate", {{"size", 1}}},
      {"color_depth", {{"bitsPerChannel", 8}}}, {"sharpen", {{"strength", 0}}}};
    for (const auto &entry : cases) {
      const auto result = run(entry.id, image, entry.params);
      QVERIFY2(result.ok(), entry.id);
      QVERIFY2(samePixels(result.image, image), entry.id);
    }
  }

  void embossMatchesTheReferenceKernel() {
    // Kernel -1 0 -1 / 0 4 0 / -1 0 -1, gain 1, bias 127, edges clamped to the image.
    // Centre (1,1): -(10) - (20) + 4 * (55) - (40) - (85) = 65; 65 + 127 = 192.
    // Corner (0,0): the clamped neighbourhood gives -270; -270 + 127 = -143, clamped to 0.
    const auto result = run("emboss", grey3x3());
    QVERIFY(result.ok());
    QCOMPARE(qRed(result.image.pixel(1, 1)), 192);
    QCOMPARE(qGreen(result.image.pixel(1, 1)), 192);
    QCOMPARE(qBlue(result.image.pixel(1, 1)), 192);
    QCOMPARE(qRed(result.image.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(result.image.pixel(1, 1)), 255);
  }

  void meanRemovalMatchesTheReferenceKernel() {
    // Kernel -1 -1 -1 / -1 9 -1 / -1 -1 -1, gain 1, no bias.
    // Centre (1,1): 9 * (55) - (10 + 200 + 20 + 30 + 70 + 40 + 10 + 85) = 495 - 465 = 30.
    const auto result = run("mean_removal", grey3x3());
    QVERIFY(result.ok());
    QCOMPARE(qRed(result.image.pixel(1, 1)), 30);
    QCOMPARE(qAlpha(result.image.pixel(1, 1)), 255);
  }

  void smoothMatchesTheReferenceKernel() {
    // Kernel 1/9 per sample over the 3x3 neighbourhood.
    // Centre (1,1): (10 + 200 + 20 + 30 + 55 + 70 + 40 + 10 + 85) / 9 = 520 / 9 = 57.78 -> 58.
    const auto result = run("smooth", grey3x3());
    QVERIFY(result.ok());
    QCOMPARE(qRed(result.image.pixel(1, 1)), 58);
    QCOMPARE(qAlpha(result.image.pixel(1, 1)), 255);
  }

  void edgeDetectMatchesTheReferenceKernel() {
    // Kernel -1 -1 -1 / 0 0 0 / 1 1 1, gain 1, bias 127.
    // Centre (1,1): -(10 + 200 + 20) + (40 + 10 + 85) = -230 + 135 = -95; -95 + 127 = 32.
    const auto result = run("edge_detect", grey3x3());
    QVERIFY(result.ok());
    QCOMPARE(qRed(result.image.pixel(1,  1)), 32);
    QCOMPARE(qAlpha(result.image.pixel(1, 1)), 255);
  }

  void sharpenAppliesTheReferenceLaplacian() {
    // Strength 50 -> s = 0.5, kernel 0 -0.5 0 / -0.5 3 -0.5 / 0 -0.5 0, alpha convolved too.
    // Centre (1,1): -0.5 * (200 + 10 + 30 + 70) + 3 * 55 = -155 + 165 = 10.
    const auto result = run("sharpen", grey3x3(), {{"strength", 50}});
    QVERIFY(result.ok());
    QCOMPARE(qRed(result.image.pixel(1, 1)), 10);
    QCOMPARE(qAlpha(result.image.pixel(1, 1)), 255);
  }

  void scanlineDarkensOnlyTheLineRows() {
    // Opacity 50 -> the line alpha is the truncated byte (byte)(255 * 0.5) = 127, so a line
    // pixel keeps (255 - 127) / 255 of its value: 10 -> 5, 200 -> 100, 20 -> 10.
    // Spacing 2 puts the lines on rows 0 and 2; row 1 is untouched.
    const auto image = grey3x3();
    const auto result = run("scanline", image, {{"spacing", 2}, {"opacity", 50.0}});
    QVERIFY(result.ok());
    QCOMPARE(qRed(result.image.pixel(0, 0)), 5);
    QCOMPARE(qRed(result.image.pixel(1, 0)), 100);
    QCOMPARE(qRed(result.image.pixel(2, 0)), 10);
    QCOMPARE(qRed(result.image.pixel(0, 2)), 20);
    QCOMPARE(qRed(result.image.pixel(2, 2)), 43);
    QCOMPARE(result.image.pixel(0, 1), image.pixel(0, 1));
    QCOMPARE(result.image.pixel(1, 1), image.pixel(1, 1));
    QCOMPARE(result.image.pixel(2, 1), image.pixel(2, 1));
  }

  void paletteMapQuantisesToTheRequestedLevels() {
    // Levels 6 -> step 255 / 5 = 51; the nearest level wins and the level value is truncated.
    // 0 -> 0, 25 -> 0, 26 -> 51, 128 -> 153, 255 -> 255; the alpha is untouched.
    QImage image(5, 1, QImage::Format_ARGB32);
    const int values[]{0, 25, 26, 128, 255};
    for (int x = 0; x < 5; ++x) image.setPixel(x, 0, qRgba(values[x], values[x], values[x], 200));
    const auto result = run("palette_map", image, {{"levels", 6}});
    QVERIFY(result.ok());
    const int expected[]{0, 0, 51, 153, 255};
    for (int x = 0; x < 5; ++x) {
      QCOMPARE(qRed(result.image.pixel(x, 0)), expected[x]);
      QCOMPARE(qGreen(result.image.pixel(x, 0)), expected[x]);
      QCOMPARE(qAlpha(result.image.pixel(x, 0)), 200);
    }
  }

  void colorDepthSnapsEachChannelToTheBitGrid() {
    // 4 bits per channel -> interval 255 / 15 = 17; each channel snaps to the nearest multiple.
    // 0 -> 0, 8 -> 0, 9 -> 17, 85 -> 85, 170 -> 170, 255 -> 255; the alpha is untouched.
    QImage image(6, 1, QImage::Format_ARGB32);
    const int values[]{0, 8, 9, 85, 170, 255};
    for (int x = 0; x < 6; ++x) image.setPixel(x, 0, qRgba(values[x], values[x], values[x], 128));
    const auto result = run("color_depth", image, {{"bitsPerChannel", 4}});
    QVERIFY(result.ok());
    const int expected[]{0, 0, 17, 85, 170, 255};
    for (int x = 0; x < 6; ++x) {
      QCOMPARE(qRed(result.image.pixel(x, 0)), expected[x]);
      QCOMPARE(qGreen(result.image.pixel(x, 0)), expected[x]);
      QCOMPARE(qAlpha(result.image.pixel(x, 0)), 128);
    }
  }

  void pixelateSquaresThePixelsOff() {
    // Size 2 on a 4x4 image: the reduced grid samples the centre of each block and the
    // result is scaled back up with the same nearest rule.
    // Reduced grid: source(1,1) = 5, source(3,1) = 7, source(1,3) = 13, source(3,3) = 15.
    // Result:      5 5 7 7 / 5 5 7 7 / 13 13 15 15 / 13 13 15 15.
    const auto result = run("pixelate", grey4x4(), {{"size", 2}});
    QVERIFY(result.ok());
    const int expected[4][4]{{5, 5, 7, 7}, {5, 5, 7, 7}, {13, 13, 15, 15}, {13, 13, 15, 15}};
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) QCOMPARE(qRed(result.image.pixel(x, y)), expected[y][x]);
  }

  void pixelateKeepsFlatAreasFlat() {
    const auto image = solid(16, 16, qRgba(90, 120, 150, 255));
    const auto result = run("pixelate", image, {{"size", 4}});
    QVERIFY(result.ok());
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) QCOMPARE(result.image.pixel(x, y), image.pixel(0, 0));
  }

  void frostedGlassDisplacesPixelsWithinTheRadius() {
    QImage image = solid(16, 16, qRgba(0, 0, 0, 255));
    image.setPixel(0, 0, qRgba(255, 255, 255, 255));
    const auto result = run("frosted_glass", image, {{"radius", 5}}, seeded(1234));
    QVERIFY(result.ok());
    // A displaced pixel is always a source pixel, and the single white pixel can only land
    // inside the radius box that the displacement can reach.
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        const QRgb pixel = result.image.pixel(x, y);
        QVERIFY(pixel == qRgba(0, 0, 0, 255) || pixel == qRgba(255, 255, 255, 255));
        if (pixel == qRgba(255, 255, 255, 255)) QVERIFY2(x <= 5 && y <= 5, qPrintable(QString::number(x) + "," + QString::number(y)));
      }
  }

  void frostedGlassKeepsFlatAreasFlat() {
    const auto image = solid(16, 16, qRgba(40, 80, 120, 255));
    const auto result = run("frosted_glass", image, {{"radius", 5}}, seeded(9));
    QVERIFY(result.ok());
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) QCOMPARE(result.image.pixel(x, y), image.pixel(0, 0));
  }

  void tvStaticBlendsTowardsGreyNoise() {
    const auto image = fixture();
    const auto result = run("tv_static", image, {{"amount", 100.0}}, seeded(3));
    QVERIFY(result.ok());
    // At full amount every channel is the same random byte, so the result is grey.
    QSet<int> greys;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) {
        const QRgb pixel = result.image.pixel(x, y);
        QCOMPARE(qRed(pixel), qGreen(pixel));
        QCOMPARE(qGreen(pixel), qBlue(pixel));
        QCOMPARE(qAlpha(pixel), 255);
        greys.insert(qRed(pixel));
      }
    // The noise is not a single flat value.
    QVERIFY(greys.size() > 16);
  }

  void seededEffectsUseOnlyTheContextSeed() {
    const auto image = fixture();
    for (const QString &id : kSeeded) {
      const auto first = run(id, image, {}, seeded(7));
      const auto again = run(id, image, {}, seeded(7));
      const auto other = run(id, image, {}, seeded(8));
      QVERIFY2(first.ok() && again.ok() && other.ok(), qPrintable(id));
      QVERIFY2(samePixels(first.image, again.image), qPrintable(id));
      QVERIFY2(!samePixels(first.image, other.image), qPrintable(id));
    }
  }

  void everyEffectCancelsWithoutPartialImage() {
    const auto image = fixture();
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto resolved = resolveParameters(effect->spec(), {});
      QVERIFY(resolved.ok());
      const auto direct = effect->apply(image, resolved.values, {[] { return true; }});
      QVERIFY2(direct.error == "effect-cancelled", qPrintable(id));
      QVERIFY(direct.image.isNull());
      const auto viaRun = runEffect(*effect, image, {}, {[] { return true; }});
      QCOMPARE(viaRun.error, QStringLiteral("effect-cancelled"));
      QVERIFY(viaRun.image.isNull());
      QVERIFY(samePixels(image, fixture()));
    }
  }

  void cancellationStopsAtTheNextRow() {
    const auto image = fixture();
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      int progress = 0;
      int lastProgress = -1;
      bool monotonic = true;
      EffectContext context{[&] { return progress >= 33; }, [&](int value) {
        monotonic = monotonic && value >= lastProgress && value >= 0 && value <= 100;
        progress = lastProgress = value;
      }};
      const auto result = runEffect(*effect, image, {}, context);
      QVERIFY2(result.error == "effect-cancelled", qPrintable(id + ": " + result.error));
      QVERIFY(result.image.isNull());
      QVERIFY2(progress >= 33 && progress < 100, qPrintable(id));
      QVERIFY(monotonic);
      QVERIFY(samePixels(image, fixture()));
    }
  }

  void progressReachesTheEndWhenNotCancelled() {
    const auto image = fixture();
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      int progress = 0;
      int lastProgress = -1;
      bool monotonic = true;
      EffectContext context{[] { return false; }, [&](int value) {
        monotonic = monotonic && value >= lastProgress && value >= 0 && value <= 100;
        progress = lastProgress = value;
      }};
      const auto result = runEffect(*effect, image, {}, context);
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QCOMPARE(progress, 100);
      QVERIFY(monotonic);
    }
  }
};
QTEST_GUILESS_MAIN(FilterEffects1Test)
#include "FilterEffects1Test.moc"
