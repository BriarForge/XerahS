// EDITOR-EFFECTS-001: EE-003, EE-008..EE-010, EE-016, EE-018 (Filters, batch 5).
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QGuiApplication>
#include <QTest>
#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kIds{
    "rope_border", "heat_haze_refraction", "old_camera_flash_burn", "datamosh_smear", "tilt_shift", "oil_slick_interference",
    "frosted_glass_ice_edges", "liquid_mercury", "marble_border", "pencil_sketch", "bloom", "etched_glass", "halftone",
    "chain_link_border", "claymation_texture", "macos_window", "torn_edge", "ascii_art",
    "watercolor_border", "halation", "brick_wall_border", "voronoi_mosaic",
    "riso_print", "starfield_border", "crimson_border", "rust_corrosion", "lava_molten_border", "rainy_window",
};
// Effects whose default parameters may leave the fixture unchanged (none expected; kept explicit).
const QStringList kMayLeaveFixtureUnchanged{};
const QStringList kSeeded{
    "heat_haze_refraction", "datamosh_smear", "frosted_glass_ice_edges", "claymation_texture", "torn_edge", "riso_print", "rainy_window",
};
const QStringList kChangeSize{
    "rope_border", "marble_border", "chain_link_border", "macos_window", "watercolor_border", "brick_wall_border",
    "starfield_border", "crimson_border", "lava_molten_border",
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

class FilterEffects5Test : public QObject {
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

  // ---- specific tests ----
  // ---- ropeBorder / marble ----
  void ropeBorderFramesTheImage() {
    const QImage source = fixture(20, 16);
    const QImage out = img("rope_border", source, {{"border_size", 12}, {"fiber_detail", 0.0}});
    QCOMPARE(out.size(), QSize(20 + 24, 16 + 24));
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 20; ++x) QCOMPARE(px(out, x + 12, y + 12), px(source, x, y));  // interior is the source
    // Corner (0,0): across = 0 - 6 = -6, strand reach is at most 2.27 + 1.9, so it is background.
    QCOMPARE(px(out, 0, 0), QColor(60, 45, 30).rgba());
    // (0,6) is on the centre line at along 0: strand 0 has offset sin(0)=0, shade = .35 + cos(0)*.08 = .43 (fiber off);
    // r = (180/255+.43)*255 -> 255 (clamped), g = 145 + .43*.8*255 = 232.7 -> 233, b = 90 + .43*.5*255 = 144.8 -> 145.
    QVERIFY(near(px(out, 0, 6), qRgba(255, 233, 145, 255), 1));
    // Fiber detail adds speckle to the rope, so some rope pixel differs between fiber 0 and fiber 100.
    QVERIFY(img("rope_border", source, {{"border_size", 12}, {"fiber_detail", 100.0}}) != out);
  }

  void marbleBorderFramesTheImageWithAHairline() {
    const QImage source = fixture(20, 16);
    const QImage out = img("marble_border", source, {{"border_size", 10}});
    QCOMPARE(out.size(), QSize(40, 36));
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 20; ++x) QCOMPARE(px(out, x + 10, y + 10), px(source, x, y));
    for (int x = 0; x < 40; ++x) QCOMPARE(qAlpha(px(out, x, 0)), 255);
    // The 1.5 px hairline (vein colour shaded by -0.3) fully covers the pixel ring just outside the picture: (90,80,70) - 76.5 -> (14,4,0).
    QVERIFY(near(px(out, 9, 18), qRgba(14, 4, 0, 255), 1));
    // Vein strength 0 and no bevel: the marble is a function of the diagonal only, so (x+y) equal pixels agree.
    const QImage plain = img("marble_border", source, {{"border_size", 10}, {"vein_strength", 0.0}, {"inner_bevel", false}});
    QCOMPARE(px(plain, 2, 5), px(plain, 5, 2));
    QVERIFY(img("marble_border", source, {{"border_size", 10}, {"inner_bevel", false}}) != img("marble_border", source, {{"border_size", 10}}));
  }

  // ---- photographic / analog ----
  void heatHazeNeutralAndFlat() {
    const QImage source = fixture();
    QCOMPARE(img("heat_haze_refraction", source, {{"strength", 0.0}}), source);
    // A flat image warps onto itself when there is no blur.
    const QImage flat = solid(16, 16, qRgb(40, 90, 160));
    QCOMPARE(img("heat_haze_refraction", flat, {{"blur_radius", 0.0}}), flat);
    // Alpha is carried through.
    QCOMPARE(qAlpha(px(img("heat_haze_refraction", solid(4, 4, qRgba(9, 9, 9, 77))), 1, 1)), 77);
  }

  void oldCameraFlashBurnHandComputed() {
    const ParamValues flashOnly{{"edge_burn", 0.0}, {"grain", 0.0}};
    // Neutral: no flash, no burn, no grain.
    const QImage source = fixture();
    QCOMPARE(img("old_camera_flash_burn", source, {{"flash_strength", 0.0}, {"edge_burn", 0.0}, {"grain", 0.0}}), source);
    // 3x3 black, centre (1,1) is the flash centre: flash = .7, shoulder = .7^.72 = .77352;
    // r = 255*.77352 = 197.2, g = (242+3.5)*.77352*.94 = 178.5, b = (218-2.8)*.77352*.82 = 136.5.
    const QImage out = img("old_camera_flash_burn", solid(3, 3, qRgb(0, 0, 0)), flashOnly);
    QVERIFY(near(px(out, 1, 1), qRgba(197, 179, 136, 255), 1));
    QVERIFY(qRed(px(out, 0, 0)) < qRed(px(out, 1, 1)));  // flash falls off with distance
  }

  void datamoshHorizontalSmearIgnoresAxisConstantImages() {
    // Rows that are constant along x cannot show a horizontal shift (including the channel split).
    QImage rows(24, 12, QImage::Format_ARGB32);
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 24; ++x) rows.setPixel(x, y, qRgb(y * 20, 255 - y * 15, y * 7));
    QCOMPARE(img("datamosh_smear", rows, {{"direction", "horizontal"}}, 5), rows);
    QVERIFY(img("datamosh_smear", rows, {{"direction", "vertical"}}, 5) != rows);
    QImage columns(24, 12, QImage::Format_ARGB32);
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 24; ++x) columns.setPixel(x, y, qRgb(x * 10, 255 - x * 9, x * 4));
    QCOMPARE(img("datamosh_smear", columns, {{"direction", "vertical"}}, 5), columns);
    // Neutral: nothing to smear, corrupt or drift.
    const QImage source = fixture();
    QCOMPARE(img("datamosh_smear", source, {{"smear_amount", 0.0}, {"corruption", 0.0}, {"drift", 0.0}}), source);
  }

  void tiltShiftKeepsTheFocusBandSharp() {
    const QImage source = fixture(40, 40);
    // Saturation 0 and no blur: identity.
    QCOMPARE(img("tilt_shift", source, {{"blur_radius", 0.0}, {"saturation_boost", 0.0}}), source);
    // Linear: the centre row sits inside the focus band (mask 0), so with no saturation boost it keeps the source.
    const QImage out = img("tilt_shift", source, {{"saturation_boost", 0.0}});
    for (int x = 0; x < 40; ++x) QCOMPARE(px(out, x, 20), px(source, x, 20));
    QVERIFY(px(out, 20, 0) != px(source, 20, 0));  // outside the band it is blurred
    // Saturation: (200,100,0) in the focus: gray = 100, factor 1.35 -> r = 235, g = 100, b = -35 -> 0.
    QCOMPARE(px(img("tilt_shift", solid(1, 1, qRgb(200, 100, 0)), {{"blur_radius", 0.0}}), 0, 0), qRgba(235, 100, 0, 255));
    // Radial mode blurs the corners and keeps the centre.
    const QImage radial = img("tilt_shift", source, {{"mode", "radial"}, {"saturation_boost", 0.0}});
    QCOMPARE(px(radial, 20, 20), px(source, 20, 20));
    QVERIFY(px(radial, 0, 0) != px(source, 0, 0));
  }

  void oilSlickIsNeutralAtZeroIntensityAndOnlyBrightensWithoutDarkness() {
    const QImage source = fixture();
    QCOMPARE(img("oil_slick_interference", source, {{"intensity", 0.0}}), source);
    const QImage out = img("oil_slick_interference", source, {{"darkness", 0.0}});
    QVERIFY(out != source);
    for (int y = 0; y < source.height(); ++y)
      for (int x = 0; x < source.width(); ++x) {  // screen blend never darkens
        QVERIFY(qRed(px(out, x, y)) >= qRed(px(source, x, y)));
        QVERIFY(qGreen(px(out, x, y)) >= qGreen(px(source, x, y)));
        QVERIFY(qBlue(px(out, x, y)) >= qBlue(px(source, x, y)));
      }
  }

  void frostedGlassNeutralAndDistortion() {
    const QImage source = fixture();
    const ParamValues neutral{{"distortion", 0.0}, {"blur", 0.0}, {"ice_strength", 0.0}, {"tint", 0.0}};
    QCOMPARE(img("frosted_glass_ice_edges", source, neutral), source);
    // Tint alone cools the picture: blue gains more than red.
    const QImage cool = img("frosted_glass_ice_edges", solid(8, 8, qRgb(100, 100, 100)),
                            {{"distortion", 0.0}, {"blur", 0.0}, {"ice_strength", 0.0}, {"tint", 100.0}});
    // coolMix = .45: r = 100 + .45*(98-100) = 99.1 -> 99; g = 100 + .45*(119-100) = 108.6 -> 109; b = 100 + .45*(108+56-100) = 128.8 -> 129.
    QVERIFY(near(px(cool, 3, 3), qRgba(99, 109, 129, 255), 2));
    QVERIFY(qBlue(px(cool, 3, 3)) > qRed(px(cool, 3, 3)));
  }

  void liquidMercuryIsSilverWithAPositiveCoolBias() {
    const QImage source = fixture();
    const QImage out = img("liquid_mercury", source);
    QVERIFY(out != source);
    for (int y = 0; y < source.height(); ++y)
      for (int x = 0; x < source.width(); ++x) {
        const QRgb c = px(out, x, y);
        QVERIFY(qBlue(c) >= qGreen(c) && qGreen(c) >= qRed(c));  // red = s - cs, green = s, blue = s + 1.25cs
        QCOMPARE(qAlpha(c), 255);
      }
    QCOMPARE(qAlpha(px(img("liquid_mercury", solid(4, 4, qRgba(1, 2, 3, 99))), 1, 1)), 99);
  }

  // ---- stylisation ----
  void pencilSketchHandComputed() {
    // Flat grey: dodge = g / max(1-(1-g), .03) = 1 and no edges -> pure paper (white), alpha kept.
    QCOMPARE(px(img("pencil_sketch", solid(8, 8, qRgba(128, 128, 128, 200))), 4, 4), qRgba(255, 255, 255, 200));
    // Flat black: gray 0 -> dodge 0 -> value 0 -> black.
    QCOMPARE(px(img("pencil_sketch", solid(8, 8, qRgb(0, 0, 0))), 4, 4), qRgba(0, 0, 0, 255));
    // Edges draw graphite: a hard edge is darker than a flat area, and the output is always grey.
    QImage edge(16, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 16; ++x) edge.setPixel(x, y, x < 8 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    const QImage out = img("pencil_sketch", edge);
    QVERIFY(qRed(px(out, 7, 4)) < qRed(px(out, 2, 4)));
    for (int x = 0; x < 16; ++x) QVERIFY(qRed(px(out, x, 4)) == qGreen(px(out, x, 4)) && qGreen(px(out, x, 4)) == qBlue(px(out, x, 4)));
    // No edge strength and no darkening gamma effect on a flat image keeps it white.
    QCOMPARE(px(img("pencil_sketch", solid(8, 8, qRgb(90, 90, 90)), {{"edge_strength", 0.0}}), 3, 3), qRgba(255, 255, 255, 255));
  }

  void bloomGlowsAroundBrightAreasOnly() {
    const QImage source = fixture(40, 32);
    QCOMPARE(img("bloom", source, {{"intensity", 0.0}}), source);
    QImage halves(40, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 40; ++x) halves.setPixel(x, y, x < 20 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    const QImage out = img("bloom", halves, {{"radius", 6.0}});
    QVERIFY(qRed(px(out, 21, 4)) > 10);                              // glow spills onto the dark side
    QVERIFY(qRed(px(out, 21, 4)) > qRed(px(out, 38, 4)));            // and fades with distance
    QCOMPARE(px(out, 2, 4), qRgba(255, 255, 255, 255));              // screen of white stays white
    QCOMPARE(img("bloom", solid(6, 6, qRgb(0, 0, 0))), solid(6, 6, qRgb(0, 0, 0)));  // nothing bright, nothing to bloom
  }

  void etchedGlassNeutralAndFrost() {
    const QImage source = fixture();
    const ParamValues neutral{{"frost", 0.0}, {"engrave", 0.0}, {"highlight", 0.0}, {"background_fade", 0.0}};
    QCOMPARE(img("etched_glass", source, neutral), source);
    // Full frost on flat white: lerp(1, gray+.07, .58) with gray = .88+.07: stays at/above 1 -> white.
    QCOMPARE(px(img("etched_glass", solid(8, 8, qRgb(255, 255, 255)), {{"frost", 100.0}, {"engrave", 0.0}, {"highlight", 0.0}}), 4, 4),
             qRgba(255, 255, 255, 255));
    QVERIFY(img("etched_glass", source) != source);
  }

  void halftoneHandComputed() {
    // White paper: no ink; source blend lerps white towards white.
    QCOMPARE(px(img("halftone", solid(16, 16, qRgb(255, 255, 255))), 4, 4), qRgba(255, 255, 255, 255));
    // Black, angle -45 (the black screen is then axis aligned): the cell centre (4,4) has fract = .5 -> distance 0 -> full dot.
    // ink alpha .9*1.1 = .99 -> .01 white, then source blend .2 towards black: .008*255 = 2.04 -> 2.
    const ParamValues aligned{{"angle_offset", -45.0}};
    const QImage black = img("halftone", solid(16, 16, qRgb(0, 0, 0)), aligned);
    QCOMPARE(px(black, 4, 4), qRgba(2, 2, 2, 255));
    // The cell corner (0,0): distance .707 > radius .5 + softness -> bare paper blended 20% to black: .8*255 = 204.
    QCOMPARE(px(black, 0, 0), qRgba(204, 204, 204, 255));
    QVERIFY(img("halftone", fixture()) != fixture());
  }
  // ---- batch 2: frames, texture, text ----
  void chainLinkBorderMeshesTheFrame() {
    const QImage source = fixture(60, 40);
    const QImage out = img("chain_link_border", source);
    QCOMPARE(out.size(), QSize(60 + 104, 40 + 104));
    for (int y = 0; y < 40; ++y)
      for (int x = 0; x < 60; ++x) QCOMPARE(px(out, x + 52, y + 52), px(source, x, y));
    // (20,20) is a diamond centre in the top band: pixel centre is ~1 from it, wires are ~10 away -> bare background.
    QCOMPARE(px(out, 20, 20), QColor(35, 40, 48).rgba());
    // The wire is drawn: the band is not a single colour, and the wire colour appears on the diamond edge (30,20).
    QVERIFY(qRed(px(out, 30, 20)) > 120);
    QVERIFY(img("chain_link_border", source, {{"highlight", false}}) != out);
  }

  void claymationQuantisesFlatColourWithoutRelief() {
    // chunk 8 -> 12 levels, step 255/11 = 23.18; smoothness 0, relief 0, grain 0, saturation 0 leave only the quantiser:
    // 100 -> round(100/23.18)*23.18 = 4*23.18 = 92.7 -> 93.
    const ParamValues plain{{"smoothness", 0.0}, {"relief", 0.0}, {"textureGrain", 0.0}, {"saturation", 0.0}};
    const QImage out = img("claymation_texture", solid(12, 12, qRgb(100, 100, 100)), plain, 3);
    QCOMPARE(px(out, 5, 5), qRgba(93, 93, 93, 255));
    // The reference's smoothing kernel weights sum to 11 but divide by 10, so full smoothing brightens by 1.1 * .9:
    // lerp(100, 110, .9) = 109 -> round(109/23.18)*23.18 = 5*23.18 = 115.9 -> 116.
    QCOMPARE(px(img("claymation_texture", solid(12, 12, qRgb(100, 100, 100)), {{"smoothness", 100.0}, {"relief", 0.0}, {"textureGrain", 0.0}, {"saturation", 0.0}}), 5, 5),
             qRgba(116, 116, 116, 255));
    // Saturation -100 turns colour to grey.
    const QImage grey = img("claymation_texture", fixture(), {{"saturation", -100.0}});
    const QRgb c = px(grey, 7, 9);
    QVERIFY(std::abs(qRed(c) - qGreen(c)) <= 1 && std::abs(qGreen(c) - qBlue(c)) <= 1);
    // The relief texture is seeded and varies the picture.
    QVERIFY(img("claymation_texture", solid(12, 12, qRgb(100, 100, 100)), {}, 3) != img("claymation_texture", solid(12, 12, qRgb(100, 100, 100)), {}, 4));
  }

  void macosWindowComposesTitleBarAndShadow() {
    const QImage source = fixture(60, 40);
    const QImage out = img("macos_window", source, {{"padding", 10}, {"shadow_size", 12}});
    QCOMPARE(out.size(), QSize(60 + 44, 40 + 38 + 44));
    const int o = 22;  // padding + shadow
    for (int y = 4; y < 36; ++y)
      for (int x = 4; x < 56; ++x) QCOMPARE(px(out, o + x, o + 38 + y), px(source, x, y));
    QCOMPARE(px(out, o + 40, o + 5), qRgba(232, 232, 232, 255));  // light title bar
    QCOMPARE(qAlpha(px(out, 0, 0)), 0);                            // outside the shadow
    QVERIFY(qAlpha(px(out, o + 30, o + 38 + 40 + 4)) > 0);         // shadow just below the window
    // Dark mode.
    QCOMPARE(px(img("macos_window", source, {{"padding", 0}, {"shadow_size", 0}, {"dark_mode", true}}), 40, 5), qRgba(56, 55, 57, 255));
    // The close button is the red traffic light at (14+6, 19).
    QVERIFY(near(px(img("macos_window", source, {{"padding", 0}, {"shadow_size", 0}}), 20, 19), qRgba(255, 95, 87, 255), 2));
    // A title draws text.
    QVERIFY(img("macos_window", source, {{"title", "Hello"}}) != out);
    // No padding, no shadow, no rounding: exact size.
    QCOMPARE(img("macos_window", source, {{"padding", 0}, {"shadow_size", 0}, {"corner_radius", 0}}).size(), QSize(60, 78));
  }

  void tornEdgeCutsRaggedEdgesAndKeepsTheMiddle() {
    const QImage source = fixture(80, 80);
    const ParamValues params{{"depth", 10}, {"range", 10}};
    const QImage out = img("torn_edge", source, params, 9);
    QCOMPARE(px(out, 40, 40), px(source, 40, 40));
    int transparent = 0;
    for (int y = 0; y < 80; ++y)
      for (int x = 0; x < 80; ++x)
        if (qAlpha(px(out, x, y)) == 0) ++transparent;
    QVERIFY(transparent > 20);
    QVERIFY(transparent < 80 * 80 / 4);
    QVERIFY(img("torn_edge", source, params, 9) != img("torn_edge", source, params, 10));
    QVERIFY(img("torn_edge", source, {{"depth", 10}, {"range", 10}, {"curved", true}}, 9) != out);
    // Neutral: every side off, or too small for two tears in either direction.
    QCOMPARE(img("torn_edge", source, {{"top", false}, {"right", false}, {"bottom", false}, {"left", false}}), source);
    QCOMPARE(img("torn_edge", source, {{"range", 100}}), source);
    // Only the top torn: the other three edges stay straight and opaque.
    const QImage topOnly = img("torn_edge", source, {{"depth", 10}, {"range", 10}, {"right", false}, {"bottom", false}, {"left", false}}, 9);
    for (int x = 0; x < 80; ++x) QCOMPARE(qAlpha(px(topOnly, x, 79)), 255);
    for (int y = 0; y < 80; ++y) QCOMPARE(qAlpha(px(topOnly, 0, y)) > 0 || y < 10, true);
  }

  void asciiArtDrawsGlyphsOnABackground() {
    const QRgb dark = qRgb(12, 12, 12);
    // White cells map to the blank character -> pure background; the background flips with dark_background.
    QCOMPARE(img("ascii_art", solid(16, 16, qRgb(255, 255, 255))), solid(16, 16, dark | 0xFF000000));
    QCOMPARE(img("ascii_art", solid(16, 16, qRgb(255, 255, 255)), {{"dark_background", false}}), solid(16, 16, qRgb(255, 255, 255)));
    // Black cells map to '@': glyph pixels in the monochrome colour (.8 + .2*0)*255 = 204 appear on the dark background.
    const QImage glyph = img("ascii_art", solid(16, 16, qRgb(0, 0, 0)), {{"use_source_color", false}});
    int bright = 0;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x)
        if (qRed(px(glyph, x, y)) > 100) ++bright;
    QVERIFY(bright > 4);
    // Invert maps black to the blank character.
    QCOMPARE(img("ascii_art", solid(16, 16, qRgb(0, 0, 0)), {{"invert", true}}), solid(16, 16, dark | 0xFF000000));
    // An empty character set falls back to the default set.
    QCOMPARE(img("ascii_art", fixture(), {{"character_set", ""}}), img("ascii_art", fixture()));
  }
  // ---- batch 3 ----
  void watercolorBorderWashesTheEdgeAndKeepsTheMiddle() {
    const QImage source = fixture(60, 60);
    const QImage out = img("watercolor_border", source, {{"border_size", 20}});
    QCOMPARE(out.size(), QSize(100, 100));
    // Bleed default = int(20 * .4 * .5) = 4: pixels deeper than that are the source.
    for (int y = 6; y < 54; ++y)
      for (int x = 6; x < 54; ++x) QCOMPARE(px(out, x + 20, y + 20), px(source, x, y));
    // Outer corner: full cross ratio -> wash alpha is at least (smoothstep 1) * .9 * .85.
    QVERIFY(qAlpha(px(out, 0, 0)) >= 190);
    // Bleed 0, roughness 0: the pixel just outside the picture has only a sliver of wash (distL 1 of 20 -> alpha ~0).
    const QImage tight = img("watercolor_border", source, {{"border_size", 20}, {"bleed_amount", 0.0}, {"edge_roughness", 0.0}});
    QVERIFY(qAlpha(px(tight, 19, 50)) < 8);
    QCOMPARE(px(tight, 20 + 30, 20 + 30), px(source, 30, 30));
    // Wash colour shows through at the corner (paper shade is +-.06 around it).
    QVERIFY(std::abs(qRed(px(tight, 0, 0)) - 185) < 24);
  }

  void halationGlowsRedAroundBrightAreas() {
    const QImage source = fixture();
    QCOMPARE(img("halation", source, {{"strength", 0.0}}), source);
    QCOMPARE(img("halation", solid(8, 8, qRgb(0, 0, 0))), solid(8, 8, qRgb(0, 0, 0)));  // nothing bright, nothing to scatter
    QImage halves(40, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 40; ++x) halves.setPixel(x, y, x < 20 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    const QImage out = img("halation", halves, {{"radius", 6.0}});
    const QRgb near_ = px(out, 21, 4), far_ = px(out, 38, 4);
    QVERIFY(qRed(near_) > 10);
    QVERIFY(qRed(near_) > qRed(far_));
    QVERIFY(qRed(near_) > qGreen(near_) && qGreen(near_) > qBlue(near_));  // warm: boosts are 1.85 / .85 / .15
    QCOMPARE(px(out, 2, 4), qRgba(255, 255, 255, 255));
  }

  void brickWallBorderLaysMortarAndBricks() {
    const QImage source = fixture(60, 40);
    const QImage out = img("brick_wall_border", source, {{"border_size", 30}});
    QCOMPARE(out.size(), QSize(120, 100));
    for (int y = 0; y < 40; ++y)
      for (int x = 0; x < 60; ++x) QCOMPARE(px(out, x + 30, y + 30), px(source, x, y));
    const QRgb mortar = QColor(195, 188, 175).rgba();
    QCOMPARE(px(out, 0, 0), mortar);              // local (0,0): row 0 fraction 0 is inside the mortar line
    QCOMPARE(px(out, 29, 50), mortar);            // the 4 px mortar frame hugs the picture
    QCOMPARE(px(out, 26, 50), mortar);
    QCOMPARE(px(out, 30 + 30, 30 + 40 + 2), mortar);
    // Bricks: reddish pixels exist in the border, and the colour variation parameter changes them.
    int brick = 0;
    for (int x = 0; x < 120; ++x)
      if (qRed(px(out, x, 10)) > qBlue(px(out, x, 10)) + 60) ++brick;
    QVERIFY(brick > 20);
    QVERIFY(img("brick_wall_border", source, {{"border_size", 30}, {"color_variation", 0.0}}) != out);
    QVERIFY(img("brick_wall_border", source, {{"border_size", 30}, {"bevel_strength", 0.0}}) != out);
  }

  void voronoiMosaicAveragesCells() {
    const QImage source = fixture(40, 32);
    // Flat opaque input without edges is unchanged: every cell averages to the colour.
    const QImage flat = solid(30, 30, qRgb(40, 90, 200));
    QCOMPARE(img("voronoi_mosaic", flat, {{"draw_edges", false}}), flat);
    // No edges: at most one colour per cell.
    const QImage plain = img("voronoi_mosaic", source, {{"cell_count", 10}, {"draw_edges", false}});
    QSet<QRgb> colours;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 40; ++x) colours.insert(px(plain, x, y));
    QVERIFY(colours.size() <= 10);
    QVERIFY(colours.size() >= 3);
    // Edges are drawn in the edge colour on cell borders, and thickness widens them.
    const ParamValues red{{"cell_count", 10}, {"edge_color", QColor(255, 0, 0)}};
    auto edges = [&](const QImage &image) {
      int n = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
          if (px(image, x, y) == qRgb(255, 0, 0)) ++n;
      return n;
    };
    const int thin = edges(img("voronoi_mosaic", source, red));
    ParamValues thick = red;
    thick.insert("edge_thickness", 3.0);
    QVERIFY(thin > 10);
    QVERIFY(edges(img("voronoi_mosaic", source, thick)) > thin);
    // Alpha comes from the source pixel.
    QCOMPARE(qAlpha(px(img("voronoi_mosaic", solid(20, 20, qRgba(5, 6, 7, 66)), {{"draw_edges", false}}), 3, 3)), 66);
  }
  // ---- batch 4 ----
  void risoPrintHandComputed() {
    const QImage source = fixture();
    QCOMPARE(img("riso_print", source, {{"ink_strength", 0.0}}), source);
    // White 16x16, noise 0: dot scale 18 -> cell 8. Cell centre (4,4): dist 0 gives mask 1 (smoothStep(0, 1, 0) = 0) for plate A;
    // plate B is misregistered by round(3*.3)=1 px so its dist is 1 -> mask 0. a = 1 * .7 * (1 - .25*.9) = .5425:
    // r = (1-a) + .8627a = .9255 -> 236; g = b = (1-a) + .2745a = .6064 -> 155.
    const QImage out = img("riso_print", solid(16, 16, qRgb(255, 255, 255)), {{"ink_noise", 0.0}});
    QVERIFY(near(px(out, 4, 4), qRgba(236, 155, 155, 255), 1));
    QCOMPARE(px(out, 0, 0), qRgba(255, 255, 255, 255));  // far from the dot centre: bare paper
    QCOMPARE(qAlpha(px(img("riso_print", solid(8, 8, qRgba(9, 9, 9, 77))), 1, 1)), 77);
  }

  void starfieldBorderFillsTheFrameWithStars() {
    const QImage source = fixture(40, 32);
    const QImage out = img("starfield_border", source);
    QCOMPARE(out.size(), QSize(40 + 120, 32 + 120));
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 40; ++x) QCOMPARE(px(out, x + 60, y + 60), px(source, x, y));
    int stars = 0;
    long dark = 0;
    for (int y = 0; y < out.height(); ++y)
      for (int x = 0; x < out.width(); ++x) {
        if (x >= 60 && x < 100 && y >= 60 && y < 92) continue;
        const QRgb c = px(out, x, y);
        QCOMPARE(qAlpha(c), 255);
        if (qRed(c) > 150) ++stars;
        if (qRed(c) < 80 && qGreen(c) < 80 && qBlue(c) < 120) ++dark;
      }
    QVERIFY(stars > 5);                  // stars
    QVERIFY(dark > (160 * 152 - 1280) / 2);  // mostly deep space
    // Without nebula, density or glow the plain background colour shows between stars.
    const QImage plain = img("starfield_border", source, {{"nebula_intensity", 0.0}, {"bright_stars", false}});
    QVERIFY(plain != out);
    int bg = 0;
    for (int x = 0; x < 160; ++x)
      if (px(plain, x, 2) == qRgb(5, 5, 18)) ++bg;
    QVERIFY(bg > 100);
  }

  void crimsonBorderFramesWithGlowAndLines() {
    const QImage source = fixture(40, 32);
    const QImage out = img("crimson_border", source);
    QCOMPARE(out.size(), QSize(40 + 24, 32 + 24));
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 40; ++x) QCOMPARE(px(out, x + 12, y + 12), px(source, x, y));
    // No glow and no lines: flat crimson border.
    const ParamValues flat{{"glow_strength", 0.0}, {"inner_line", false}, {"outer_line", false}};
    const QImage plain = img("crimson_border", source, flat);
    QCOMPARE(px(plain, 5, 3), qRgba(153, 0, 18, 255));
    QCOMPARE(px(plain, 63, 40), qRgba(153, 0, 18, 255));
    // The outer line is the colour darkened by half: (76, 0, 9), and fully covers the outermost pixel row.
    QVERIFY(near(px(img("crimson_border", source, {{"glow_strength", 0.0}, {"inner_line", false}}), 20, 0), qRgba(76, 0, 9, 255), 1));
    // Glow darkens the outer edge of the band relative to its inner area: compare pixel rows 2 and 8 (size 12, glow width 8.4).
    QVERIFY(qRed(px(img("crimson_border", source, {{"outer_line", false}, {"inner_line", false}, {"glow_strength", 100.0}}), 20, 2)) <
            qRed(px(plain, 20, 2)));
    // The inner line brightens the ring just outside the picture.
    QVERIFY(qRed(px(img("crimson_border", source, {{"glow_strength", 0.0}, {"outer_line", false}}), 20, 11)) > 153);
  }

  void rustCorrosionTintsAndIsNeutralAtZero() {
    const QImage source = fixture();
    QCOMPARE(img("rust_corrosion", source, {{"corrosion", 0.0}}), source);
    const QImage black = solid(64, 64, qRgba(0, 0, 0, 120));
    auto meanRed = [](const QImage &image) {
      double sum = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) sum += qRed(image.pixel(x, y));
      return sum / (double(image.width()) * image.height());
    };
    const QImage mild = img("rust_corrosion", black, {{"corrosion", 20.0}});
    const QImage heavy = img("rust_corrosion", black, {{"corrosion", 100.0}});
    QVERIFY(meanRed(mild) > 2.0);
    QVERIFY(meanRed(heavy) > meanRed(mild));          // more corrosion, more rust
    QCOMPARE(qAlpha(px(heavy, 10, 10)), 120);         // alpha untouched
    // Rust is orange: red leads green leads blue on average.
    long r = 0, g = 0, b = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        r += qRed(px(heavy, x, y));
        g += qGreen(px(heavy, x, y));
        b += qBlue(px(heavy, x, y));
      }
    QVERIFY(r > g && g > b);
  }

  void lavaMoltenBorderGlowsInCracks() {
    const QImage source = fixture(40, 32);
    const QImage out = img("lava_molten_border", source);
    QCOMPARE(out.size(), QSize(40 + 112, 32 + 112));
    QCOMPARE(px(out, 56 + 20, 56 + 16), px(source, 20, 16));
    // The rock is dark and the (rare) cracks are fractal-noise valleys, so look for lava on a large frame at high density.
    const QImage big = img("lava_molten_border", solid(10, 10, qRgb(0, 0, 0)), {{"border_size", 300}, {"crack_density", 100.0}, {"ember_sparks", false}});
    int lava = 0, rock = 0;
    for (int y = 0; y < big.height(); ++y)
      for (int x = 0; x < big.width(); ++x) {
        if (x >= 300 && x < 310 && y >= 300 && y < 310) continue;
        const QRgb c = px(big, x, y);
        if (qRed(c) > 120 && qBlue(c) < 100) ++lava;
        if (qRed(c) < 70 && qGreen(c) < 70) ++rock;
      }
    QVERIFY2(lava > 20, qPrintable(QString::number(lava)));
    QVERIFY(rock > big.width() * big.height() / 2);
    // Ember sparks only land where |noise| <= .15 (the reference's rare crack valleys), so on most frames they change nothing.
    QCOMPARE(img("lava_molten_border", source, {{"ember_sparks", false}}).size(), out.size());
    QVERIFY(img("lava_molten_border", source, {{"crack_density", 100.0}}) != out);
    // The glow stroke straddles the picture's edge but the opaque picture covers it.
    for (int x = 0; x < 40; ++x) QCOMPARE(px(out, 56 + x, 56), px(source, x, 0));
  }

  void rainyWindowHandComputed() {
    const QImage source = fixture();
    const ParamValues off{{"distortion", 0.0}, {"streak_density", 0.0}, {"mist_amount", 0.0}, {"droplet_amount", 0.0}};
    QCOMPARE(img("rainy_window", source, off, 3), source);
    // Distortion alone leaves a flat picture flat.
    const QImage flat = solid(24, 24, qRgb(100, 100, 100));
    ParamValues distortionOnly = off;
    distortionOnly.insert("distortion", 20.0);
    QCOMPARE(img("rainy_window", flat, distortionOnly), flat);
    // Mist alone on flat (100,100,100): lum .392, fog mix = 1 * (.2 + .608*.8) = .6864;
    // r = (100 + 86*.6864)*.92 = 146.3, g = (100 + 96*.6864)*.92 = 152.6, b = (100 + 106*.6864)*.92 = 158.9.
    ParamValues mistOnly = off;
    mistOnly.insert("mist_amount", 100.0);
    QVERIFY(near(px(img("rainy_window", flat, mistOnly), 5, 5), qRgba(146, 153, 159, 255), 1));
    // Droplets add highlights: some pixels of a dark flat picture turn lighter.
    ParamValues drops = off;
    drops.insert("droplet_amount", 100.0);
    const QImage wet = img("rainy_window", solid(64, 64, qRgb(10, 10, 10)), drops, 4);
    int lighter = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        if (qRed(px(wet, x, y)) > 30) ++lighter;
    QVERIFY(lighter > 10);
    QVERIFY(img("rainy_window", source, {}, 1) != img("rainy_window", source, {}, 2));
  }
  // ---- specific tests end ----
};

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  FilterEffects5Test test;
  return QTest::qExec(&test, argc, argv);
}
#include "FilterEffects5Test.moc"
