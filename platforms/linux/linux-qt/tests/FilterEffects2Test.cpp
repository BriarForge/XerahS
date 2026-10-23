// EDITOR-EFFECTS-001: Filters batch F2.
#include "image-editor/effects/Effect.h"

#include <QColorSpace>
#include <QSet>
#include <QTest>

#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kIds{"color_splash", "heat_map", "dream_glow", "radial_rainbow", "diamond_pixelate", "ghost_trail",
                       "film_grain", "glitch_shift", "add_noise", "hexagonal_pixelate", "stripe_overlay", "gaussian_blur",
                       "blur", "triangle_pixelate", "convolution_matrix", "retro_sunset", "motion_blur", "median_filter",
                       "vignette", "wind_streaks", "light_leak", "slice", "unsharp_mask", "color_halftone", "pop_art_grid",
                       "sobel_edge", "glow", "miniature", "oil_paint", "zoom_blur"};
const QStringList kSeeded{"film_grain", "glitch_shift", "add_noise", "slice"};

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
EffectResult run(const QString &id, const QImage &image, const ParamValues &params = {}, const EffectContext &context = {}) {
  const auto *effect = builtinEffects().find(id);
  return effect ? runEffect(*effect, image, params, context) : EffectResult{{}, "missing"};
}
int uniqueColors(const QImage &image) {
  QSet<QRgb> colors;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) colors.insert(image.pixel(x, y));
  return colors.size();
}
double meanChannel(const QImage &image, int shift) {
  double sum = 0;
  const int n = image.width() * image.height();
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) sum += (image.pixel(x, y) >> shift) & 0xFF;
  return sum / n;
}
}

