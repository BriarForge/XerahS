// EDITOR-EFFECTS-001: Filters batch F6.
#include "image-editor/effects/Effect.h"

#include <QColorSpace>
#include <QSet>
#include <QTest>

#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kIds{"cartoon_sticker_cutout", "jpeg_artifact",     "snowfall_depth_fog",    "celtic_knot_border",
                       "chrome_metallic_border", "frost_ice_border",   "stained_glass",         "art_deco_border",
                       "liquid_glass",           "ink_splatter_drips", "mosaic_polygon",        "perlin_noise",
                       "dithering",              "ornate_scroll_border", "neon_edge_glow",      "watercolor_kuwahara",
                       "wooden_frame",           "circuit_board_border", "pixel_sorting",       "vintage_print_damage",
                       "matrix_digital_rain",    "windows11_window",     "bevel",               "stained_glass_border",
                       "golden_border",          "nebula_starfield",     "clouds",              "blood_splash"};
const QStringList kSeeded{"snowfall_depth_fog", "stained_glass",      "liquid_glass",    "ink_splatter_drips",
                          "mosaic_polygon",      "wooden_frame",       "pixel_sorting",   "vintage_print_damage",
                          "matrix_digital_rain", "nebula_starfield",   "blood_splash"};
const QStringList kResizing{"celtic_knot_border", "chrome_metallic_border", "frost_ice_border", "art_deco_border",
                            "ornate_scroll_border", "wooden_frame", "circuit_board_border", "windows11_window",
                            "stained_glass_border", "golden_border"};

QImage fixture() {
  QImage image(3, 3, QImage::Format_ARGB32);
  const QRgb pixels[]{qRgba(40, 100, 200, 128), qRgba(0, 255, 1, 255), qRgba(15, 20, 25, 0),
                      qRgba(255, 255, 255, 1), qRgba(0, 0, 0, 254), qRgba(127, 128, 129, 64),
                      qRgba(40, 100, 200, 0), qRgba(255, 255, 255, 255), qRgba(1, 2, 3, 17)};
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x) image.setPixel(x, y, pixels[y * 3 + x]);
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}
QImage solid(int w, int h, QRgb color) {
  QImage image(w, h, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}
QImage varied() {
  QImage image(16, 8, QImage::Format_ARGB32);
  image.fill(qRgba(20, 40, 60, 255));
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      image.setPixel(x, y, qRgba((x * 15) & 255, (y * 30) & 255, (80 + x * 3) & 255, 255));
  image.setPixel(0, 0, qRgba(255, 0, 0, 255));
  image.setPixel(1, 0, qRgba(0, 255, 0, 255));
  image.setPixel(0, 2, qRgba(255, 255, 255, 255));
  image.setPixel(1, 2, qRgba(0, 0, 0, 255));
  image.setPixel(15, 5, qRgba(0, 0, 0, 0));
  return image;
}
QImage wideField() {
  QImage image(64, 48, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      image.setPixel(x, y, qRgba((x * 4) & 255, (y * 5) & 255, (40 + x + y) & 255, 255));
  return image;
}
EffectResult run(const QString &id, const QImage &image, const ParamValues &params = {}, const EffectContext &context = {}) {
  const auto *effect = builtinEffects().find(id);
  return effect ? runEffect(*effect, image, params, context) : EffectResult{{}, "missing"};
}
double meanChannel(const QImage &image, int shift) {
  double sum = 0;
  const int n = std::max(1, image.width() * image.height());
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) sum += (image.pixel(x, y) >> shift) & 0xFF;
  return sum / n;
}
int uniqueColors(const QImage &image) {
  QSet<QRgb> colors;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) colors.insert(image.pixel(x, y));
  return colors.size();
}
QSize expectedSize(const QString &id, const QSize &source) {
  auto grown = [&](int border) { return QSize(source.width() + border * 2, source.height() + border * 2); };
  if (id == "celtic_knot_border") return grown(52);
  if (id == "chrome_metallic_border") return grown(36);
  if (id == "frost_ice_border") return grown(55);
  if (id == "art_deco_border") return grown(64);
  if (id == "ornate_scroll_border") return grown(48);
  if (id == "wooden_frame") return grown(48);
  if (id == "circuit_board_border") return grown(64);
  if (id == "stained_glass_border") return grown(60);
  if (id == "golden_border") return grown(25);
  if (id == "windows11_window") {
    // padding 20 + shadow 16 on each side, plus the 32px title bar.
    const int pad = (20 + 16) * 2;
    return QSize(source.width() + pad, source.height() + 32 + pad);
  }
  return source;
}
}

