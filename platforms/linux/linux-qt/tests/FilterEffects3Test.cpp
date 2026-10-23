// EDITOR-EFFECTS-001: EE-003, EE-008..EE-010, EE-016, EE-018 (Filters, batch 3).
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QGuiApplication>
#include <QTest>
#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kIds{
    "crosshatch",
    "surface_blur",
    "rgb_split",
    "dot_matrix",
    "edge_feather",
    "reflection",
    "spin_blur",
    "x_ray_scan",
    "double_exposure",
    "glare",
    "anaglyph_3d",
    "star_filter",
    "thermal_vision",
    "soft_diffusion",
    "cyanotype",
    "night_vision",
    "hologram_scan",
    "pointillism",
    "chromatic_aberration",
    "paper_stencil_mask",
    "infrared_false_color",
    "anime_speed_lines",
    "neon_glow_border",
    "film_strip_frame",
    "gradient_fade_border",
    "engraving",
    "vhs_tape_damage",
    "wet_plate_collodion",
    "smoke_overlay",
    "block_glitch",
    "polaroid_frame",
    "embroidery",
    "wave_edge",
    "crystal_prism",
    "luminance_contour_lines",
    "disposable_camera",
    "lens_blur",
    "chalk_drawing",
    "zigzag_border",
    "mosaic_tile_border",
};
// Effects whose default parameters may leave the fixture unchanged (none expected; kept explicit).
const QStringList kMayLeaveFixtureUnchanged{};
const QStringList kSeeded{
    "cyanotype",
    "hologram_scan",
    "pointillism",
    "paper_stencil_mask",
    "anime_speed_lines",
    "wet_plate_collodion",
    "block_glitch",
    "crystal_prism",
    "disposable_camera",
};
const QStringList kChangeSize{
    "reflection",
    "neon_glow_border",
    "film_strip_frame",
    "gradient_fade_border",
    "polaroid_frame",
    "zigzag_border",
    "mosaic_tile_border",
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
bool near(QRgb a, QRgb b, int tolerance) {
  return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance &&
         std::abs(qBlue(a) - qBlue(b)) <= tolerance && std::abs(qAlpha(a) - qAlpha(b)) <= tolerance;
}
double meanLum(const QImage &image) {
  double sum = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) sum += qGray(image.pixel(x, y));
  return sum / (double(image.width()) * image.height());
}
double variance(const QImage &image) {
  const double mean = meanLum(image);
  double sum = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) sum += std::pow(qGray(image.pixel(x, y)) - mean, 2);
  return sum / (double(image.width()) * image.height());
}
}