class FilterEffects2Test : public QObject {
  Q_OBJECT
private slots:
  void allThirtyRegisteredWithReferenceAndValidDefaults() {
    QCOMPARE(kIds.size(), 30);
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.category, Category::Filters);
      QVERIFY(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Filters/"));
      QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
      QCOMPARE(spec.changesSize, id == "glow" || id == "slice");
      QCOMPARE(spec.usesSeed, kSeeded.contains(id));
      QCOMPARE(spec.mode, id == "heat_map" ? Mode::Immediate : Mode::Previewable);
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
        } else {
          QCOMPARE(p.kind, ParamKind::Color);
          QVERIFY(p.defaultValue.value<QColor>().isValid());
        }
      }
    }
    QCOMPARE(builtinEffects().find("blur")->spec().parameters[0].defaultValue.toInt(), 10);
    QCOMPARE(builtinEffects().find("wind_streaks")->spec().parameters[3].defaultValue.toString(), "right");
    QCOMPARE(builtinEffects().find("light_leak")->spec().parameters[2].choices.size(), 6);
    QCOMPARE(builtinEffects().find("light_leak")->spec().parameters[1].defaultValue.value<QColor>(), QColor(255, 160, 50));
    QCOMPARE(builtinEffects().find("stripe_overlay")->spec().parameters[4].defaultValue.value<QColor>(), QColor(0, 0, 0));
    QCOMPARE(builtinEffects().find("pop_art_grid")->spec().parameters[0].defaultValue.value<QColor>(), QColor(255, 50, 50));
    QCOMPARE(builtinEffects().find("glow")->spec().parameters[4].defaultValue.value<QColor>(), QColor(255, 255, 255));
    QVERIFY(builtinEffects().find("film_grain")->spec().parameters[1].defaultValue.toBool());
    QVERIFY(builtinEffects().find("glow")->spec().parameters[5].defaultValue.toBool());
  }

  void referenceDefaults() {
    struct Expected { const char *id; const char *parameter; double initial, minimum, maximum; };
    const Expected expected[]{
        {"color_splash", "hue", 0, 0, 360}, {"color_splash", "tolerance", 30, 1, 180},
        {"dream_glow", "radius", 15, 1, 50}, {"dream_glow", "opacity", 50, 0, 100},
        {"radial_rainbow", "strength", 50, 0, 100}, {"radial_rainbow", "rotations", 1, 1, 5},
        {"diamond_pixelate", "size", 16, 4, 100}, {"ghost_trail", "copies", 3, 1, 8},
        {"ghost_trail", "offset_x", 15, -50, 50}, {"ghost_trail", "fade", 50, 10, 90},
        {"film_grain", "amount", 30, 1, 100}, {"glitch_shift", "intensity", 30, 1, 100},
        {"glitch_shift", "bands", 12, 2, 50}, {"add_noise", "amount", 8, 0, 100},
        {"gaussian_blur", "radius", 15, 1, 200}, {"blur", "radius", 10, 1, 200},
        {"triangle_pixelate", "size", 24, 8, 100}, {"convolution_matrix", "x1y1", 1, -99, 99},
        {"convolution_matrix", "factor", 1, 0.01, 10}, {"convolution_matrix", "offset", 0, -255, 255},
        {"retro_sunset", "strength", 50, 0, 100}, {"motion_blur", "distance", 12, 1, 200},
        {"motion_blur", "angle", 0, 0, 360}, {"median_filter", "radius", 1, 1, 5},
        {"vignette", "strength", 0.5, 0, 1}, {"vignette", "radius", 0.75, 0.05, 0.999},
        {"wind_streaks", "length", 30, 5, 100}, {"wind_streaks", "threshold", 200, 100, 254},
        {"slice", "min_shift", 0, 0, 100}, {"slice", "max_shift", 10, 0, 100},
        {"unsharp_mask", "amount", 150, 0, 500}, {"unsharp_mask", "radius", 5, 1, 100},
        {"color_halftone", "size", 8, 4, 30}, {"pop_art_grid", "strength", 60, 0, 100},
        {"sobel_edge", "strength", 1.2, 0.1, 5}, {"sobel_edge", "threshold", 20, 0, 255},
        {"glow", "size", 20, 1, 100}, {"glow", "strength", 80, 1, 100},
        {"miniature", "blur_radius", 15, 2, 40}, {"miniature", "saturation", 20, 0, 50},
        {"oil_paint", "radius", 3, 1, 6}, {"oil_paint", "levels", 24, 8, 64},
        {"zoom_blur", "strength", 35, 0, 100}, {"zoom_blur", "samples", 24, 4, 64},
        {"light_leak", "intensity", 50, 0, 100}};
    for (const auto &e : expected) {
      const auto &spec = builtinEffects().find(e.id)->spec();
      const auto p = std::find_if(spec.parameters.cbegin(), spec.parameters.cend(),
                                  [&](const ParamSpec &q) { return q.id == QLatin1String(e.parameter); });
      QVERIFY2(p != spec.parameters.cend(), e.parameter);
      QCOMPARE(p->defaultValue.toDouble(), e.initial);
      QCOMPARE(p->minimum, e.minimum);
      QCOMPARE(p->maximum, e.maximum);
    }
  }

  void neutralParametersGiveIdenticalImage_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariantMap>("params");
    QTest::newRow("dream_glow") << "dream_glow" << QVariantMap{{"opacity", 0.0}};
    QTest::newRow("radial_rainbow") << "radial_rainbow" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("stripe") << "stripe_overlay" << QVariantMap{{"opacity", 0.0}};
    QTest::newRow("convolution") << "convolution_matrix" << QVariantMap{};
    QTest::newRow("retro") << "retro_sunset" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("motion") << "motion_blur" << QVariantMap{{"distance", 1}};
    QTest::newRow("vignette") << "vignette" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("wind") << "wind_streaks" << QVariantMap{{"opacity", 0.0}};
    QTest::newRow("light_leak") << "light_leak" << QVariantMap{{"intensity", 0.0}};
    QTest::newRow("slice") << "slice" << QVariantMap{{"min_shift", 0}, {"max_shift", 0}};
    QTest::newRow("unsharp") << "unsharp_mask" << QVariantMap{{"amount", 0.0}};
    QTest::newRow("add_noise") << "add_noise" << QVariantMap{{"amount", 0.0}};
    QTest::newRow("zoom") << "zoom_blur" << QVariantMap{{"strength", 0.0}};
  }
  void neutralParametersGiveIdenticalImage() {
    QFETCH(QString, id);
    QFETCH(QVariantMap, params);
    const auto image = fixture();
    const auto result = run(id, image, params);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image, image);
    QCOMPARE(result.image.colorSpace(), image.colorSpace());
  }

  void glowIdentityWhenOpaqueAndNotResized() {
    const auto image = solid(4, 4, qRgba(30, 90, 140, 255));
    const auto result = run("glow", image, {{"auto_resize", false}, {"offset_x", 0}, {"offset_y", 0}});
    QVERIFY(result.ok());
    QCOMPARE(result.image, image);
  }

  void handComputedPixels() {
    // color_splash: pure green hue 120 is outside tolerance 30 of hue 0, sat > 5.
    // gray = (byte)(0.7152*255) = 182. Pure red hue 0 is kept.
    QCOMPARE(run("color_splash", solid(1, 1, qRgba(0, 255, 0, 200))).image.pixel(0, 0), qRgba(182, 182, 182, 200));
    QCOMPARE(run("color_splash", solid(1, 1, qRgba(255, 0, 0, 200))).image.pixel(0, 0), qRgba(255, 0, 0, 200));

    // heat_map luma/255. Black stays black. Red t = 0.2126 is in [0.2, 0.4): g = (byte)(0.063*255) = 16, b = 255.
    // Gray 128: t = 128/255 ≈ 0.50196, b = (byte)((1-s)*255) = 124 (truncation, not 125).
    QCOMPARE(run("heat_map", solid(1, 1, qRgba(0, 0, 0, 200))).image.pixel(0, 0), qRgba(0, 0, 0, 200));
    QCOMPARE(run("heat_map", solid(1, 1, qRgba(255, 0, 0, 255))).image.pixel(0, 0), qRgba(0, 16, 255, 255));
    QCOMPARE(run("heat_map", solid(1, 1, qRgba(128, 128, 128, 80))).image.pixel(0, 0), qRgba(0, 255, 124, 80));

    // convolution identity kernel, factor 1, offset 10: channel = roundHalfAway(c + 10), alpha copied.
    QCOMPARE(run("convolution_matrix", solid(1, 1, qRgba(10, 20, 30, 40)), {{"offset", 10}}).image.pixel(0, 0),
             qRgba(20, 30, 40, 40));

    // motion distance 3 angle 0: samples i = -1,0,1, integer mean. Edges clamp.
    // (10+10+40)/3 = 20, (10+40+70)/3 = 40, (40+70+70)/3 = 60.
    QImage ramp(3, 1, QImage::Format_ARGB32);
    ramp.setPixel(0, 0, qRgba(10, 10, 10, 255));
    ramp.setPixel(1, 0, qRgba(40, 40, 40, 255));
    ramp.setPixel(2, 0, qRgba(70, 70, 70, 255));
    const auto motion = run("motion_blur", ramp, {{"distance", 3}, {"angle", 0.0}});
    QVERIFY(motion.ok());
    QCOMPARE(motion.image.pixel(0, 0), qRgba(20, 20, 20, 255));
    QCOMPARE(motion.image.pixel(1, 0), qRgba(40, 40, 40, 255));
    QCOMPARE(motion.image.pixel(2, 0), qRgba(60, 60, 60, 255));

    // median radius 1 on a 3x1 row (1,9,3). Height clamps, so each value is repeated 3 times.
    // Center window sorted [1,1,1,3,3,3,9,9,9], mid index 4 = 3.
    QImage med(3, 1, QImage::Format_ARGB32);
    med.setPixel(0, 0, qRgba(1, 1, 1, 255));
    med.setPixel(1, 0, qRgba(9, 9, 9, 255));
    med.setPixel(2, 0, qRgba(3, 3, 3, 255));
    QCOMPARE(run("median_filter", med).image.pixel(1, 0), qRgba(3, 3, 3, 255));

    // oil radius 1 levels 8: both pixels have intensity (255*8)/768 = 2, so one bin.
    // x=0 sees 6 red + 3 blue → (1530/9, 0, 765/9) = (170, 0, 85). x=1 is the swap.
    QImage oil(2, 1, QImage::Format_ARGB32);
    oil.setPixel(0, 0, qRgba(255, 0, 0, 255));
    oil.setPixel(1, 0, qRgba(0, 0, 255, 255));
    const auto painted = run("oil_paint", oil, {{"radius", 1}, {"levels", 8}});
    QVERIFY(painted.ok());
    QCOMPARE(painted.image.pixel(0, 0), qRgba(170, 0, 85, 255));
    QCOMPARE(painted.image.pixel(1, 0), qRgba(85, 0, 170, 255));

    // vignette 3x3, strength 1, radius 0.75. Corner distance01 is 1 in float32, smoothstep factor 0.
    // Center distance is 0, so the pixel is unchanged.
    QImage white = solid(3, 3, qRgba(255, 255, 255, 255));
    const auto vig = run("vignette", white, {{"strength", 1.0}, {"radius", 0.75}});
    QVERIFY(vig.ok());
    QCOMPARE(vig.image.pixel(0, 0), qRgba(0, 0, 0, 255));
    QCOMPARE(vig.image.pixel(1, 1), qRgba(255, 255, 255, 255));

    // sobel: left column black, rest white. Center of the left edge: gx = 1020, gy = 0,
    // mag = 1020*1.2 = 1224 → clamped 255. Alpha is copied from the source pixel.
    QImage edge(3, 3, QImage::Format_ARGB32);
    edge.fill(qRgba(255, 255, 255, 255));
    for (int y = 0; y < 3; ++y) edge.setPixel(0, y, qRgba(0, 0, 0, 200));
    QCOMPARE(run("sobel_edge", edge).image.pixel(0, 1), qRgba(255, 255, 255, 200));

    // zoom samples 4, strength 100, center (0,0) on
    // (255,0,0) (0,255,0) / (0,0,255) (255,255,255).
    // Pixel (1,0) weighted bilinear mean rounds to (97, 158, 0). Pixel (1,1) red is
    // 198.50002 in float32, just above the half-even boundary, so it rounds to 199.
    QImage zoom(2, 2, QImage::Format_ARGB32);
    zoom.setPixel(0, 0, qRgba(255, 0, 0, 255));
    zoom.setPixel(1, 0, qRgba(0, 255, 0, 255));
    zoom.setPixel(0, 1, qRgba(0, 0, 255, 255));
    zoom.setPixel(1, 1, qRgba(255, 255, 255, 255));
    const auto zoomed = run("zoom_blur", zoom, {{"strength", 100.0}, {"samples", 4}, {"center_x", 0.0}, {"center_y", 0.0}});
    QVERIFY(zoomed.ok());
    QCOMPARE(zoomed.image.pixel(1, 0), qRgba(97, 158, 0, 255));
    QCOMPARE(zoomed.image.pixel(1, 1), qRgba(199, 158, 158, 255));

    // add_noise seed 1337, amount 8 → amplitude round(10.16) = 10. Pixel 0 hash deltas +1, -1, -7.
    EffectContext noise;
    noise.seed = 1337;
    QCOMPARE(run("add_noise", solid(1, 1, qRgba(128, 128, 128, 255)), {}, noise).image.pixel(0, 0), qRgba(129, 127, 121, 255));

    // wind length 5 (the parameter minimum), threshold 200, opacity 100, right.
    // White at x=0: s=1 fade=0.8 → (byte)(204) = 204; s=2 fade=0.6 → (byte)(153) = 153.
    // Later pixels are below the threshold in the source, so they do not restreak.
    QImage wind(3, 1, QImage::Format_ARGB32);
    wind.fill(qRgba(0, 0, 0, 255));
    wind.setPixel(0, 0, qRgba(255, 255, 255, 255));
    const auto streaked = run("wind_streaks", wind, {{"length", 5}, {"threshold", 200}, {"opacity", 100.0}, {"direction", "right"}});
    QVERIFY(streaked.ok());
    QCOMPARE(streaked.image.pixel(0, 0), qRgba(255, 255, 255, 255));
    QCOMPARE(streaked.image.pixel(1, 0), qRgba(204, 204, 204, 255));
    QCOMPARE(streaked.image.pixel(2, 0), qRgba(153, 153, 153, 255));

    // ghost: one copy, offset x = 1, fade 50 → paint alpha (byte)(255*0.5) = 127 on transparent.
    QImage ghost(4, 1, QImage::Format_ARGB32);
    ghost.fill(qRgba(0, 0, 0, 0));
    ghost.setPixel(0, 0, qRgba(255, 0, 0, 255));
    const auto trailed = run("ghost_trail", ghost, {{"copies", 1}, {"offset_x", 1}, {"offset_y", 0}, {"fade", 50.0}});
    QVERIFY(trailed.ok());
    QCOMPARE(trailed.image.pixel(0, 0), qRgba(255, 0, 0, 255));
    QCOMPARE(trailed.image.pixel(1, 0), qRgba(255, 0, 0, 127));
    QCOMPARE(trailed.image.pixel(2, 0), qRgba(0, 0, 0, 0));

    // radial rainbow strength 100, rotations 1. Pixel (1,1) of a 2x2 sits on the center, atan2(0,0) = 0,
    // hue shift 180. Red (s=100,l=50) becomes cyan.
    QImage rainbow = solid(2, 2, qRgba(255, 0, 0, 255));
    QCOMPARE(run("radial_rainbow", rainbow, {{"strength", 100.0}, {"rotations", 1}}).image.pixel(1, 1), qRgba(0, 255, 255, 255));

    // retro sunset strength 100, no lines, 1px tall: t = 0.5 sits between stops 0.33 and 0.66.
    // local ≈ 0.51515 → (238, 61, 39) at alpha 200, W3C overlay on gray 128 → (214, 76, 59).
    const auto sunset = run("retro_sunset", solid(1, 1, qRgba(128, 128, 128, 255)),
                            {{"strength", 100.0}, {"horizontal_lines", false}});
    QVERIFY(sunset.ok());
    QCOMPARE(sunset.image.pixel(0, 0), qRgba(214, 76, 59, 255));

    // light leak intensity 100, red, top-left, 1x1. Distance of the pixel centre / radius ≈ 0.625,
    // alpha = (byte)(255*0.375) = 95. Screen onto black is (95, 0, 0).
    const auto leak = run("light_leak", solid(1, 1, qRgba(0, 0, 0, 255)),
                          {{"intensity", 100.0}, {"color", QColor(255, 0, 0)}, {"position", "top_left"}});
    QVERIFY(leak.ok());
    QCOMPARE(leak.image.pixel(0, 0), qRgba(95, 0, 0, 255));

    // stripe angle 0, width 4, gap 4, opaque blue. Pixel centres 0.5..3.5 fall in the first rect.
    QImage stripes(8, 1, QImage::Format_ARGB32);
    stripes.fill(qRgba(255, 255, 255, 255));
    const auto striped = run("stripe_overlay", stripes, {{"width", 4}, {"gap", 4}, {"angle", 0}, {"opacity", 100.0},
                                                         {"color", QColor(0, 0, 255)}});
    QVERIFY(striped.ok());
    QCOMPARE(striped.image.pixel(0, 0), qRgba(0, 0, 255, 255));
    QCOMPARE(striped.image.pixel(3, 0), qRgba(0, 0, 255, 255));
    QCOMPARE(striped.image.pixel(4, 0), qRgba(255, 255, 255, 255));

    // colour halftone size 8 on pure red. (0,0) is inside cyan-skipped, magenta, yellow and black dots → black.
    // (3,0) is inside magenta and yellow (radius 4) and outside black (radius ≈ 3.15) → red. Alpha becomes 255.
    QImage red = solid(8, 2, qRgba(255, 0, 0, 10));
    const auto half = run("color_halftone", red, {{"size", 8}});
    QVERIFY(half.ok());
    QCOMPARE(half.image.pixel(0, 0), qRgba(0, 0, 0, 255));
    QCOMPARE(half.image.pixel(3, 0), qRgba(255, 0, 0, 255));

    // pop art 4x4 gray 100, strength 100. gray = round(0.33*100*3) = 99.
    // Cell 0 tint (255,50,50): mulDiv255 → (99, 19, 19), alpha 255.
    QImage pop = solid(4, 4, qRgba(100, 100, 100, 255));
    QCOMPARE(run("pop_art_grid", pop, {{"strength", 100.0}}).image.pixel(0, 0), qRgba(99, 19, 19, 255));
    QCOMPARE(run("pop_art_grid", pop, {{"strength", 100.0}}).image.pixel(2, 0), qRgba(19, 78, 19, 255));
  }

  void flatAreasAndRegionStatistics() {
    const auto white = solid(8, 8, qRgba(255, 255, 255, 255));
    // Gaussian pad is 6 sigma, so a flat opaque image is unchanged. Plain blur's 3-sigma window
    // is wider than its pad: outside samples are transparent, so the corner alpha falls while
    // the unpremultiplied colour stays white. The centre is still inside the pad.
    QCOMPARE(run("gaussian_blur", white, {{"radius", 15}}).image, white);
    const auto plain = run("blur", white, {{"radius", 2}});
    QVERIFY(plain.ok());
    QVERIFY(plain.image != white);
    QVERIFY(qAlpha(plain.image.pixel(0, 0)) < 255);
    QCOMPARE(qRed(plain.image.pixel(0, 0)), 255);
    QCOMPARE(plain.image.pixel(4, 4), qRgba(255, 255, 255, 255));
    QCOMPARE(run("median_filter", white).image, white);
    QCOMPARE(run("oil_paint", white, {{"radius", 1}}).image, white);
    QCOMPARE(run("motion_blur", white, {{"distance", 5}}).image, white);
    QCOMPARE(run("zoom_blur", white, {{"strength", 80.0}}).image, white);
    // Sobel of a flat field is magnitude 0, below the default threshold, so the field becomes black.
    QCOMPARE(run("sobel_edge", white).image.pixel(3, 3), qRgba(0, 0, 0, 255));

    const auto image = varied();
    for (const char *id : {"diamond_pixelate", "hexagonal_pixelate", "triangle_pixelate"}) {
      const auto result = run(id, image, {{"size", 8}});
      QVERIFY2(result.ok(), id);
      QVERIFY2(result.image != image, id);
      QVERIFY2(uniqueColors(result.image) < uniqueColors(image), id);
      QVERIFY(qAlpha(result.image.pixel(4, 4)) == 255);
    }
    const auto grainImage = solid(8, 8, qRgba(128, 128, 128, 200));
    EffectContext seeded;
    seeded.seed = 3;
    const auto grain = run("film_grain", grainImage, {{"amount", 40}, {"monochrome", true}}, seeded);
    QVERIFY(grain.ok());
    QVERIFY(grain.image != grainImage);
    QCOMPARE(qAlpha(grain.image.pixel(0, 0)), 200);
    QCOMPARE(qRed(grain.image.pixel(0, 0)), qGreen(grain.image.pixel(0, 0)));
    QVERIFY(std::fabs(meanChannel(grain.image, 16) - 128.0) < 40.0);
    int grainDiffers = 0;
    for (int i = 1; i < 8; ++i)
      if (grain.image.pixel(i, 0) != grain.image.pixel(0, 0)) ++grainDiffers;
    QVERIFY(grainDiffers > 0);

    // Screen of gray with itself lightens even a flat field: 1-(1-0.502)^2 ≈ 0.75 → about 192.
    const auto gray = solid(8, 8, qRgba(128, 128, 128, 255));
    const auto halo = run("dream_glow", gray, {{"radius", 2}, {"opacity", 100.0}});
    QVERIFY(halo.ok());
    QVERIFY(qRed(halo.image.pixel(4, 4)) > 160);
    QVERIFY(qAlpha(halo.image.pixel(4, 4)) == 255);
  }

  void defaultsChangeAVariedImage() {
    const auto image = varied();
    for (const QString &id : kIds) {
      if (id == "convolution_matrix") continue;  // the default kernel is the identity
      const auto result = run(id, image);
      QVERIFY2(result.ok(), qPrintable(id + " " + result.error));
      QVERIFY2(result.image != image, qPrintable(id));
    }
    QCOMPARE(run("convolution_matrix", image).image, image);
    const auto glow = run("glow", fixture());
    QCOMPARE(glow.image.size(), QSize(43, 43));  // 3 + 2 * default size 20
    const auto sliced = run("slice", fixture());
    QCOMPARE(sliced.image.size(), QSize(23, 3));  // width + 2 * default max shift 10
  }

  void seededEffectsDependOnlyOnContextSeed() {
    const auto image = varied();
    for (const QString &id : kSeeded) {
      EffectContext first;
      first.seed = 11;
      EffectContext second;
      second.seed = 11;
      EffectContext third;
      third.seed = 29;
      const auto a = run(id, image, {}, first);
      const auto b = run(id, image, {}, second);
      const auto c = run(id, image, {}, third);
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
      if (id == "glow") QCOMPARE(result.image.size(), QSize(image.width() + 40, image.height() + 40));
      else if (id == "slice") QCOMPARE(result.image.size(), QSize(image.width() + 20, image.height()));
      else QCOMPARE(result.image.size(), image.size());
      QCOMPARE(result.image.colorSpace(), image.colorSpace());
      QCOMPARE(image, fixture());
      const auto plain = run(id, untagged);
      QVERIFY2(plain.ok(), qPrintable(id));
      QVERIFY(!plain.image.colorSpace().isValid());
      QVERIFY(run(id, image.convertToFormat(QImage::Format_ARGB32_Premultiplied)).ok());
      QCOMPARE(run(id, image).image, result.image);
    }
    QVERIFY(!run("vignette", image, {{"strength", 2.0}}).ok());
    QVERIFY(!run("wind_streaks", image, {{"direction", "up"}}).ok());
    QVERIFY(!run("blur", image, {{"unknown", 1}}).ok());
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
      QVERIFY2(direct.error == "effect-cancelled", qPrintable(id));
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

QTEST_GUILESS_MAIN(FilterEffects2Test)
#include "FilterEffects2Test.moc"
