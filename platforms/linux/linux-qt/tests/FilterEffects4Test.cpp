// EDITOR-EFFECTS-001: EE-003, EE-008..EE-010, EE-016, EE-018 (Filters, batch 4).
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QGuiApplication>
#include <QSet>
#include <QTest>
#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kIds{
    "anamorphic_lens_flare",
    "crystalize_shards",
    "inner_shadow",
    "blueprint_drawing",
    "holographic_foil_shimmer",
    "woodcut_print",
    "rainbow_border",
    "minecraft_grass",
    "crt",
    "plasma_energy_arcs",
};
const QStringList kSeeded{
    "crystalize_shards",
    "blueprint_drawing",
    "holographic_foil_shimmer",
    "crt",
};
const QStringList kChangeSize{
    "rainbow_border",
    "minecraft_grass",
};

// A varied opaque fixture: colour gradients plus a hard-edged bright block and a dark block.
QImage fixture(int w = 40, int h = 32) {
  QImage image(w, h, QImage::Format_ARGB32);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int r = (x * 255) / (w - 1), g = (y * 255) / (h - 1), b = ((x + y) * 255) / (w + h - 2);
      if (x >= w / 4 && x < w / 2 && y >= h / 4 && y < h / 2) r = g = b = 240;
      if (x >= w / 2 && x < 3 * w / 4 && y >= h / 2 && y < 3 * h / 4) r = g = b = 15;
      image.setPixel(x, y, qRgba(r, g, b, 255));
    }
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}
QImage solid(int w, int h, QRgb color) {
  QImage image(w, h, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}
QImage row(const QVector<QRgb> &pixels) {
  QImage image(pixels.size(), 1, QImage::Format_ARGB32);
  for (int x = 0; x < pixels.size(); ++x) image.setPixel(x, 0, pixels[x]);
  return image;
}
EffectResult run(const QString &id, const QImage &image, const ParamValues &params = {}, const EffectContext &context = {}) {
  const auto *effect = builtinEffects().find(id);
  return effect ? runEffect(*effect, image, params, context) : EffectResult{{}, "missing"};
}
QImage img(const QString &id, const QImage &image, const ParamValues &params = {}, quint64 seed = 0) {
  EffectContext context;
  context.seed = seed;
  const auto result = run(id, image, params, context);
  if (!result.ok()) qWarning("effect %s failed: %s", qPrintable(id), qPrintable(result.error));
  return result.image;
}
QRgb px(const QImage &image, int x, int y) { return image.pixel(x, y); }
double meanLum(const QImage &image) {
  double sum = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) sum += qGray(image.pixel(x, y));
  return sum / (double(image.width()) * image.height());
}
int distinctColours(const QImage &image) {
  QSet<QRgb> colours;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) colours.insert(image.pixel(x, y));
  return int(colours.size());
}
}