class FilterEffects3Test : public QObject {
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
        QVERIFY2(result.image != source || kMayLeaveFixtureUnchanged.contains(id), qPrintable(id + " left the fixture unchanged"));
      }
    }
  }

  void sameInputAndSeedIsDeterministic() {
    const QImage source = fixture();
    for (const QString &id : kIds) QVERIFY2(img(id, source, {}, 7) == img(id, source, {}, 7), qPrintable(id));
  }

  void seededEffectsDependOnTheSeed() {
    const QImage source = fixture();
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

  // ---- batch 1 ----
  void crosshatchHandComputed() {
    // Black pixel at (0,0): lum 0 -> contrast gives <0 -> 0, darkness 1; all four layers hit lines at 0 -> ink 1 -> 0.
    QCOMPARE(px(img("crosshatch", solid(1, 1, qRgb(0, 0, 0))), 0, 0), qRgba(0, 0, 0, 255));
    // White pixel: darkness 0 -> no lines -> value = lum = 1 -> 255.
    QCOMPARE(px(img("crosshatch", solid(1, 1, qRgb(255, 255, 255))), 0, 0), qRgba(255, 255, 255, 255));
    // Grey 128 at (3,3): lum=0.50196; contrast 1.2 -> 0.50235; darkness .49765 passes thresholds .14/.28/.42 (layers 1-3);
    // only the (x-y) diagonal (|0|) is on a line -> ink = 1/4 -> value = .50235*(1-.25*.94) = .38429 -> 98.
    QImage grey = solid(8, 8, qRgb(128, 128, 128));
    QCOMPARE(px(img("crosshatch", grey), 3, 3), qRgba(98, 98, 98, 255));
    // Alpha is carried through.
    QCOMPARE(qAlpha(px(img("crosshatch", solid(1, 1, qRgba(10, 10, 10, 77))), 0, 0)), 77);
  }

  void surfaceBlurPreservesEdgesAndFlatAreas() {
    QCOMPARE(img("surface_blur", solid(9, 9, qRgb(50, 60, 70))), solid(9, 9, qRgb(50, 60, 70)));
    // Two flat halves 200 apart per channel: delta 600 > threshold sum (24*3) -> each side stays exactly flat.
    QImage halves(10, 4, QImage::Format_ARGB32);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 10; ++x) halves.setPixel(x, y, x < 5 ? qRgb(0, 0, 0) : qRgb(200, 200, 200));
    QCOMPARE(img("surface_blur", halves), halves);
    // A small step (delta 3*10=30 <= 72) is blended: the pixel left of the step moves up, the right one down.
    QImage soft(10, 1, QImage::Format_ARGB32);
    for (int x = 0; x < 10; ++x) soft.setPixel(x, 0, x < 5 ? qRgb(100, 100, 100) : qRgb(110, 110, 110));
    const QImage out = img("surface_blur", soft);
    QVERIFY(qRed(px(out, 4, 0)) > 100);
    QVERIFY(qRed(px(out, 5, 0)) < 110);
  }

  void rgbSplitHandComputed() {
    // Reference: R from x-offset_red_x, G from x-offset_green_x, B from x-offset_blue_x, each clamped; opaque -> channel * 255/255.
    QVector<QRgb> pixels;
    for (int x = 0; x < 11; ++x) pixels << qRgb(x * 10, 100 + x, 200 - x);
    const QImage source = row(pixels);
    const QImage out = img("rgb_split", source, {{"offset_red_x", -2}, {"offset_green_x", 0}, {"offset_blue_x", 1}});
    // x=5: R = px[7].r = 70, G = px[5].g = 105, B = px[4].b = 196.
    QCOMPARE(px(out, 5, 0), qRgba(70, 105, 196, 255));
    // x=0: B clamps to px[0].b = 200; x=10: R clamps to px[10].r = 100.
    QCOMPARE(qBlue(px(out, 0, 0)), 200);
    QCOMPARE(qRed(px(out, 10, 0)), 100);
    // Neutral: all offsets 0.
    const ParamValues zero{{"offset_red_x", 0}, {"offset_blue_x", 0}};
    QCOMPARE(img("rgb_split", source, zero), source);
    // Alpha: R and alpha channels use integer maths: r*a/255 with a=128, r=255 -> 128; alpha average of the three taps.
    const QImage half = img("rgb_split", solid(1, 1, qRgba(255, 255, 255, 128)), zero);
    QCOMPARE(px(half, 0, 0), qRgba(128, 128, 128, 128));
  }

  void dotMatrixPaintsDotsOnPaper() {
    const QColor paper(250, 245, 235), ink(20, 20, 20);
    QCOMPARE(px(img("dot_matrix", solid(16, 16, qRgb(255, 255, 255))), 4, 4), paper.rgba());  // white -> radius 0 -> bare paper
    const QImage black = img("dot_matrix", solid(16, 16, qRgb(0, 0, 0)), {{"dot_scale", 1.0}});
    QCOMPARE(px(black, 4, 4), ink.rgba());          // cell centre inside the dot (r = 4)
    QCOMPARE(px(black, 0, 0), paper.rgba());        // cell corner (distance 5.7 from centre) is outside
    const QImage colour = img("dot_matrix", solid(16, 16, qRgb(0, 0, 200)), {{"color_mode", true}, {"dot_scale", 1.0}});
    QCOMPARE(px(colour, 4, 4), qRgba(0, 0, 200, 255));
  }

  void edgeFeatherSoftensTheAlphaEdgeOnly() {
    QImage opaque = solid(40, 40, qRgb(10, 20, 30));
    QCOMPARE(img("edge_feather", opaque, {{"radius", 0.0}}), opaque);
    const QImage out = img("edge_feather", opaque, {{"radius", 6.0}});
    QCOMPARE(qAlpha(px(out, 20, 20)), 255);       // interior stays solid
    QVERIFY(qAlpha(px(out, 0, 20)) < 200);        // border fades
    QVERIFY(qAlpha(px(out, 0, 0)) < qAlpha(px(out, 0, 20)));  // corner fades more than an edge
    QCOMPARE(px(out, 20, 20) & 0x00FFFFFF, opaque.pixel(20, 20) & 0x00FFFFFF);
    // Fully transparent input stays fully transparent.
    QCOMPARE(qAlpha(px(img("edge_feather", solid(8, 8, qRgba(5, 5, 5, 0))), 4, 4)), 0);
  }

  void reflectionAppendsAFadingMirror() {
    const QImage source = fixture(20, 20);
    const QImage out = img("reflection", source, {{"percentage", 50}, {"offset", 4}});
    QCOMPARE(out.size(), QSize(20, 20 + 4 + 10));
    for (int x = 0; x < 20; ++x) QCOMPARE(px(out, x, 5), px(source, x, 5));  // original untouched
    QCOMPARE(qAlpha(px(out, 3, 21)), 0);                                        // gap rows transparent
    // First mirrored row = source row 19, alpha = 255 * (1 - 0.5/10) = 242.25 -> 242; last row alpha = 255*(1-9.5/10) = 12.75 -> 13.
    const QRgb first = px(out, 3, 24), last = px(out, 3, 33);
    QCOMPARE(first & 0x00FFFFFF, px(source, 3, 19) & 0x00FFFFFF);
    QCOMPARE(qAlpha(first), 242);
    QCOMPARE(last & 0x00FFFFFF, px(source, 3, 10) & 0x00FFFFFF);
    QCOMPARE(qAlpha(last), 13);
    // Skew widens the canvas by reflection height * skew_size/100 = 10 * 0.5 = 5.
    QCOMPARE(img("reflection", source, {{"percentage", 50}, {"skew", true}, {"skew_size", 50}}).width(), 25);
  }

  void spinBlurLeavesCentreAndFlatAreasAlone() {
    const QImage source = fixture();
    QCOMPARE(img("spin_blur", source, {{"angle", 0.0}}), source);
    QCOMPARE(img("spin_blur", solid(21, 21, qRgb(9, 99, 199))), solid(21, 21, qRgb(9, 99, 199)));
    // Centre (50% of 0..w-1 = 19.5, 15.5): pixels within 0.5 of it are untouched; a far edge pixel changes.
    const QImage out = img("spin_blur", source, {{"angle", 90.0}});
    QVERIFY(px(out, 0, 0) != px(source, 0, 0));
    QCOMPARE(qAlpha(px(out, 0, 0)), 255);
  }

  void xRayScanHandComputed() {
    // Flat white, noise 0: lum 1 -> density 0, edge 0, energy 0, glow 0, grain 0 -> black; alpha kept.
    const QImage out = img("x_ray_scan", solid(5, 5, qRgba(255, 255, 255, 200)), {{"noise", 0.0}});
    QCOMPARE(px(out, 2, 2), qRgba(0, 0, 0, 200));
    // Flat black, no scanlines or glow, contrast 100: density = 1^(1.85-1.25) = 1, energy = .48 -> r=.0672 g=.3456 b=.48 -> (17,88,122).
    const QImage dark = img("x_ray_scan", solid(5, 5, qRgb(0, 0, 0)), {{"noise", 0.0}, {"scanlines", 0.0}, {"contrast", 100.0}, {"glow", 0.0}});
    QCOMPARE(px(dark, 2, 2), qRgba(17, 88, 122, 255));
  }

  void doubleExposureNeutralAndBlend() {
    const QImage source = fixture();
    QCOMPARE(img("double_exposure", source, {{"blend_amount", 0.0}, {"offset_x", 0}, {"offset_y", 0}, {"ghost_blur", 0.0}}), source);
    QCOMPARE(img("double_exposure", source, {{"blend_amount", 0.0}}), source);  // no blend -> source colours exactly
    // Screen blending can only brighten: no channel of the default result is darker than the source.
    const QImage out = img("double_exposure", source);
    for (int y = 0; y < source.height(); ++y)
      for (int x = 0; x < source.width(); ++x) {
        QVERIFY(qRed(px(out, x, y)) >= qRed(px(source, x, y)));
        QVERIFY(qGreen(px(out, x, y)) >= qGreen(px(source, x, y)));
        QVERIFY(qBlue(px(out, x, y)) >= qBlue(px(source, x, y)));
      }
  }

  // ---- batch 2 ----
  void glareBrightensNearTheCentreOnly() {
    const QImage dark = solid(60, 40, qRgb(0, 0, 0));
    // Radius 30% of the diagonal/2 -> 10.8 px; centre (30, 12). Screen over black = the glow colour itself: alpha 204 at the
    // centre -> channel ~204 (a little less 0.7 px off-centre); far corner untouched.
    const QImage out = img("glare", dark, {{"ray_count", 0}});
    QVERIFY(qRed(px(out, 30, 12)) >= 175 && qRed(px(out, 30, 12)) <= 204);  // pixel centre is 0.7 px off the centre of a 10.8 px radius: 204*(1-0.065/0.4*0.6) = 184
    QCOMPARE(px(out, 59, 39), qRgba(0, 0, 0, 255));
    QCOMPARE(img("glare", dark, {{"intensity", 0.0}}), dark);
    // Rays reach beyond the glow radius along the ray angle (ray 0 points +x from the centre).
    const QImage rays = img("glare", dark, {{"ray_count", 1}, {"ray_length", 100.0}, {"radius", 20.0}});
    const QImage noRays = img("glare", dark, {{"ray_count", 0}, {"radius", 20.0}});
    QVERIFY(qRed(px(rays, 30 + 18, 12)) > qRed(px(noRays, 30 + 18, 12)));
    // Glow colour is honoured.
    const QImage red = img("glare", dark, {{"ray_count", 0}, {"color", QColor(255, 0, 0)}});
    QCOMPARE(qGreen(px(red, 30, 12)), 0);
    QVERIFY(qRed(px(red, 30, 12)) > 150);
  }

  void anaglyphChannelRouting() {
    // Row of distinct pixels, separation 2 -> halfX 1: left = x-1, right = x+1; ghost 0 keeps channels unmixed.
    QVector<QRgb> pixels;
    for (int x = 0; x < 9; ++x) pixels << qRgb(10 * x, 100 + 5 * x, 200 - 10 * x);
    const QImage source = row(pixels);
    const ParamValues base{{"separation_x", 2}, {"ghost_reduction", 0.0}};
    // red_cyan: R from left (x-1), G and B from right (x+1). x=4: R=30, G=125, B=200-50=150.
    QCOMPARE(px(img("anaglyph_3d", source, base), 4, 0), qRgba(30, 125, 150, 255));
    // amber_blue: R,G from left, B from right -> (30, 115, 150).
    ParamValues amber = base;
    amber["mode"] = "amber_blue";
    QCOMPARE(px(img("anaglyph_3d", source, amber), 4, 0), qRgba(30, 115, 150, 255));
    // green_magenta: R,B from right, G from left -> (50, 115, 150).
    ParamValues gm = base;
    gm["mode"] = "green_magenta";
    QCOMPARE(px(img("anaglyph_3d", source, gm), 4, 0), qRgba(50, 115, 150, 255));
    // No separation: identity.
    QCOMPARE(img("anaglyph_3d", source, {{"separation_x", 0}, {"separation_y", 0}}), source);
  }

  void starFilterAddsStreaksAroundBrightPoints() {
    QImage dark = solid(41, 41, qRgb(0, 0, 0));
    dark.setPixel(20, 20, qRgb(255, 255, 255));
    const QImage out = img("star_filter", dark, {{"rotation", 0.0}});
    QVERIFY(qRed(px(out, 24, 20)) > 0);   // along the horizontal arm
    QVERIFY(qRed(px(out, 20, 24)) > 0);   // along the vertical arm
    QCOMPARE(px(out, 33, 7), qRgba(0, 0, 0, 255));  // off-axis, away from every arm
    QCOMPARE(img("star_filter", dark, {{"strength", 0.0}}), dark);
    // Flat mid-grey below the threshold: no streaks, image unchanged.
    QCOMPARE(img("star_filter", solid(9, 9, qRgb(100, 100, 100))), solid(9, 9, qRgb(100, 100, 100)));
  }

  void thermalVisionMapsLuminanceToPalette() {
    // Pure palette (blend 100, glow 0, contrast 100, levels 3). Black: lum 0 -> t 0 -> (0,0,52). White: t 1 -> (255,255,255).
    const ParamValues p{{"contrast", 100.0}, {"glow", 0.0}, {"levels", 3}};
    QCOMPARE(px(img("thermal_vision", solid(1, 1, qRgb(0, 0, 0)), p), 0, 0), qRgba(0, 0, 52, 255));
    QCOMPARE(px(img("thermal_vision", solid(1, 1, qRgb(255, 255, 255)), p), 0, 0), qRgba(255, 255, 255, 255));
    // Mid-grey 128: lum .50196 -> round(.50196*2)/2 = 0.5 -> between (0,160,255) and (0,255,210) at (0.5-.4)/.2 = .5 -> (0,208,232).
    QCOMPARE(px(img("thermal_vision", solid(1, 1, qRgb(128, 128, 128)), p), 0, 0), qRgba(0, 208, 232, 255));
    // Invert flips black and white.
    ParamValues inv = p;
    inv["invert"] = true;
    QCOMPARE(px(img("thermal_vision", solid(1, 1, qRgb(255, 255, 255)), inv), 0, 0), qRgba(0, 0, 52, 255));
    // Blend 0 -> source unchanged.
    QCOMPARE(img("thermal_vision", fixture(), {{"blend", 0.0}}), fixture());
  }

  void softDiffusionNeutralAndBrightening() {
    const QImage source = fixture();
    QCOMPARE(img("soft_diffusion", source, {{"amount", 0.0}}), source);
    QCOMPARE(img("soft_diffusion", solid(8, 8, qRgb(0, 0, 0)), {{"warmth", 0.0}, {"contrast_softening", 0.0}}),
             solid(8, 8, qRgb(0, 0, 0)));  // black: screen with a black blur adds nothing
    // Diffusion blurs edges: the dark block's neighbour gets brighter than in the source on at least one of its pixels.
    const QImage out = img("soft_diffusion", source);
    QVERIFY(out != source);
    QCOMPARE(qAlpha(px(out, 3, 3)), 255);
  }

  void cyanotypeIsBlueMonochrome() {
    const QImage out = img("cyanotype", fixture());
    // Tones run paper (241,236,220) to deep blue (11,45,92): red never exceeds blue-ish dominance in the dark half.
    const QImage flatBlack = img("cyanotype", solid(16, 16, qRgb(0, 0, 0)), {{"paper_texture", 0.0}, {"stain", 0.0}, {"grain", 0.0}, {"vignette", 0.0}});
    QCOMPARE(px(flatBlack, 8, 8), qRgba(11, 45, 92, 255));  // ink 1 -> lerp(mid, deep, 1)
    const QImage flatWhite = img("cyanotype", solid(16, 16, qRgb(255, 255, 255)), {{"paper_texture", 0.0}, {"stain", 0.0}, {"grain", 0.0}, {"vignette", 0.0}});
    QCOMPARE(px(flatWhite, 8, 8), qRgba(241, 236, 220, 255));  // ink 0 -> paper
    QVERIFY(meanLum(out) > 0);
    QCOMPARE(qAlpha(px(out, 2, 2)), 255);
  }

  void nightVisionIsGreenPhosphor() {
    const QImage out = img("night_vision", fixture(), {{"noise", 0.0}});
    for (int y = 0; y < out.height(); y += 5)
      for (int x = 0; x < out.width(); x += 5) QVERIFY(qGreen(px(out, x, y)) >= qRed(px(out, x, y)) && qGreen(px(out, x, y)) >= qBlue(px(out, x, y)));
    QCOMPARE(img("night_vision", fixture(), {{"intensity", 0.0}}), fixture());
    // Flat black with no noise is black.
    QCOMPARE(px(img("night_vision", solid(8, 8, qRgb(0, 0, 0)), {{"noise", 0.0}, {"glow", 0.0}}), 4, 4), qRgba(0, 0, 0, 255));
  }

  // ---- batch 3 ----
  void hologramScanProperties() {
    const QImage source = fixture();
    QCOMPARE(img("hologram_scan", source, {{"scanline_strength", 0.0}, {"glitch_amount", 0.0}, {"chroma_shift", 0}, {"glow_amount", 0.0}}), source);
    // Flat black, no glitch/glow/chroma: output is black everywhere (all terms scale the zero colour); alpha kept.
    const QImage black = img("hologram_scan", solid(12, 12, qRgba(0, 0, 0, 90)), {{"glitch_amount", 0.0}, {"glow_amount", 0.0}});
    QCOMPARE(px(black, 5, 5), qRgba(0, 0, 90, 90) & 0xFF000000);
    // Scanlines modulate rows: the mean luminance of a flat grey image drops as the strength grows.
    const QImage grey = solid(24, 24, qRgb(160, 160, 160));
    QVERIFY(meanLum(img("hologram_scan", grey, {{"scanline_strength", 90.0}, {"glitch_amount", 0.0}, {"glow_amount", 0.0}, {"chroma_shift", 0}})) <
            meanLum(img("hologram_scan", grey, {{"scanline_strength", 5.0}, {"glitch_amount", 0.0}, {"glow_amount", 0.0}, {"chroma_shift", 0}})));
    // The hologram is bluer than red: blue channel beats red on grey.
    const QImage out = img("hologram_scan", grey, {{"glitch_amount", 0.0}, {"scanline_strength", 0.0}, {"chroma_shift", 0}});
    QVERIFY(qBlue(px(out, 5, 5)) > qRed(px(out, 5, 5)));
  }

  void pointillismDotsAndBackground() {
    const QImage source = fixture(60, 60);
    // Background mix 0 and density 10 (min): most of the canvas stays transparent.
    const QImage sparse = img("pointillism", source, {{"background_mix", 0.0}, {"density", 10.0}});
    int transparent = 0;
    for (int y = 0; y < sparse.height(); ++y)
      for (int x = 0; x < sparse.width(); ++x) transparent += qAlpha(px(sparse, x, y)) == 0;
    QVERIFY(transparent > sparse.width() * sparse.height() / 2);
    // Full background mix puts the source under the dots: no transparent pixels remain.
    const QImage full = img("pointillism", source, {{"background_mix", 100.0}});
    for (int y = 0; y < full.height(); y += 7)
      for (int x = 0; x < full.width(); x += 7) QCOMPARE(qAlpha(px(full, x, y)), 255);
    // Dot colours come from the source: a flat colour image yields only that colour where alpha is full.
    const QImage flat = img("pointillism", solid(30, 30, qRgb(40, 80, 120)), {{"color_boost", 0.0}, {"background_mix", 100.0}});
    for (int y = 0; y < 30; ++y)
      for (int x = 0; x < 30; ++x) QVERIFY(near(px(flat, x, y), qRgba(40, 80, 120, 255), 2));
  }

  void chromaticAberrationLeavesTheCentreAlone() {
    const QImage source = fixture();
    QCOMPARE(img("chromatic_aberration", source, {{"amount", 0.0}}), source);
    QCOMPARE(img("chromatic_aberration", source, {{"strength", 0.0}}), source);
    const QImage out = img("chromatic_aberration", source);
    // The central pixel lies inside edgeStart (0.20): unchanged. A corner moves.
    QCOMPARE(px(out, 20, 16), px(source, 20, 16));
    QVERIFY(px(out, 0, 0) != px(source, 0, 0) || px(out, 39, 0) != px(source, 39, 0) || px(out, 39, 31) != px(source, 39, 31));
    QCOMPARE(img("chromatic_aberration", solid(20, 20, qRgb(90, 10, 200))), solid(20, 20, qRgb(90, 10, 200)));  // flat stays flat
  }

  void paperStencilMaskHandComputed() {
    // Pure black pixel, feather 0 -> range 0.02, threshold 140/255. lum 0 < t0 -> maskAlpha 0 -> output = src * (1-dim).
    // Mid-grey source 100: still below the band -> (100 * 0.65) = 65.
    QCOMPARE(px(img("paper_stencil_mask", solid(3, 3, qRgb(100, 100, 100)), {{"feather_radius", 0.0}}), 1, 1), qRgba(65, 65, 65, 255));
    // White pixel: lum 1 above the band -> maskAlpha 1 -> mix = alpha(220)/255 * 0.7 = 0.60392; out = 1*0.65 -> lerp(.65, 0, .60392) = .25745 -> 66.
    QCOMPARE(px(img("paper_stencil_mask", solid(3, 3, qRgb(255, 255, 255)), {{"feather_radius", 0.0}}), 1, 1), qRgba(66, 66, 66, 255));
    // Inverting the mask swaps the two cases.
    QCOMPARE(px(img("paper_stencil_mask", solid(3, 3, qRgb(255, 255, 255)), {{"feather_radius", 0.0}, {"invert_mask", true}}), 1, 1), qRgba(166, 166, 166, 255));
    // Neutral: no edge strength and no dim.
    QCOMPARE(img("paper_stencil_mask", fixture(), {{"edge_strength", 0.0}, {"background_dim", 0.0}}), fixture());
  }

  void infraredFalseColorShiftsFoliageToRed() {
    const QImage green = solid(16, 16, qRgb(40, 160, 50));
    const QImage out = img("infrared_false_color", green, {{"glow", 0.0}});
    QVERIFY(qRed(px(out, 8, 8)) > qGreen(px(out, 8, 8)));  // foliage hue pulled toward red/pink
    // Sky (blue) gets darker.
    const QImage sky = solid(16, 16, qRgb(90, 150, 230));
    QVERIFY(meanLum(img("infrared_false_color", sky, {{"glow", 0.0}, {"contrast", 100.0}})) < meanLum(sky));
    // Intensity 0 -> identity. Greys (no hue) keep neutral channels at contrast 100 with intensity small.
    QCOMPARE(img("infrared_false_color", fixture(), {{"intensity", 0.0}}), fixture());
    const QImage grey = img("infrared_false_color", solid(4, 4, qRgb(120, 120, 120)), {{"glow", 0.0}, {"contrast", 100.0}});
    QCOMPARE(qRed(px(grey, 1, 1)), qBlue(px(grey, 1, 1)));
  }

  void animeSpeedLinesRadialStructure() {
    const QImage source = fixture(80, 60);
    QCOMPARE(img("anime_speed_lines", source, {{"strength", 0.0}}), source);
    const QImage out = img("anime_speed_lines", source, {{"focus_radius", 40.0}});
    // Inside the focus circle only the desaturation applies (no lines): a grey pixel with equal channels stays equal.
    QImage grey = solid(80, 60, qRgb(120, 120, 120));
    const QImage g = img("anime_speed_lines", grey, {{"focus_radius", 40.0}});
    QCOMPARE(px(g, 39, 29), qRgba(120, 120, 120, 255));
    // Outside the focus circle the lines add variance to a flat grey image.
    QVERIFY(variance(g) > 1.0);
    QVERIFY(out != source);
  }

  // ---- batch 4 ----
  void neonGlowBorderGrowsTheCanvas() {
    const QImage source = fixture(30, 20);
    const QImage out = img("neon_glow_border", source, {{"border_size", 20}});
    QCOMPARE(out.size(), QSize(70, 60));
    // The glow bleeds over the picture edge, as in the reference, but the middle of a larger picture is untouched.
    const QImage big = fixture(100, 80);
    const QImage wide = img("neon_glow_border", big, {{"border_size", 20}});
    QCOMPARE(px(wide, 20 + 50, 20 + 40), px(big, 50, 40));
    QVERIFY(qGreen(px(out, 0, 0)) < 60 && qAlpha(px(out, 0, 0)) == 255);                          // corner far from the tube: near the background (a faint glow reaches it)
    // The tube centre line (border/2 = 10 px in) carries the near-white core + neon colour: much brighter than the background.
    QVERIFY(qGreen(px(out, 35, 10)) > 200);
    // Double tube adds a second line at border*0.78 = 15.6 px.
    const QImage dbl = img("neon_glow_border", source, {{"border_size", 20}, {"double_tube", true}});
    QVERIFY(qGreen(px(dbl, 35, 15)) > qGreen(px(out, 35, 15)));
    // No glow reach into the image area is checked by the source-untouched loop above only where the glow is far; the
    // glow itself is strongest on the tube: green there exceeds green one border-width away.
    QVERIFY(qGreen(px(out, 35, 10)) > qGreen(px(out, 35, 2)));
  }

  void filmStripFramePunchesSprocketHoles() {
    const QImage source = fixture(100, 60);
    const ParamValues params{{"border_size", 40}, {"scratches", false}};
    const QImage out = img("film_strip_frame", source, params);
    QCOMPARE(out.size(), QSize(180, 140));
    // Frame colour in the band between holes; the picture in the centre.
    QCOMPARE(px(out, 3, 20), qRgba(15, 15, 15, 255));
    QCOMPARE(px(out, 90, 70), px(source, 90 - 40, 70 - 40));
    // Hole height = 40*.55 = 22, step = 38.5, first hole at x=15.4..37.4, y = 9..31: its centre is transparent.
    QCOMPARE(qAlpha(px(out, 26, 20)), 0);
    QCOMPARE(qAlpha(px(out, 26, 120)), 0);  // bottom band hole
    // Side strips off: left band has no holes; on: it does.
    QCOMPARE(qAlpha(px(out, 20, 70)), 255);
    const QImage sides = img("film_strip_frame", source, {{"border_size", 40}, {"scratches", false}, {"side_strips", true}});
    bool anyHole = false;
    for (int y = 40; y < 100; ++y) anyHole = anyHole || qAlpha(px(sides, 20, y)) == 0;
    QVERIFY(anyHole);
    // Scratches change only a few columns of the frame band.
    QVERIFY(img("film_strip_frame", source, {{"border_size", 40}, {"scratches", true}}) != out);
  }

  void gradientFadeBorderColoursByDistance() {
    const QImage source = fixture(20, 20);
    const QImage out = img("gradient_fade_border", source, {{"border_size", 20}, {"glow_edge", false}});
    QCOMPARE(out.size(), QSize(60, 60));
    // Hand computed on the left band, row 30: x=0 -> dist 20 -> t=1 -> outer colour; x=19 -> dist 1 -> t=.05 -> seg=.1,
    // smoothstep(.1)=0.028 -> lerp(inner, mid, .028): r = 180 + (60-180)*.028 = 176.6 -> 177.
    QCOMPARE(px(out, 0, 30), qRgba(10, 10, 35, 255));
    QCOMPARE(qRed(px(out, 19, 30)), 177);
    // Mid position: at t == mid (x=10 -> dist 10 -> t .5) the colour is the mid colour.
    QCOMPARE(px(out, 10, 30), qRgba(60, 20, 120, 255));
    QCOMPARE(px(out, 25, 25), px(source, 5, 5));   // interior = source
    // The glow edge brightens pixels on the image rim.
    const QImage glow = img("gradient_fade_border", source, {{"border_size", 20}, {"glow_edge", true}});
    QVERIFY(glow != out);
    QCOMPARE(px(glow, 0, 0), px(out, 0, 0));
  }

  void engravingDrawsInkOnPaper() {
    const QImage white = img("engraving", solid(40, 40, qRgb(255, 255, 255)));
    QCOMPARE(px(white, 20, 20), qRgba(245, 240, 230, 255));   // white -> no line -> bare paper everywhere
    QCOMPARE(variance(white), 0.0);
    const QImage black = img("engraving", solid(40, 40, qRgb(0, 0, 0)), {{"line_width_max", 3.0}, {"line_spacing", 6.0}});
    QVERIFY(meanLum(black) < qGray(245, 240, 230));            // ink reduces the mean
    QVERIFY(variance(black) > 100.0);                          // lines and gaps
    // Darker source -> more ink.
    QVERIFY(meanLum(img("engraving", solid(40, 40, qRgb(60, 60, 60)))) > meanLum(img("engraving", solid(40, 40, qRgb(0, 0, 0)))));
  }

  void vhsTapeDamageNeutralAndNoise() {
    const QImage source = fixture();
    const ParamValues zero{{"distortion", 0.0}, {"noise", 0.0}, {"color_bleed", 0.0}, {"tracking", 0.0}, {"scanlines", 0.0}};
    QCOMPARE(img("vhs_tape_damage", source, zero), source);
    // Noise only on a flat image: pixels vary around the base value; with noise 0 and the rest zero it is flat.
    QImage flat = solid(40, 32, qRgb(100, 100, 100));
    QVERIFY(variance(img("vhs_tape_damage", flat, {{"distortion", 0.0}, {"noise", 80.0}, {"color_bleed", 0.0}, {"tracking", 0.0}, {"scanlines", 0.0}})) > 5.0);
    // Scanlines only: row y=0 has factor 1 - .18*s*(0.5+0.5*sin(0)) = 1 - .09*s; s=1 -> .91 -> 100*.91 = 91; row 1 (sin(pi) ~ 0) the same.
    const QImage scan = img("vhs_tape_damage", flat, {{"distortion", 0.0}, {"noise", 0.0}, {"color_bleed", 0.0}, {"tracking", 0.0}, {"scanlines", 100.0}});
    QCOMPARE(qRed(px(scan, 3, 0)), 91);
    QCOMPARE(qAlpha(px(scan, 3, 0)), 255);
  }

  void wetPlateCollodionToneRange() {
    const ParamValues clean{{"plate_stains", 0.0}, {"scratches", 0.0}, {"silvering", 0.0}, {"vignette", 0.0}};
    // Flat black: lum 0 -> shadow tone (20,16,18). Flat white: lum 1 -> highlight (220,213,202).
    QCOMPARE(px(img("wet_plate_collodion", solid(9, 9, qRgb(0, 0, 0)), clean), 4, 4), qRgba(20, 16, 18, 255));
    QCOMPARE(px(img("wet_plate_collodion", solid(9, 9, qRgb(255, 255, 255)), clean), 4, 4), qRgba(220, 213, 202, 255));
    // Vignette darkens the corner relative to the centre of a flat white image.
    const QImage vig = img("wet_plate_collodion", solid(41, 41, qRgb(255, 255, 255)), {{"plate_stains", 0.0}, {"scratches", 0.0}, {"silvering", 0.0}, {"vignette", 100.0}});
    QVERIFY(qRed(px(vig, 0, 0)) < qRed(px(vig, 20, 20)));
  }

  // ---- batch 5 ----
  void smokeOverlayLightensTowardGrey() {
    const QImage source = fixture();
    QCOMPARE(img("smoke_overlay", source, {{"density", 0.0}}), source);
    // Smoke colours are light greys (0.82..0.96): on a black image no channel can exceed 0.96*255 = 245 and pixels only brighten.
    const QImage black = solid(48, 48, qRgb(0, 0, 0));
    const QImage out = img("smoke_overlay", black, {{"density", 100.0}});
    bool brightened = false;
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 48; ++x) {
        QVERIFY(qRed(px(out, x, y)) <= 246);
        brightened = brightened || qRed(px(out, x, y)) > 20;
      }
    QVERIFY(brightened);
    QVERIFY(variance(out) > 1.0);  // patchy, not uniform
    QCOMPARE(qAlpha(px(out, 3, 3)), 255);
  }

  void blockGlitchOnlyTouchesBlocks() {
    const QImage source = fixture(80, 60);
    // One block, no displacement, no channel shift, no noise: every pixel is copied from itself -> identical image.
    QCOMPARE(img("block_glitch", source, {{"block_count", 1}, {"max_displacement", 0}, {"channel_shift", 0}, {"noise_amount", 0.0}}, 5), source);
    const QImage out = img("block_glitch", source, {}, 5);
    int changed = 0;
    for (int y = 0; y < 60; ++y)
      for (int x = 0; x < 80; ++x) changed += px(out, x, y) != px(source, x, y);
    QVERIFY(changed > 0 && changed < 80 * 60);
    // With one small block (min=max=4x2) at most 8 pixels change.
    const QImage tiny = img("block_glitch", source, {{"block_count", 1}, {"min_block_width", 4}, {"max_block_width", 4}, {"min_block_height", 2}, {"max_block_height", 2}}, 9);
    int tinyChanged = 0;
    for (int y = 0; y < 60; ++y)
      for (int x = 0; x < 80; ++x) tinyChanged += px(tiny, x, y) != px(source, x, y);
    QVERIFY(tinyChanged <= 8);
  }

  void polaroidFrameGeometry() {
    const QImage source = fixture(40, 30);
    // No rotation, no shadow: canvas = (40+60+20, 30+30+80+20) = 120 x 160; the polaroid is centred: x offset 10, y offset 10.
    const QImage out = img("polaroid_frame", source, {{"shadow_size", 0}});
    QCOMPARE(out.size(), QSize(120, 160));
    QCOMPARE(px(out, 0, 0), qRgba(0, 0, 0, 0));                              // outside the print: transparent
    QCOMPARE(px(out, 10 + 30 + 20, 10 + 30 + 15), px(source, 20, 15));         // photo at (border, border)
    QCOMPARE(px(out, 10 + 15, 10 + 5), qRgba(250, 249, 245, 255));            // frame colour in the top border
    // Shadow adds margin and a translucent halo outside the print.
    const QImage shadow = img("polaroid_frame", source, {{"shadow_size", 10}});
    QCOMPARE(shadow.size(), QSize(140, 180));
    QVERIFY(qAlpha(px(shadow, 20 - 4, 20 + 30)) > 0 && qAlpha(px(shadow, 20 - 4, 20 + 30)) < 100);
    // Rotation grows the canvas.
    QVERIFY(img("polaroid_frame", source, {{"shadow_size", 0}, {"rotation", 20.0}}).width() > out.width());
    // A caption draws dark text in the bottom border.
    const QImage captioned = img("polaroid_frame", source, {{"shadow_size", 0}, {"caption", "Hello"}});
    QVERIFY(captioned != out);
  }

  void embroideryStitchesOnFabric() {
    const QColor fabric(240, 235, 220);
    // Flat black stitched cells, size 10, grid off: the cell centre lies on the X crossing -> stitch colour black.
    const QImage out = img("embroidery", solid(20, 20, qRgb(0, 0, 0)), {{"show_grid", false}});
    QCOMPARE(px(out, 5, 5), qRgba(0, 0, 0, 255));
    // Row 0, column 5 is clear of both diagonals (they start 1 px in at x=1 and x=9): bare fabric.
    QCOMPARE(px(out, 5, 0), fabric.rgba());
    // A pure red cell quantises to a level of the 3-per-channel palette (levels = ceil(16^(1/3)) = 3 -> 0,128,255).
    const QImage red = img("embroidery", solid(20, 20, qRgb(250, 3, 0)), {{"show_grid", false}});
    QCOMPARE(px(red, 5, 5), qRgba(255, 0, 0, 255));
    // The grid is drawn semi-transparently on top.
    const QImage grid = img("embroidery", solid(20, 20, qRgb(255, 255, 255)), {{"show_grid", true}});
    QVERIFY(px(grid, 10, 4) != px(out, 10, 4));
    QCOMPARE(qAlpha(px(grid, 4, 4)), 255);
  }

  void waveEdgeCutsTheBorderIntoWaves() {
    const QImage source = fixture(100, 80);
    // All four edges: corners are transparent, the centre is the source.
    const QImage out = img("wave_edge", source);
    QCOMPARE(qAlpha(px(out, 0, 0)), 0);
    QCOMPARE(px(out, 50, 40), px(source, 50, 40));
    QVERIFY(qAlpha(px(out, 99, 79)) == 0);
    // No edge selected: identity.
    QCOMPARE(img("wave_edge", source, {{"top", false}, {"right", false}, {"bottom", false}, {"left", false}}), source);
    // Only the top edge: the left column below the cut stays opaque, the top row varies in coverage along its length.
    const QImage topOnly = img("wave_edge", source, {{"right", false}, {"bottom", false}, {"left", false}});
    QCOMPARE(qAlpha(px(topOnly, 0, 79)), 255);
    int transparentTop = 0, opaqueTop = 0;
    for (int x = 0; x < 100; ++x) {
      transparentTop += qAlpha(px(topOnly, x, 0)) == 0;
      opaqueTop += qAlpha(px(topOnly, x, 0)) > 200;
    }
    QVERIFY(transparentTop > 0 && opaqueTop > 0);
  }

  // ---- batch 6 ----
  void crystalPrismFacets() {
    const QImage source = fixture(64, 64);
    QCOMPARE(img("crystal_prism", source, {{"refraction", 0.0}, {"dispersion", 0.0}, {"sparkle", 0.0}}), source);
    // Flat colour, no sparkle: every sample is the same colour; the facet shade (0.88 +- 0.15) then blends 58% with the source,
    // so output stays within [0.58*c*0.73 + 0.42*c .. 0.58*c*1.03 + 0.42*c] of the flat value 100 -> 85..102.
    const QImage flat = img("crystal_prism", solid(48, 48, qRgb(100, 100, 100)), {{"sparkle", 0.0}, {"refraction", 0.0}, {"dispersion", 4.0}});
    for (int y = 0; y < 48; y += 3)
      for (int x = 0; x < 48; x += 3) QVERIFY(qRed(px(flat, x, y)) >= 84 && qRed(px(flat, x, y)) <= 103);
    // Sparkle brightens facet borders more than facet centres on a flat image.
    const QImage spark = img("crystal_prism", solid(48, 48, qRgb(100, 100, 100)), {{"sparkle", 100.0}, {"refraction", 0.0}, {"dispersion", 0.0}});
    int borderSum = 0, centreSum = 0;
    for (int c = 0; c < 2; ++c) {
      borderSum += qRed(px(spark, c * 24, 12));   // x%24 == 0: on a facet border
      centreSum += qRed(px(spark, c * 24 + 12, 12));
    }
    QVERIFY(borderSum > centreSum);
  }

  void luminanceContourLinesHandComputed() {
    // 4 levels, line_width 0 -> feather 0.04, no threshold bias, background 100%, line strength 100%, black lines.
    const ParamValues p{{"levels", 4}, {"line_width", 0.0}, {"line_strength", 100.0}, {"background_strength", 100.0}};
    // Bucket centre: lum 0.125 (32/255=.1255): frac .502 -> distToBoundary .498 -> lineMask 0 -> qLum = .125 -> 32 (31.9).
    QCOMPARE(px(img("luminance_contour_lines", solid(1, 1, qRgb(32, 32, 32)), p), 0, 0), qRgba(32, 32, 32, 255));
    // Exactly on a boundary: lum 0.5 gives frac 0 -> dist 0 -> lineMask 1 -> black line. Use 127.5? Use 255: lum 1 -> scaled 4 -> frac 0 -> line.
    QCOMPARE(px(img("luminance_contour_lines", solid(1, 1, qRgb(255, 255, 255)), p), 0, 0), qRgba(0, 0, 0, 255));
    // Invert flips line and non-line.
    ParamValues inv = p;
    inv["invert"] = true;
    QCOMPARE(px(img("luminance_contour_lines", solid(1, 1, qRgb(255, 255, 255)), inv), 0, 0), qRgba(255, 255, 255, 255));
    // qLum for 255 with bucket index 4 is (4.5/4 -> clamp 1) = 1 -> white.
    QCOMPARE(img("luminance_contour_lines", fixture(), {{"line_strength", 0.0}, {"background_strength", 0.0}}), fixture());
  }

  void disposableCameraProperties() {
    const QImage source = fixture();
    QCOMPARE(img("disposable_camera", source, {{"flash", 0.0}, {"softness", 0.0}, {"grain", 0.0}, {"vignette", 0.0}, {"warmth", 0.0}}), source);
    // Vignette only on a flat image: the corner is darker than the centre.
    const QImage vig = img("disposable_camera", solid(41, 41, qRgb(128, 128, 128)), {{"flash", 0.0}, {"softness", 0.0}, {"grain", 0.0}, {"vignette", 100.0}, {"warmth", 0.0}});
    QVERIFY(qRed(px(vig, 0, 0)) < qRed(px(vig, 20, 20)));
    // Grain only: variance on a flat image.
    QVERIFY(variance(img("disposable_camera", solid(41, 41, qRgb(128, 128, 128)), {{"flash", 0.0}, {"softness", 0.0}, {"grain", 100.0}, {"vignette", 0.0}, {"warmth", 0.0}})) > 2.0);
    // Warmth pushes red above blue on a grey image.
    const QImage warm = img("disposable_camera", solid(21, 21, qRgb(128, 128, 128)), {{"grain", 0.0}, {"vignette", 0.0}});
    QVERIFY(qRed(px(warm, 10, 10)) > qBlue(px(warm, 10, 10)));
  }

  void lensBlurAveragesADisc() {
    // Radius 2 disc: centre w=1; 4 taps at distance 1 (w = 1 - 0.35*1/2 = .825); 4 at sqrt2 (.75259); 4 at 2 (.65); sum = 9.91036.
    QImage dot = solid(5, 5, qRgb(0, 0, 0));
    dot.setPixel(2, 2, qRgb(200, 200, 200));
    const ParamValues p{{"radius", 2}, {"highlight_boost", 0.0}};
    const QImage out = img("lens_blur", dot, p);
    QCOMPARE(qRed(px(out, 2, 2)), 20);   // 200 * 1 / 9.91036 = 20.18
    QCOMPARE(qRed(px(out, 1, 2)), 17);   // the dot is 1 away: 200 * .825 / 9.91036 = 16.65
    QCOMPARE(qRed(px(out, 0, 0)), 0);    // distance sqrt(8) > 2: untouched
    // The reference shortcut: radius 1 with no highlight boost is a plain copy.
    QCOMPARE(img("lens_blur", dot, {{"radius", 1}, {"highlight_boost", 0.0}}), dot);
    // Highlight boost raises the weight of bright taps: the neighbour of the dot gets brighter than without it.
    QVERIFY(qRed(px(img("lens_blur", dot, {{"radius", 2}, {"highlight_boost", 200.0}, {"highlight_threshold", 10.0}}), 1, 2)) > 17);
  }

  // ---- batch 7 ----
  void chalkDrawingHandComputed() {
    const QColor board(30, 50, 40);
    // Flat black: lum 0 -> chalk intensity 0 (dark mask) -> board colour everywhere.
    QCOMPARE(px(img("chalk_drawing", solid(8, 8, qRgb(0, 0, 0))), 4, 4), board.rgba());
    // Flat white, roughness 0, detail 0: edge = 0, lumWeight = 1 - 0.3*0.5 = 0.85, intensity = 0.85 * sqrt(1) = .85.
    // red = 30/255 + (1 - 30/255) * .85 = .8678 -> 221; green = 50/255 + (205/255)*.85 = .8794 -> 224; blue = 40/255 + (215/255)*.85 = .8735 -> 223.
    const QImage white = img("chalk_drawing", solid(8, 8, qRgb(255, 255, 255)), {{"detail", 0.0}, {"roughness", 0.0}});
    QCOMPARE(px(white, 4, 4), qRgba(221, 224, 223, 255));
    // Roughness only darkens (texture factor <= 1): mean luminance falls.
    const QImage rough = img("chalk_drawing", solid(40, 40, qRgb(255, 255, 255)), {{"detail", 0.0}, {"roughness", 100.0}});
    QVERIFY(meanLum(rough) < meanLum(white));
    QVERIFY(variance(rough) > 1.0);   // the noise texture shows
    // Edges are chalked brighter than the flat dark side next to them.
    QImage edge(20, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 20; ++x) edge.setPixel(x, y, x < 10 ? qRgb(40, 40, 40) : qRgb(40, 40, 40));
    for (int y = 0; y < 8; ++y) edge.setPixel(10, y, qRgb(255, 255, 255));
    const QImage out = img("chalk_drawing", edge, {{"roughness", 0.0}});
    QVERIFY(qRed(px(out, 9, 4)) > qRed(px(out, 3, 4)));
  }

  void zigzagBorderClipsThePicture() {
    const QImage source = fixture(100, 80);
    const QImage out = img("zigzag_border", source, {{"accent_stripe", false}});
    QCOMPARE(out.size(), QSize(172, 152));
    QCOMPARE(px(out, 2, 2), qRgba(40, 40, 50, 255));                   // frame colour
    QCOMPARE(px(out, 36 + 50, 36 + 40), px(source, 50, 40));           // deep inside: the picture
    // Teeth point into the picture: tips at x = 36 + 12 + 24k, feet at x = 36 + 24k, tooth height 40.
    // Above the third tip (x = 96) the V is outside the zigzag, so the pixel just below the top edge is frame colour;
    // under the foot at x = 84 (4 px down, the V edges are ~1.2 px either side) the solid between teeth shows the picture.
    QCOMPARE(px(out, 96, 38), qRgba(40, 40, 50, 255));
    QCOMPARE(px(out, 84, 40), px(source, 84 - 36, 40 - 36));
    // The accent stripe paints accent-coloured pixels along the border line.
    const QImage striped = img("zigzag_border", source);
    int accent = 0;
    for (int y = 0; y < striped.height(); ++y)
      for (int x = 0; x < striped.width(); ++x) accent += near(px(striped, x, y), qRgba(220, 180, 60, 255), 6);
    QVERIFY(accent > 100);
  }

  void mosaicTileBorderGroutAndTiles() {
    const QImage source = fixture(60, 40);
    const QImage out = img("mosaic_tile_border", source, {{"border_size", 24}, {"tile_size", 12.0}, {"grout_width", 2.0}});
    QCOMPARE(out.size(), QSize(108, 88));
    const QRgb grout = qRgba(210, 205, 195, 255);
    QCOMPARE(px(out, 0, 5), grout);          // x % 12 = 0 < 1: grout
    QCOMPARE(px(out, 5, 12), grout);         // y % 12 = 0: grout
    QCOMPARE(qAlpha(px(out, 6, 6)), 255);    // tile interior, opaque, not grout colour
    QVERIFY(px(out, 6, 6) != grout);
    // The inner grout ring: 2 px wide just outside the picture (x = 22, 23 at row 40).
    QCOMPARE(px(out, 22, 40), grout);
    QCOMPARE(px(out, 23, 40), grout);
    QCOMPARE(px(out, 24 + 10, 24 + 10), px(source, 10, 10));
    // Colour variety 0: all hues are red (hue 0): R >= G and R >= B in the tile interior.
    const QImage red = img("mosaic_tile_border", source, {{"border_size", 24}, {"color_variety", 0.0}});
    const QRgb c = px(red, 6, 6);
    QVERIFY(qRed(c) >= qGreen(c) && qRed(c) >= qBlue(c));
  }

};

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  FilterEffects3Test test;
  return QTest::qExec(&test, argc, argv);
}
#include "FilterEffects3Test.moc"