class FilterEffects6Test : public QObject {
  Q_OBJECT
private slots:
  void allTwentyEightRegisteredWithReferenceAndValidDefaults() {
    QCOMPARE(kIds.size(), 28);
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.category, Category::Filters);
      QCOMPARE(spec.mode, Mode::Previewable);
      QVERIFY(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/"));
      QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
      QCOMPARE(spec.changesSize, kResizing.contains(id));
      QCOMPARE(spec.usesSeed, kSeeded.contains(id));
      QVERIFY2(resolveParameters(spec, {}).ok(), qPrintable(id));
      for (const auto &p : spec.parameters) {
        if (p.kind == ParamKind::Float || p.kind == ParamKind::Int) {
          QVERIFY2(p.defaultValue.toDouble() >= p.minimum && p.defaultValue.toDouble() <= p.maximum,
                   qPrintable(id + "/" + p.id));
          QVERIFY(!resolveParameters(spec, {{p.id, p.minimum - 0.5}}).ok());
          QVERIFY(!resolveParameters(spec, {{p.id, p.maximum + 0.5}}).ok());
        } else if (p.kind == ParamKind::Enum) {
          QVERIFY(p.choices.contains(p.defaultValue.toString()));
          QVERIFY(!resolveParameters(spec, {{p.id, QString("no-such-choice")}}).ok());
        } else if (p.kind == ParamKind::Bool) {
          QVERIFY(p.defaultValue.userType() == QMetaType::Bool);
        } else if (p.kind == ParamKind::Text) {
          QVERIFY(p.defaultValue.canConvert<QString>());
        } else {
          QCOMPARE(p.kind, ParamKind::Color);
          QVERIFY(p.defaultValue.value<QColor>().isValid());
        }
      }
    }
    QCOMPARE(builtinEffects().find("dithering")->spec().parameters[0].defaultValue.toString(), "floyd_steinberg");
    QCOMPARE(builtinEffects().find("dithering")->spec().parameters[1].defaultValue.toString(), "one_bit_bw");
    QCOMPARE(builtinEffects().find("mosaic_polygon")->spec().parameters[1].defaultValue.toString(), "hexagon");
    QCOMPARE(builtinEffects().find("pixel_sorting")->spec().parameters[0].defaultValue.toString(), "vertical");
    QCOMPARE(builtinEffects().find("neon_edge_glow")->spec().parameters[5].defaultValue.value<QColor>(), QColor(0, 240, 255));
    QCOMPARE(builtinEffects().find("bevel")->spec().parameters[3].defaultValue.value<QColor>(), QColor(255, 255, 255, 217));
    QCOMPARE(builtinEffects().find("matrix_digital_rain")->spec().parameters[7].defaultValue.toString(),
             QStringLiteral("01<>[]{}*+-/\\=#$%&"));
    QCOMPARE(builtinEffects().find("windows11_window")->spec().displayName, "Windows 11");
    QVERIFY(builtinEffects().find("perlin_noise")->spec().parameters[4].defaultValue.toBool());
    QVERIFY(!builtinEffects().find("windows11_window")->spec().parameters[2].defaultValue.toBool());
  }

  void neutralParametersGiveIdenticalImage_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariantMap>("params");
    QTest::addColumn<bool>("flat");
    QTest::newRow("jpeg") << "jpeg_artifact" << QVariantMap{{"compression", 0.0}, {"ringing", 0.0}} << false;
    QTest::newRow("snow") << "snowfall_depth_fog" << QVariantMap{{"snow_amount", 0.0}, {"fog_amount", 0.0}} << false;
    QTest::newRow("liquid") << "liquid_glass"
                            << QVariantMap{{"distortion", 0.0}, {"refraction", 0.0}, {"chroma_shift", 0}, {"gloss", 0.0}} << false;
    QTest::newRow("ink") << "ink_splatter_drips" << QVariantMap{{"ink_amount", 0.0}, {"paper_fade", 0.0}} << false;
    QTest::newRow("perlin") << "perlin_noise" << QVariantMap{{"strength", 0.0}} << false;
    QTest::newRow("dither") << "dithering" << QVariantMap{{"strength", 0.0}} << false;
    QTest::newRow("vintage") << "vintage_print_damage" << QVariantMap{{"sepia_amount", 0.0},
                                                                      {"fade_amount", 0.0},
                                                                      {"grain_amount", 0.0},
                                                                      {"scratch_amount", 0.0},
                                                                      {"dust_amount", 0.0}}
                             << false;
    QTest::newRow("bevel") << "bevel" << QVariantMap{{"strength", 0.0}} << false;
    QTest::newRow("clouds") << "clouds" << QVariantMap{{"coverage", 0.0}} << false;
    QTest::newRow("nebula") << "nebula_starfield" << QVariantMap{{"intensity", 0.0}} << false;
    QTest::newRow("blood") << "blood_splash" << QVariantMap{{"splash_amount", 0.0}} << false;
    QTest::newRow("sort") << "pixel_sorting" << QVariantMap{{"threshold_low", 90.0}, {"threshold_high", 100.0}} << false;
    QTest::newRow("neon") << "neon_edge_glow" << QVariantMap{{"base_dim", 0.0}} << true;
    // Quadrant means equal the source only on a flat field. Saturation and detail at 0 then copy it.
    QTest::newRow("watercolor") << "watercolor_kuwahara"
                                << QVariantMap{{"saturation_boost", 0.0}, {"detail_blend", 0.0}} << true;
    QTest::newRow("cartoon-black") << "cartoon_sticker_cutout" << QVariantMap{} << true;
  }
  void neutralParametersGiveIdenticalImage() {
    QFETCH(QString, id);
    QFETCH(QVariantMap, params);
    QFETCH(bool, flat);
    // Cartoon has no zero-strength bypass. Solid black quantizes to 0 with a zero gradient, so it stays black.
    // Neon on a flat field has a zero Sobel response, so base_dim 0 is a copy.
    const auto image = id == "cartoon_sticker_cutout" ? solid(4, 4, qRgba(0, 0, 0, 200))
                       : flat                         ? solid(6, 6, qRgba(40, 90, 140, 255))
                       : id == "pixel_sorting"        ? solid(4, 4, qRgba(255, 0, 0, 180))
                                                      : fixture();
    const auto result = run(id, image, params);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image, image);
    QCOMPARE(result.image.colorSpace(), image.colorSpace());
  }

  void handComputedPixels() {
    // Cartoon levels 8: step = 255/7. Round(100/step) = 3, 3*step = 765/7 ≈ 109.2857, ClampToByte = 109.
    // Ink and the sticker border are off, so a flat field is only the quantizer.
    QCOMPARE(run("cartoon_sticker_cutout", solid(1, 1, qRgba(100, 100, 100, 200)),
                 {{"colorLevels", 8}, {"inkStrength", 0.0}, {"stickerBorder", 0}})
                 .image.pixel(0, 0),
             qRgba(109, 109, 109, 200));

    // JPEG amount 1, block 8, no chroma bleed, no ringing. Black YCbCr is (0, 128, 128).
    // lumaStep = 23, chromaStep = 40. quant(128/40) = 3, so Cb = Cr = 120.
    // G = -0.344136*(120-128) - 0.714136*(120-128) ≈ 8.466 → 8. R and B clamp to 0.
    QCOMPARE(run("jpeg_artifact", solid(1, 1, qRgba(0, 0, 0, 200)),
                 {{"compression", 100.0}, {"chroma_bleed", 0.0}, {"ringing", 0.0}, {"block_size", 8}})
                 .image.pixel(0, 0),
             qRgba(0, 8, 0, 200));

    // Perlin, one octave, scale 1, strength 100, noise seed 0. perlin2(0, 0) is 0, so the
    // normalized noise is 0.5 and ClampToByte(0.5*255) = 128. Alpha is copied.
    QCOMPARE(run("perlin_noise", solid(1, 1, qRgba(0, 0, 0, 200)),
                 {{"scale", 1.0}, {"octaves", 1}, {"persistence", 1.0}, {"strength", 100.0}, {"monochrome", true}})
                 .image.pixel(0, 0),
             qRgba(128, 128, 128, 200));

    // Bayer 4x4, 1-bit, strength 100. bias = (matrix[y&3][x&3]/16 - 0.5) * 36.
    // x=0 matrix 0 → bias -18, gray 122 < 128 → 0. x=1 matrix 8 → bias 0, gray 140 ≥ 128 → 255.
    QImage bayer(2, 1, QImage::Format_ARGB32);
    bayer.fill(qRgba(140, 140, 140, 180));
    const auto dithered = run("dithering", bayer, {{"method", "bayer_4x4"}, {"palette", "one_bit_bw"}, {"strength", 100.0}});
    QVERIFY(dithered.ok());
    QCOMPARE(dithered.image.pixel(0, 0), qRgba(0, 0, 0, 180));
    QCOMPARE(dithered.image.pixel(1, 0), qRgba(255, 255, 255, 180));

    // Sepia 100, fade/grain/scratch/dust 0, 1x1 so the edge burn is 0.
    // Pure red → (0.393, 0.349, 0.272), then paperMix 0.08 toward (0.93, 0.86, 0.74):
    // 0.43596*255 ≈ 111, 0.38988*255 ≈ 99, 0.30944*255 ≈ 79.
    QCOMPARE(run("vintage_print_damage", solid(1, 1, qRgba(255, 0, 0, 200)),
                 {{"sepia_amount", 100.0}, {"fade_amount", 0.0}, {"grain_amount", 0.0}, {"scratch_amount", 0.0}, {"dust_amount", 0.0}})
                 .image.pixel(0, 0),
             qRgba(111, 99, 79, 200));
  }

  void stylisedRegionsAndStatistics() {
    const auto dark = solid(48, 36, qRgba(12, 18, 28, 255));
    const auto snow = run("snowfall_depth_fog", dark);
    QVERIFY(snow.ok());
    QVERIFY(meanChannel(snow.image, 0) + meanChannel(snow.image, 8) + meanChannel(snow.image, 16) >
            meanChannel(dark, 0) + meanChannel(dark, 8) + meanChannel(dark, 16));

    // A bright field clears the density gate (dark 12-cell samples all hashed above it).
    const auto rain = run("matrix_digital_rain", solid(48, 36, qRgba(220, 220, 230, 255)), {{"glow_amount", 0.0}});
    QVERIFY(rain.ok());
    QVERIFY(meanChannel(rain.image, 8) > meanChannel(rain.image, 16));
    int greenCells = 0;
    for (int y = 0; y < rain.image.height(); ++y)
      for (int x = 0; x < rain.image.width(); ++x) {
        const QRgb c = rain.image.pixel(x, y);
        if (qGreen(c) > qRed(c) + 30 && qGreen(c) > qBlue(c) + 30) ++greenCells;
      }
    QVERIFY(greenCells > 0);

    const auto glass = run("stained_glass", solid(32, 32, qRgba(180, 40, 40, 255)));
    QVERIFY(glass.ok());
    QVERIFY(uniqueColors(glass.image) > 1);
    QVERIFY(meanChannel(glass.image, 0) < 180);

    const auto flat = solid(12, 12, qRgba(70, 140, 200, 255));
    const auto paint = run("watercolor_kuwahara", flat);
    QVERIFY(paint.ok());
    QCOMPARE(uniqueColors(paint.image), 1);

    const auto gold = run("golden_border", fixture());
    QVERIFY(gold.ok());
    QCOMPARE(gold.image.size(), QSize(53, 53));
    const QRgb corner = gold.image.pixel(2, 2);
    QVERIFY(qRed(corner) > 140 && qGreen(corner) > 80);
    QCOMPARE(gold.image.pixel(26, 26), fixture().pixel(1, 1));

    const auto window = run("windows11_window", solid(40, 20, qRgba(10, 20, 30, 255)));
    QVERIFY(window.ok());
    // padding 20 + shadow 16, title bar 32. Sample inside the bar, clear of the radius-8 corners.
    QCOMPARE(window.image.size(), QSize(40 + 72, 20 + 32 + 72));
    QVERIFY(qRed(window.image.pixel(36 + 12, 36 + 8)) > 180);

    // Default vertical sort, brightness, threshold 12..85, min span 8.
    // Gray 40..200 is inside the window. Hash01(0, 0, 3110 xor 7723) ≈ 0.0415 <= 0.85, so the column sorts ascending.
    QImage column(1, 8, QImage::Format_ARGB32);
    const int shuffled[]{200, 40, 180, 50, 160, 60, 140, 70};
    for (int y = 0; y < 8; ++y) column.setPixel(0, y, qRgba(shuffled[y], shuffled[y], shuffled[y], 255));
    const auto sorted = run("pixel_sorting", column);
    QVERIFY(sorted.ok());
    const int ordered[]{40, 50, 60, 70, 140, 160, 180, 200};
    for (int y = 0; y < 8; ++y) QCOMPARE(sorted.image.pixel(0, y), qRgba(ordered[y], ordered[y], ordered[y], 255));
  }

  void defaultsChangeTheImage() {
    const auto image = varied();
    for (const QString &id : kIds) {
      if (id == "pixel_sorting") continue;  // a brightness ramp is already sorted, so the default span is a no-op
      const auto result = run(id, image);
      QVERIFY2(result.ok(), qPrintable(id + " " + result.error));
      QVERIFY2(result.image != image, qPrintable(id));
      if (kResizing.contains(id)) QCOMPARE(result.image.size(), expectedSize(id, image.size()));
    }
  }

  void seededEffectsDependOnlyOnContextSeed() {
    const auto image = wideField();
    for (const QString &id : kSeeded) {
      EffectContext first;
      first.seed = 11;
      EffectContext second;
      second.seed = 11;
      EffectContext third;
      third.seed = 29;
      // A brightness ramp is already sorted, so pixel sorting needs a reversed column to move pixels.
      QImage reversed(16, 16, QImage::Format_ARGB32);
      for (int y = 0; y < reversed.height(); ++y)
        for (int x = 0; x < reversed.width(); ++x)
          reversed.setPixel(x, y, qRgba(200 - y * 8, 40 + x * 3, 180 - y * 6, 255));
      const auto &source = id == "pixel_sorting" ? reversed : image;
      const auto a = run(id, source, {}, first);
      const auto b = run(id, source, {}, second);
      const auto c = run(id, source, {}, third);
      QVERIFY2(a.ok() && b.ok() && c.ok(), qPrintable(id));
      QCOMPARE(a.image, b.image);
      QVERIFY2(a.image != c.image, qPrintable(id));
    }
  }

  void outputContractFormatSizeColourSpace() {
    const auto image = fixture();
    const auto untagged = [&] {
      auto copy = fixture();
      copy.setColorSpace({});
      return copy;
    }();
    for (const QString &id : kIds) {
      const auto result = run(id, image);
      QVERIFY2(result.ok(), qPrintable(id + " " + result.error));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.size(), expectedSize(id, image.size()));
      QCOMPARE(result.image.colorSpace(), image.colorSpace());
      QCOMPARE(image, fixture());
      const auto plain = run(id, untagged);
      QVERIFY2(plain.ok(), qPrintable(id));
      QVERIFY(!plain.image.colorSpace().isValid());
      QVERIFY(run(id, image.convertToFormat(QImage::Format_ARGB32_Premultiplied)).ok());
      QCOMPARE(run(id, image).image, result.image);
    }
    QVERIFY(!run("dithering", image, {{"strength", 200.0}}).ok());
    QVERIFY(!run("dithering", image, {{"method", "atkinson"}}).ok());
    QVERIFY(!run("perlin_noise", image, {{"unknown", 1}}).ok());
  }

  void everyEffectCancelsWithoutPartialImage() {
    const auto image = fixture();
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      const auto resolved = resolveParameters(effect->spec(), {});
      QVERIFY2(resolved.ok(), qPrintable(id));
      int progress = 0;
      int lastProgress = -1;
      bool monotonic = true;
      EffectContext context{[&] { return progress >= 33; }, [&](int value) {
        monotonic = monotonic && value >= lastProgress && value >= 0 && value <= 100;
        progress = lastProgress = value;
      }};
      const auto direct = effect->apply(image, resolved.values, context);
      QVERIFY2(direct.error == "effect-cancelled", qPrintable(id + " " + direct.error));
      QVERIFY(direct.image.isNull());
      QVERIFY2(progress >= 33 && progress < 100 && monotonic, qPrintable(id + " progress " + QString::number(progress)));
      progress = 0;
      lastProgress = -1;
      const auto viaRun = runEffect(*effect, image, {}, context);
      QCOMPARE(viaRun.error, QStringLiteral("effect-cancelled"));
      QVERIFY(viaRun.image.isNull());
      QCOMPARE(image, fixture());
      const auto immediate = effect->apply(image, resolved.values, {[] { return true; }});
      QCOMPARE(immediate.error, QStringLiteral("effect-cancelled"));
      QVERIFY(immediate.image.isNull());
      progress = 0;
      lastProgress = -1;
      monotonic = true;
      context.cancelled = [] { return false; };
      QVERIFY2(runEffect(*effect, image, {}, context).ok(), qPrintable(id));
      QVERIFY2(progress == 100 && monotonic, qPrintable(id + " done " + QString::number(progress)));
    }
  }
};

QTEST_GUILESS_MAIN(FilterEffects6Test)
#include "FilterEffects6Test.moc"