class FilterEffects4Test : public QObject {
  Q_OBJECT
private slots:
  void allRegisteredWithReferenceAndValidDefaults() {
    QCOMPARE(kIds.size(), QSet<QString>(kIds.begin(), kIds.end()).size());
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.category, Category::Filters);
      QCOMPARE(spec.mode, Mode::Previewable);
      QVERIFY(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/"));
      QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
      QCOMPARE(spec.changesSize, kChangeSize.contains(id));
      QCOMPARE(spec.usesSeed, kSeeded.contains(id));
      QVERIFY2(resolveParameters(spec, {}).ok(), qPrintable(id));
      for (const auto &p : spec.parameters) {
        const QString what = id + "/" + p.id;
        if (p.kind == ParamKind::Float || p.kind == ParamKind::Int) {
          QVERIFY2(p.defaultValue.toDouble() >= p.minimum && p.defaultValue.toDouble() <= p.maximum, qPrintable(what));
          QVERIFY2(!resolveParameters(spec, {{p.id, p.minimum - 1}}).ok(), qPrintable(what));
          QVERIFY2(!resolveParameters(spec, {{p.id, p.maximum + 1}}).ok(), qPrintable(what));
        } else if (p.kind == ParamKind::Enum) {
          QVERIFY2(p.choices.contains(p.defaultValue.toString()), qPrintable(what));
          QVERIFY(!resolveParameters(spec, {{p.id, QString("no-such-choice")}}).ok());
        } else if (p.kind == ParamKind::Color) {
          QVERIFY2(p.defaultValue.value<QColor>().isValid(), qPrintable(what));
        } else {
          QVERIFY(p.kind == ParamKind::Bool || p.kind == ParamKind::Text);
        }
      }
    }
  }

  void defaultsRunWithContractAndChangeTheImage() {
    const QImage source = fixture();
    for (const QString &id : kIds) {
      const auto result = run(id, source);
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.colorSpace(), source.colorSpace());
      if (kChangeSize.contains(id)) {
        QVERIFY2(result.image.size() != source.size(), qPrintable(id));
      } else {
        QCOMPARE(result.image.size(), source.size());
        QVERIFY2(result.image != source, qPrintable(id + " left the fixture unchanged"));
      }
    }
  }

  void sameInputAndSeedIsDeterministic() {
    const QImage source = fixture();
    for (const QString &id : kIds) QVERIFY2(img(id, source, {}, 7) == img(id, source, {}, 7), qPrintable(id));
  }

  void seededEffectsDependOnTheSeed() {
    const QImage source = fixture(64, 64);
    for (const QString &id : kSeeded) {
      QVERIFY2(img(id, source, {}, 1) != img(id, source, {}, 2), qPrintable(id));
      QVERIFY2(img(id, source, {}, 1) == img(id, source, {}, 1), qPrintable(id));
    }
    for (const QString &id : kIds)
      if (!kSeeded.contains(id)) QVERIFY2(img(id, source, {}, 1) == img(id, source, {}, 2), qPrintable(id + " depends on the seed"));
  }

  void cancellationReturnsEffectCancelled() {
    const QImage source = fixture();
    for (const QString &id : kIds) {
      int polls = 0;
      EffectContext context;
      context.cancelled = [&] { return ++polls > 3; };
      const auto result = run(id, source, {}, context);
      QCOMPARE(result.error, QString("effect-cancelled"));
      QVERIFY2(polls > 3, qPrintable(id));
    }
  }

  void invalidParametersAndEmptyInputAreRejected() {
    for (const QString &id : kIds) {
      QCOMPARE(run(id, QImage()).error, QString("effect-invalid-input"));
      QCOMPARE(run(id, fixture(), {{"no_such_parameter", 1}}).error, QString("effect-invalid-input"));
    }
  }

  // ---- anamorphic_lens_flare ---------------------------------------------------------
  void anamorphicLensFlareStreakIsHorizontal() {
    const QImage source = fixture();
    // Neutral: the reference returns the source unchanged when the intensity is zero.
    QCOMPARE(img("anamorphic_lens_flare", source, {{"intensity", 0}}), source);
    // A flat mid grey is below the 72% threshold, so nothing flares and the image is untouched.
    const QImage grey = solid(16, 16, qRgb(128, 128, 128));
    QCOMPARE(img("anamorphic_lens_flare", grey), grey);
    // One bright bar produces a horizontal streak in its own row only: the flare samples along
    // x, so the rows above and below stay black.
    QImage spark(24, 12, QImage::Format_ARGB32);
    spark.fill(qRgb(0, 0, 0));
    for (int x = 8; x < 16; ++x) spark.setPixel(x, 5, qRgba(255, 255, 255, 255));
    const QImage out = img("anamorphic_lens_flare", spark);
    // The flare is screened on top of the source, so the white bar itself stays white.
    QCOMPARE(px(out, 10, 5), px(spark, 10, 5));
    QVERIFY(qGray(px(out, 2, 5)) > qGray(px(spark, 2, 5)));   // the streak reaches sideways
    QCOMPARE(px(out, 2, 4), px(spark, 2, 4));                // no vertical bleed
    QCOMPARE(px(out, 2, 6), px(spark, 2, 6));
    // The default parameters brighten the bright block and leave the rest mostly alone.
    const QImage flared = img("anamorphic_lens_flare", source);
    QVERIFY(meanLum(flared) > meanLum(source));
  }

  // ---- crystalize_shards ---------------------------------------------------------------
  void crystalizeShardsQuantisesIntoFacets() {
    const QImage source = fixture();
    const QImage out = img("crystalize_shards", source);
    QVERIFY(out != source);
    // Each shard samples a single point of the source, so the output has far fewer colours
    // than the continuous gradient of the fixture.
    QVERIFY2(distinctColours(out) * 4 < distinctColours(source), qPrintable(QString::number(distinctColours(out))));
    // The seed moves the shard seeds, so the same input with another seed is another image.
    QVERIFY(img("crystalize_shards", source, {}, 1) != img("crystalize_shards", source, {}, 2));
  }

  // ---- inner_shadow ----------------------------------------------------------------------
  void innerShadowDarkensTheEdgesOnly() {
    const QImage source = solid(40, 40, qRgb(200, 200, 200));
    // Neutral: opacity zero returns the source unchanged.
    QCOMPARE(img("inner_shadow", source, {{"opacity", 0}}), source);
    const QImage out = img("inner_shadow", source);
    // The blurred alpha mask is transparent outside the picture, so the rim is shadowed and
    // the middle is untouched.
    QVERIFY(qRed(px(out, 0, 20)) < qRed(px(out, 20, 20)));
    QCOMPARE(px(out, 20, 20), px(source, 20, 20));
    // The mask is drawn at (-offsetX, -offsetY), so a positive x offset shifts the mask left and
    // the shadow lands on the right edge.
    const QImage shifted = img("inner_shadow", source, {{"offset_x", 20}});
    QVERIFY(qRed(px(shifted, 39, 20)) < qRed(px(shifted, 0, 20)));
    QVERIFY(qRed(px(shifted, 39, 20)) < qRed(px(out, 39, 20)));
    // A transparent colour disables the shadow.
    QCOMPARE(img("inner_shadow", source, {{"color", QColor(0, 0, 0, 0)}}), source);
  }

  // ---- blueprint_drawing ---------------------------------------------------------------------
  void blueprintDrawingIsBlueAndSeeded() {
    const QImage source = fixture();
    const QImage out = img("blueprint_drawing", source);
    QVERIFY(out != source);
    // Every pixel is mixed from the blueprint palette, so blue stays the dominant channel.
    for (int y = 0; y < out.height(); ++y)
      for (int x = 0; x < out.width(); ++x) {
        const QRgb c = px(out, x, y);
        QVERIFY2(qBlue(c) > qGreen(c) && qGreen(c) > qRed(c), qPrintable(QString("%1,%2").arg(x).arg(y)));
      }
    QVERIFY(meanLum(out) > meanLum(source));
    // The paper noise is seeded, so another seed gives another blueprint.
    QVERIFY(img("blueprint_drawing", source, {}, 1) != img("blueprint_drawing", source, {}, 2));
  }

  // ---- holographic_foil_shimmer ------------------------------------------------------------------
  void holographicFoilShimmerScreensWithoutDarkening() {
    const QImage source = fixture();
    // Neutral: the reference returns the source unchanged when the intensity is zero.
    QCOMPARE(img("holographic_foil_shimmer", source, {{"intensity", 0}}), source);
    const QImage out = img("holographic_foil_shimmer", source);
    QVERIFY(out != source);
    // The overlay is screened and mixed in, so no channel can get darker.
    for (int y = 0; y < out.height(); ++y)
      for (int x = 0; x < out.width(); ++x) {
        const QRgb s = px(source, x, y), o = px(out, x, y);
        QVERIFY(qRed(o) >= qRed(s) && qGreen(o) >= qGreen(s) && qBlue(o) >= qBlue(s));
      }
    QVERIFY(meanLum(out) > meanLum(source));
  }

  // ---- woodcut_print ---------------------------------------------------------------------------
  void woodcutPrintMapsToInkAndPaper() {
    // Hand computed: lum = (0.299 r + 0.587 g + 0.114 b) / 255, edge = 0 outside the Sobel
    // ring, score = lum - edge * 0.6, local threshold 0.5. A white pixel scores 1 > 0.55, so it
    // takes the paper colour; a black pixel scores 0 < 0.45, so it takes the ink colour.
    QImage tiles(3, 3, QImage::Format_ARGB32);
    tiles.setPixel(0, 0, qRgba(255, 255, 255, 255));
    tiles.setPixel(2, 2, qRgba(0, 0, 0, 255));
    tiles.setPixel(0, 2, qRgba(128, 128, 128, 255));
    const QImage out = img("woodcut_print", tiles, {{"grain_amount", 0}});
    QCOMPARE(px(out, 0, 0), qRgba(235, 225, 200, 255));
    QCOMPARE(px(out, 2, 2), qRgba(15, 10, 5, 255));
    // Mid grey: lum = 128/255 = 0.50196, so isInk = 1 - (0.50196 - 0.45) / 0.1 = 0.48039 and
    // out = ink * 0.48039 + paper * 0.51961.
    QCOMPARE(px(out, 0, 2), qRgba(129, 122, 106, 255));
    // Every output pixel is a mix of the two colours.
    const QImage cut = img("woodcut_print", fixture());
    QVERIFY(cut != fixture());
    for (int y = 0; y < cut.height(); ++y)
      for (int x = 0; x < cut.width(); ++x) {
        const QRgb c = px(cut, x, y);
        QVERIFY(qRed(c) >= 15 && qRed(c) <= 235);
        QVERIFY(qGreen(c) >= 10 && qGreen(c) <= 225);
        QVERIFY(qBlue(c) >= 5 && qBlue(c) <= 200);
      }
  }

  // ---- rainbow_border --------------------------------------------------------------------------
  void rainbowBorderPaintsAHueRing() {
    const QImage source = solid(8, 8, qRgb(30, 40, 50));
    const QImage out = img("rainbow_border", source);
    QCOMPARE(out.size(), QSize(8 + 48, 8 + 48));
    // Hand computed: at (0, 0) the perimeter position is 0, so the hue is 0 (red). The corner is
    // at the outer edge, so the cross ratio saturates at 1 and the glow scales the brightness to
    // 95 * (1 - 0.4 * 0.5) = 76. HSV(0, 100, 76) is (193, 0, 0) once truncated to bytes.
    QCOMPARE(px(out, 0, 0), qRgba(193, 0, 0, 255));
    // The picture keeps its pixels inside the ring.
    QCOMPARE(px(out, 28, 28), px(source, 4, 4));
    // The hue travels around the frame: red at the top left, yellow towards the top middle and
    // magenta down the left edge.
    const QRgb topLeft = px(out, 0, 0);
    const QRgb topMiddle = px(out, 28, 0);
    const QRgb leftMiddle = px(out, 0, 28);
    QVERIFY(qRed(topLeft) > qGreen(topLeft) && qRed(topLeft) > qBlue(topLeft));
    QVERIFY(qRed(topMiddle) > qBlue(topMiddle) && qGreen(topMiddle) > qBlue(topMiddle));
    QVERIFY(qRed(leftMiddle) > qGreen(leftMiddle) && qBlue(leftMiddle) > qGreen(leftMiddle));
    // The border can be switched off to a plain frame.
    const QImage noLine = img("rainbow_border", source, {{"inner_line", false}});
    QCOMPARE(px(noLine, 0, 0), topLeft);
  }

  // ---- minecraft_grass --------------------------------------------------------------------------
  void minecraftGrassDrawsBlocks() {
    const QImage source = solid(40, 32, qRgb(10, 20, 30));
    const QImage out = img("minecraft_grass", source);
    // 6 block rows of 16 px, minus one block of overlap: the picture grows by 80 rows.
    QCOMPARE(out.size(), QSize(40, 32 + 80));
    const int borderY = 32 - 16;
    // Hand computed: Hash01(0, 0, 137) = 0.05628, so rVar = int(0.05628 * 20 - 10) = -8 and
    // gVar = int(0.05628 * 30 - 15) = -13; the grass colour is (89, 176, 48) varied by those.
    QCOMPARE(px(out, 0, borderY), qRgba(81, 163, 40, 255));
    // Two rows down the border turns to dirt: Hash01(0, 2, 137) = 0.98054, so dVar = 11 and the
    // dirt colour is (134, 96, 67) varied by 11 (and 15 for blue).
    QCOMPARE(px(out, 0, borderY + 32), qRgba(145, 107, 82, 255));
    // The jagged edge skips the top grass row of every second column: Hash01(1, 0, 42) = 0.99912
    // is above 0.5, so column 1 keeps the picture under its grass row.
    QCOMPARE(px(out, 16, borderY), px(source, 16, borderY));
    // The border can sit on the top edge instead: the blocks are then drawn from the bottom of
    // the border upwards, so the last row (row 5) lands on y = 0. Its colour comes from
    // Hash01(0, 5, 137) = 0.51524, which varies the grass colour by 0.
    const QImage top = img("minecraft_grass", source, {{"top_edge", true}});
    QCOMPARE(top.size(), QSize(40, 32 + 80));
    QCOMPARE(px(top, 0, 0), qRgba(89, 176, 48, 255));
    // Column 1 skips its jagged top row, so that block stays transparent.
    QCOMPARE(qAlpha(px(top, 16, 0)), 0);
    // The picture starts under the border and is only covered where the blocks overlap it.
    QCOMPARE(px(top, 0, 96), px(source, 0, 16));
  }

  // ---- crt ----------------------------------------------------------------------------------------
  void crtAppliesScanlinesAndChannelShift() {
    const QImage source = solid(1, 1, qRgb(200, 100, 50));
    // Neutral: no scanline, no shift, no noise and no vignette returns the source unchanged.
    QCOMPARE(img("crt", source, {{"scanlineStrength", 0}, {"rgbShift", 0}, {"noiseAmount", 0},
                                 {"vignetteStrength", 0}}),
             source);
    // Hand computed: at x = 0 the shadow mask is (1.06, 0.97, 0.93); an even row darkens by
    // 0.62 and an odd row by 0.18, so the brightness is 0.38 and 0.82.
    QImage pair(1, 2, QImage::Format_ARGB32);
    pair.setPixel(0, 0, qRgba(200, 100, 50, 255));
    pair.setPixel(0, 1, qRgba(200, 100, 50, 255));
    const QImage out = img("crt", pair, {{"scanlineStrength", 100}, {"rgbShift", 0}, {"noiseAmount", 0},
                                          {"vignetteStrength", 0}});
    QCOMPARE(px(out, 0, 0), qRgba(81, 37, 18, 255));
    QCOMPARE(px(out, 0, 1), qRgba(174, 80, 38, 255));
    // The red channel comes from the pixel on the right and the blue channel from the left, both
    // through the shadow mask: at x = 1 the mask is (0.93, 1.06, 0.97), so red is
    // round(80 * 0.93) = 74 and blue is round(50 * 0.97) = 48.
    QVector<QRgb> pixels;
    for (int x = 0; x < 5; ++x) pixels << qRgb(x * 40, 100, 50);
    const QImage shifted = img("crt", row(pixels), {{"scanlineStrength", 0}, {"rgbShift", 1}, {"noiseAmount", 0},
                                                      {"vignetteStrength", 0}});
    QCOMPARE(qRed(px(shifted, 1, 0)), 74);
    QCOMPARE(qBlue(px(shifted, 1, 0)), 48);
    // At x = 3 the mask is (1.06, 0.97, 0.93): blue comes from x = 2 and is round(50 * 0.93) = 46.
    QCOMPARE(qBlue(px(shifted, 3, 0)), 46);
  }

  // ---- plasma_energy_arcs -------------------------------------------------------------------------
  void plasmaEnergyArcsAddsGlow() {
    const QImage source = fixture();
    // Neutral: the reference returns the source unchanged when the energy is zero.
    QCOMPARE(img("plasma_energy_arcs", source, {{"energy", 0}}), source);
    const QImage out = img("plasma_energy_arcs", source);
    QVERIFY(out != source);
    // The arcs are screened in, so no channel can get darker.
    for (int y = 0; y < out.height(); ++y)
      for (int x = 0; x < out.width(); ++x) {
        const QRgb s = px(source, x, y), o = px(out, x, y);
        QVERIFY(qRed(o) >= qRed(s) && qGreen(o) >= qGreen(s) && qBlue(o) >= qBlue(s));
      }
    // The brightest arc cores are pushed towards white.
    int brightest = 0;
    for (int y = 0; y < out.height(); ++y)
      for (int x = 0; x < out.width(); ++x) brightest = std::max(brightest, int(qGray(px(out, x, y))));
    QVERIFY(brightest > 200);
  }
};
QTEST_GUILESS_MAIN(FilterEffects4Test)
#include "FilterEffects4Test.moc"
