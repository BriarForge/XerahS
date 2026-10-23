// EDITOR-EFFECTS-001: EE-003, EE-008..EE-010, EE-016, EE-018 (second batch of Adjustments).
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QTest>

using namespace xerahs::effects;
namespace {
const QStringList kIds{"auto_contrast", "black_and_white", "channel_swap", "clarity", "color_balance", "color_matrix",
                       "colorize", "cross_process", "dehaze", "duotone_gradient_map", "exposure", "film_emulation",
                       "gamma", "hue", "levels", "lomo", "polaroid", "posterize", "replace_color", "sepia",
                       "shadows_highlights", "solarize", "split_toning", "temperature_tint", "threshold", "vibrance"};

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
QRgb one(const QString &id, QRgb pixel, const ParamValues &params) {
  const auto result = run(id, solid(1, 1, pixel), params);
  if (!result.ok()) return 0;
  return result.image.pixel(0, 0);
}
}

class AdjustmentEffects2Test : public QObject {
  Q_OBJECT
private slots:
  void allTwentySixRegisteredWithReferenceAndValidDefaults() {
    QCOMPARE(kIds.size(), 26);
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.category, Category::Adjustments);
      QVERIFY(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Adjustments/"));
      QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
      QVERIFY(!spec.changesSize);
      QCOMPARE(spec.usesSeed, id == "film_emulation");
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
        } else {
          QCOMPARE(p.kind, ParamKind::Color);
          QVERIFY(p.defaultValue.value<QColor>().isValid());
        }
      }
    }
    QCOMPARE(builtinEffects().find("black_and_white")->spec().mode, Mode::Immediate);
    QCOMPARE(builtinEffects().find("polaroid")->spec().mode, Mode::Immediate);
    QCOMPARE(builtinEffects().find("color_matrix")->spec().parameters.size(), 20);
  }

  void referenceDefaults() {
    struct Expected { const char *id; const char *parameter; double initial, minimum, maximum; };
    const Expected expected[]{
      {"auto_contrast", "clip_percent", 0.5, 0, 20}, {"clarity", "amount", 0, -100, 100},
      {"colorize", "strength", 50, 0, 100}, {"cross_process", "strength", 100, 0, 100},
      {"dehaze", "amount", 50, -100, 100}, {"duotone_gradient_map", "contrast", 110, 50, 200},
      {"duotone_gradient_map", "gamma", 1, 0.1, 5}, {"duotone_gradient_map", "blend", 100, 0, 100},
      {"exposure", "amount", 0, -100, 100}, {"film_emulation", "tone_strength", 65, 0, 100},
      {"film_emulation", "grain_amount", 12, 0, 100}, {"film_emulation", "fade_amount", 10, 0, 100},
      {"film_emulation", "contrast_amount", 110, 50, 150}, {"gamma", "amount", 1, 0.1, 5},
      {"hue", "amount", 0, -180, 180}, {"levels", "input_black", 0, 0, 255}, {"levels", "input_white", 255, 0, 255},
      {"levels", "gamma", 1, 0.1, 5}, {"levels", "output_black", 0, 0, 255}, {"levels", "output_white", 255, 0, 255},
      {"lomo", "strength", 100, 0, 100}, {"posterize", "levels", 8, 2, 64}, {"replace_color", "tolerance", 40, 0, 255},
      {"sepia", "strength", 100, 0, 100}, {"solarize", "threshold", 128, 0, 255}, {"split_toning", "strength", 30, 0, 100},
      {"split_toning", "balance", 0, -100, 100}, {"threshold", "value", 128, 0, 255}, {"vibrance", "amount", 0, -100, 100},
      {"color_matrix", "rr", 1, -5, 5}, {"color_matrix", "ro", 0, -255, 255}, {"color_matrix", "aa", 1, -5, 5},
      {"color_matrix", "rg", 0, -5, 5}, {"color_balance", "mid_cyan_red", 0, -100, 100},
      {"shadows_highlights", "shadows", 0, -100, 100}, {"temperature_tint", "tint", 0, -100, 100}};
    for (const auto &e : expected) {
      const auto &spec = builtinEffects().find(e.id)->spec();
      const auto p = std::find_if(spec.parameters.cbegin(), spec.parameters.cend(), [&](const ParamSpec &q) { return q.id == e.parameter; });
      QVERIFY2(p != spec.parameters.cend(), e.parameter);
      QCOMPARE(p->defaultValue.toDouble(), e.initial);
      QCOMPARE(p->minimum, e.minimum);
      QCOMPARE(p->maximum, e.maximum);
    }
    const auto &duotone = builtinEffects().find("duotone_gradient_map")->spec();
    QCOMPARE(duotone.parameters[0].defaultValue.value<QColor>(), QColor(24, 28, 78));
    QCOMPARE(duotone.parameters[1].defaultValue.value<QColor>(), QColor(182, 60, 132));
    QCOMPARE(duotone.parameters[2].defaultValue.value<QColor>(), QColor(255, 224, 132));
    QCOMPARE(builtinEffects().find("colorize")->spec().parameters[0].defaultValue.value<QColor>(), QColor(255, 165, 0));
    QCOMPARE(builtinEffects().find("channel_swap")->spec().parameters[0].choices.size(), 5);
    QCOMPARE(builtinEffects().find("film_emulation")->spec().parameters[0].choices.size(), 5);
  }

  void neutralParametersGiveIdenticalImage_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariantMap>("params");
    // Reference neutral settings (copy-through or arithmetic identity).
    QTest::newRow("auto_contrast") << "auto_contrast" << QVariantMap{{"clip_percent", 0.0}};
    QTest::newRow("clarity") << "clarity" << QVariantMap{{"amount", 0.0}};
    QTest::newRow("color_balance") << "color_balance" << QVariantMap{};
    QTest::newRow("color_matrix") << "color_matrix" << QVariantMap{};
    QTest::newRow("colorize") << "colorize" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("cross_process") << "cross_process" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("dehaze") << "dehaze" << QVariantMap{{"amount", 0.0}};
    QTest::newRow("duotone") << "duotone_gradient_map" << QVariantMap{{"blend", 0.0}};
    QTest::newRow("exposure") << "exposure" << QVariantMap{};
    QTest::newRow("film_emulation") << "film_emulation" << QVariantMap{{"tone_strength", 0.0}, {"grain_amount", 0.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}};
    QTest::newRow("gamma") << "gamma" << QVariantMap{{"amount", 1.0}};
    QTest::newRow("hue") << "hue" << QVariantMap{};
    QTest::newRow("levels") << "levels" << QVariantMap{};
    QTest::newRow("lomo") << "lomo" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("replace_color-no-match") << "replace_color" << QVariantMap{{"target_color", QColor(7, 7, 7, 7)}, {"tolerance", 0.0}};
    QTest::newRow("sepia") << "sepia" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("shadows_highlights") << "shadows_highlights" << QVariantMap{};
    QTest::newRow("solarize-255") << "solarize" << QVariantMap{{"threshold", 255}};
    QTest::newRow("split_toning") << "split_toning" << QVariantMap{{"strength", 0.0}};
    QTest::newRow("temperature_tint") << "temperature_tint" << QVariantMap{};
    QTest::newRow("vibrance") << "vibrance" << QVariantMap{};
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

  void handComputedPixels_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariantMap>("params");
    QTest::addColumn<QRgb>("input");
    QTest::addColumn<QRgb>("expected");
    const QRgb p = qRgba(40, 100, 200, 128);
    const QRgb o = qRgba(40, 100, 200, 255);
    // black_and_white: lum = .2126*40 + .7152*100 + .0722*200 = 94.464 <= 127 -> opaque black; the reference returns
    // SKColors.Black / White, so alpha becomes 255.
    QTest::newRow("bw-dark") << "black_and_white" << QVariantMap{} << p << qRgba(0, 0, 0, 255);
    QTest::newRow("bw-light") << "black_and_white" << QVariantMap{} << qRgba(250, 250, 250, 10) << qRgba(255, 255, 255, 255);
    // lum = 127 is not > 127.
    QTest::newRow("bw-edge") << "black_and_white" << QVariantMap{} << qRgba(127, 127, 127, 255) << qRgba(0, 0, 0, 255);
    // channel_swap: SKColor(r, g, b, a) built from swapped channels.
    QTest::newRow("swap-rg") << "channel_swap" << QVariantMap{{"mode", "red_green"}} << p << qRgba(100, 40, 200, 128);
    QTest::newRow("swap-rb") << "channel_swap" << QVariantMap{{"mode", "red_blue"}} << p << qRgba(200, 100, 40, 128);
    QTest::newRow("swap-gb") << "channel_swap" << QVariantMap{{"mode", "green_blue"}} << p << qRgba(40, 200, 100, 128);
    QTest::newRow("rotate-rgb") << "channel_swap" << QVariantMap{{"mode", "rotate_rgb"}} << p << qRgba(200, 40, 100, 128);
    QTest::newRow("rotate-bgr") << "channel_swap" << QVariantMap{{"mode", "rotate_bgr"}} << p << qRgba(100, 200, 40, 128);
    // color_balance: lum(40,100,200)/255 = .299*.15686+.587*.39216+.114*.78431 = .3665 -> shadow/highlight weights 0, mid 1.
    // shift = value/100 * .5: red +.25 -> (.15686+.25)*255 = 103.75 -> 104; blue -.25 -> (.78431-.25)*255 = 136.25 -> 136.
    QTest::newRow("balance-mid") << "color_balance" << QVariantMap{{"mid_cyan_red", 50.0}, {"mid_yellow_blue", -50.0}} << o << qRgba(104, 100, 136, 255);
    // (10,10,10): lum .03922, shadow weight 1-4*lum = .84314, mid .15686; green shadow +50 -> shift .21078 -> .25 * 255 = 63.75 -> 64.
    QTest::newRow("balance-shadow") << "color_balance" << QVariantMap{{"shadow_magenta_green", 50.0}} << qRgba(10, 10, 10, 200) << qRgba(10, 64, 10, 200);
    // color_matrix rows are Skia matrix rows with offsets in 0..1 units:
    // R = .5*40 + .5*100 = 70; B = 200 + (-.2*255) = 149; A = .5*128 = 64.
    QTest::newRow("matrix") << "color_matrix" << QVariantMap{{"rr", 0.5}, {"rg", 0.5}, {"bo", -0.2}, {"aa", 0.5}} << p << qRgba(70, 100, 149, 64);
    // colorize 100: gray = 94.464; modulate with orange (255,165,0) -> (94.46, 94.464*165/255 = 61.12, 0).
    QTest::newRow("colorize-full") << "colorize" << QVariantMap{{"strength", 100.0}} << o << qRgba(94, 61, 0, 255);
    // colorize 50: paint alpha (byte)(255*.5) = 127 -> a = .498; R = 94.464*.498 + 40*.502 = 67.1, G = 61.12*.498 + 100*.502 = 80.6, B = 0 + 200*.502 = 100.4.
    QTest::newRow("colorize-half") << "colorize" << QVariantMap{{"strength", 50.0}} << o << qRgba(67, 81, 100, 255);
    // cross_process 50: matrix = identity/2 + cross/2 -> R = .6*40 + .55*100*... computed per row: (45,116,191).
    // R = (1.1*40 + .05*100 - .05*200) + .025*255 = 44 + 5 - 10 + 6.375 = 45.375 -> 45
    QTest::newRow("cross-half") << "cross_process" << QVariantMap{{"strength", 50.0}} << o << qRgba(45, 116, 191, 255);
    // duotone defaults: lum .37045 -> (lum-.5)*1.1+.5 = .35751; t<=.5 so lerp(shadow, mid, .71502)
    // = (24+158*.71502, 28+32*.71502, 78+54*.71502) = (137, 51, 117); blend 100 keeps it.
    QTest::newRow("duotone") << "duotone_gradient_map" << QVariantMap{} << p << qRgba(137, 51, 117, 128);
    // blend 25: lerp(40,137,.25)=64.25, lerp(100,51,.25)=87.75, lerp(200,117,.25)=179.25.
    QTest::newRow("duotone-blend") << "duotone_gradient_map" << QVariantMap{{"blend", 25.0}} << p << qRgba(64, 88, 179, 128);
    // exposure: gain 2^amount; +1 stop doubles, clipping 200 -> 255; -1 stop halves.
    QTest::newRow("exposure-up") << "exposure" << QVariantMap{{"amount", 1.0}} << p << qRgba(80, 200, 255, 128);
    QTest::newRow("exposure-down") << "exposure" << QVariantMap{{"amount", -1.0}} << p << qRgba(20, 50, 100, 128);
    // film_emulation, tone 100, no grain/fade, contrast 100, Classic: R = .15686*1.05 + .39216*.02 = .17255 -> 44 ... see below.
    // classic: (.15686*1.05+.39216*.02, .39216*1.02+.15686*.01, .78431*.94+.39216*.02) * 255 = (44.0, 102.0, 190.0).
    QTest::newRow("film-classic") << "film_emulation" << QVariantMap{{"preset", "classic"}, {"tone_strength", 100.0}, {"grain_amount", 0.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}} << p << qRgba(44, 102, 190, 128);
    // cool: (.15686*.94+.78431*.02, .39216*1.01+.78431*.01, min(1,.78431*1.1+.39216*.01)) * 255 = (42, 103, 221).
    QTest::newRow("film-cool") << "film_emulation" << QVariantMap{{"preset", "cool"}, {"tone_strength", 100.0}, {"grain_amount", 0.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}} << p << qRgba(42, 103, 221, 128);
    // faded: lum = .2126*.15686+.7152*.39216+.0722*.78431 = .37045; lerp(c, lum+{.04,.03,-.01}, {.22,.18,.26}) -> (54, 100, 172).
    QTest::newRow("film-faded") << "film_emulation" << QVariantMap{{"preset", "faded"}, {"tone_strength", 100.0}, {"grain_amount", 0.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}} << p << qRgba(54, 100, 172, 128);
    // cross_processed: (r^.88*1.08, g^1.05*.98, min(1,b^.82*1.12)) * 255 = (54, 94, 234).
    QTest::newRow("film-crossprocessed") << "film_emulation" << QVariantMap{{"preset", "cross_processed"}, {"tone_strength", 100.0}, {"grain_amount", 0.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}} << p << qRgba(54, 94, 234, 128);
    // fade 100: scale 1-.35 = .65, lift .06 -> c*.65+.06: 40/255 -> (.15686*.65+.06)*255 = 41.0.
    QTest::newRow("film-fade") << "film_emulation" << QVariantMap{{"tone_strength", 0.0}, {"grain_amount", 0.0}, {"fade_amount", 100.0}, {"contrast_amount", 100.0}} << p << qRgba(41, 80, 145, 128);
    // contrast 150%: (c-.5)*1.5+.5 -> (.15686-.5)*1.5+.5 = -.0147 -> 0; g .39216 -> .3382 -> 86; b .78431 -> .9265 -> 236.
    QTest::newRow("film-contrast") << "film_emulation" << QVariantMap{{"tone_strength", 0.0}, {"grain_amount", 0.0}, {"fade_amount", 0.0}, {"contrast_amount", 150.0}} << p << qRgba(0, 86, 236, 128);
    // film grain at (0,0), seed 1977, mid grey: FNV-style hash n = (.40909, .39430, .91761)*2-1, amplitude .1 at grain 100:
    // 128/255 + n*.1 = (.50196-.01818, .50196-.02114, .50196+.08352) -> (123, 123, 149).
    QTest::newRow("film-grain") << "film_emulation" << QVariantMap{{"tone_strength", 0.0}, {"grain_amount", 100.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}} << qRgba(128, 128, 128, 255) << qRgba(123, 123, 149, 255);
    // gamma: table[i] = (byte)(pow(i/255, 1/amount) * 255), truncated: amount 2 -> 40:100, 100:159, 200:225.
    QTest::newRow("gamma-2") << "gamma" << QVariantMap{{"amount", 2.0}} << qRgba(40, 100, 200, 128) << qRgba(100, 159, 225, 128);
    // amount .5 -> pow(x, 2): 40:6, 100:39, 200:156.
    QTest::newRow("gamma-half") << "gamma" << QVariantMap{{"amount", 0.5}} << qRgba(40, 100, 200, 128) << qRgba(6, 39, 156, 128);
    // hue 180: c = -1, s ~ 0. R = -.574*40 + 1.43*100 + .144*200 = 148.84; G = .426*40 + .43*100 + .144*200 = 88.84; B = .426*40 + 1.43*100 - .856*200 < 0.
    QTest::newRow("hue-180") << "hue" << QVariantMap{{"amount", 180.0}} << p << qRgba(149, 89, 0, 128);
    // levels: in 50..200 -> (v-50)/150 clamped, out 0..255: 40 -> 0, 100 -> 85, 200 -> 255. Gamma 2: 100 -> (1/3)^2*255 = 28.3.
    QTest::newRow("levels-range") << "levels" << QVariantMap{{"input_black", 50}, {"input_white", 200}} << p << qRgba(0, 85, 255, 128);
    QTest::newRow("levels-gamma") << "levels" << QVariantMap{{"input_black", 50}, {"input_white", 200}, {"gamma", 2.0}} << p << qRgba(0, 28, 255, 128);
    // output 100..200: out = 100 + n*100: 40/255 -> n=.15686 -> 115.7; 100 -> 139.2; 200 -> 178.4.
    QTest::newRow("levels-output") << "levels" << QVariantMap{{"output_black", 100}, {"output_white", 200}} << p << qRgba(116, 139, 178, 128);
    // polaroid: R = 1.438*40 - .062*100 - .062*200 = 38.92.
    QTest::newRow("polaroid") << "polaroid" << QVariantMap{} << p << qRgba(39, 109, 255, 128);  // R = 57.52-6.2-12.4 = 38.92; G = -4.88+137.8-24.4 = 108.52; B clips.
    // posterize levels 4: bucket = round(v*3/255); mapped = bucket*255/3: 40 -> 0, 100 -> 1 -> 85, 200 -> 2 -> 170.
    QTest::newRow("posterize-4") << "posterize" << QVariantMap{{"levels", 4}} << p << qRgba(0, 85, 170, 128);
    QTest::newRow("posterize-2") << "posterize" << QVariantMap{{"levels", 2}} << p << qRgba(0, 0, 255, 128);
    // replace_color: tolerance 10 -> (int)(10*2.55f) = 25; every channel incl. alpha must be within it. Replacement keeps its own alpha.
    QTest::newRow("replace-hit") << "replace_color" << QVariantMap{{"target_color", QColor(60, 100, 200, 128)}, {"replace_color", QColor(1, 2, 3, 4)}, {"tolerance", 10.0}} << p << qRgba(1, 2, 3, 4);
    QTest::newRow("replace-miss") << "replace_color" << QVariantMap{{"target_color", QColor(70, 100, 200, 128)}, {"replace_color", QColor(1, 2, 3, 4)}, {"tolerance", 10.0}} << p << p;
    QTest::newRow("replace-alpha-miss") << "replace_color" << QVariantMap{{"target_color", QColor(40, 100, 200, 255)}, {"replace_color", QColor(1, 2, 3, 4)}, {"tolerance", 10.0}} << p << p;
    // sepia 100: R = .393*40+.769*100+.189*200 = 130.42; G = 13.96+68.6+33.6 = 116.16; B = 10.88+53.4+26.2 = 90.48.
    QTest::newRow("sepia") << "sepia" << QVariantMap{} << p << qRgba(130, 116, 90, 128);
    // sepia 50: halfway between identity and sepia: (85.2, 108.1, 145.2).
    QTest::newRow("sepia-half") << "sepia" << QVariantMap{{"strength", 50.0}} << p << qRgba(85, 108, 145, 128);
    // shadows_highlights: luma .37045; shadow weight (1-luma)^2 = .39633; highlight weight luma^2 = .13723.
    // shadows +50: delta = .5*.39633*255 = 50.53 -> (91, 151, 251).
    QTest::newRow("shadows-up") << "shadows_highlights" << QVariantMap{{"shadows", 50.0}} << p << qRgba(91, 151, 251, 128);
    // highlights +50: delta = -.5*.13723*255 = -17.5 -> (23, 83, 183).
    QTest::newRow("highlights-up") << "shadows_highlights" << QVariantMap{{"highlights", 50.0}} << p << qRgba(23, 83, 183, 128);
    // solarize 128: channels above the threshold invert: 200 -> 55.
    QTest::newRow("solarize") << "solarize" << QVariantMap{} << p << qRgba(40, 100, 55, 128);
    QTest::newRow("solarize-boundary") << "solarize" << QVariantMap{} << qRgba(128, 129, 255, 9) << qRgba(128, 126, 0, 9);
    // split_toning defaults (strength 30), lum .39..: computed from soft light (1-2t)c^2 + 2tc blended 100%: strength 100.
    QTest::newRow("split-full") << "split_toning" << QVariantMap{{"strength", 100.0}} << p << qRgba(8, 43, 164, 128);
    QTest::newRow("split-half") << "split_toning" << QVariantMap{{"strength", 50.0}} << p << qRgba(24, 72, 182, 128);
    // temperature +50: delta 32 -> R+32, B-32; tint +50: delta 32 -> G+32, R,B -8.
    QTest::newRow("temperature") << "temperature_tint" << QVariantMap{{"temperature", 50.0}} << p << qRgba(72, 100, 168, 128);
    QTest::newRow("temp-tint") << "temperature_tint" << QVariantMap{{"temperature", 50.0}, {"tint", 50.0}} << p << qRgba(64, 132, 160, 128);
    // threshold: luma = (40*77 + 100*150 + 200*29) >> 8 = 23880 >> 8 = 93.
    QTest::newRow("threshold-above") << "threshold" << QVariantMap{{"value", 93}} << p << qRgba(255, 255, 255, 128);
    QTest::newRow("threshold-below") << "threshold" << QVariantMap{{"value", 94}} << p << qRgba(0, 0, 0, 128);
    // vibrance +50: sat .6275, gray 113.33, factor 1+.5*(1-.6275) = 1.1863 -> (26, 98, 216); -50: factor .5 -> (77, 107, 157).
    QTest::newRow("vibrance-up") << "vibrance" << QVariantMap{{"amount", 50.0}} << p << qRgba(26, 98, 216, 128);
    QTest::newRow("vibrance-down") << "vibrance" << QVariantMap{{"amount", -50.0}} << p << qRgba(77, 107, 157, 128);
  }
  void handComputedPixels() {
    QFETCH(QString, id);
    QFETCH(QVariantMap, params);
    QFETCH(QRgb, input);
    QFETCH(QRgb, expected);
    const auto result = run(id, solid(1, 1, input), params);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image.pixel(0, 0), expected);
  }

  void lomoVignetteAndMatrix() {
    // 3x3 of (100,100,100): matrix R = 130+5-5+5.1 = 135.1, G = -2+110-2+2.55 = 108.55, B = -5+90-5.1 = 79.9 -> (135, 109, 80).
    // Edge-centre pixels are inside half the radius (1/2.1213 = .47): no vignette.
    // Corner: t = sqrt(2)/2.1213 = .6667, u = (t-.5)/.5 = .3333, black alpha 180/255*u = .2353 -> channels * .7647 = (103, 83, 61).
    const auto result = run("lomo", solid(3, 3, qRgba(100, 100, 100, 255)));
    QVERIFY(result.ok());
    QCOMPARE(result.image.pixel(1, 1), qRgba(135, 109, 80, 255));
    QCOMPARE(result.image.pixel(1, 0), qRgba(135, 109, 80, 255));
    QCOMPARE(result.image.pixel(0, 0), qRgba(103, 83, 61, 255));
    QCOMPARE(result.image.pixel(2, 2), qRgba(103, 83, 61, 255));
    // The vignette draws opaque-ish black even on transparent corners (source-over of black onto the layer).
    const auto transparent = run("lomo", solid(3, 3, qRgba(0, 0, 0, 0)));
    QVERIFY(transparent.ok());
    QCOMPARE(transparent.image.pixel(1, 1), qRgba(0, 0, 0, 0));
    QCOMPARE(qAlpha(transparent.image.pixel(0, 0)), 60);  // 180 * .3333
    // Strength scales the vignette alpha: (byte)(180*.5) = 90 -> .3529*.3333 = .1176.
    const auto half = run("lomo", solid(3, 3, qRgba(0, 0, 0, 0)), {{"strength", 50.0}});
    QCOMPARE(qAlpha(half.image.pixel(0, 0)), 30);
  }

  void autoContrastStretchesPerChannel() {
    // Red 50/100/150/200 with clip 0: min 50, max 200 -> 0, (50*255/150) = 85, 170, 255. Green/blue are flat and untouched.
    const QImage image = row({qRgba(50, 9, 9, 77), qRgba(100, 9, 9, 77), qRgba(150, 9, 9, 77), qRgba(200, 9, 9, 77)});
    const auto result = run("auto_contrast", image, {{"clip_percent", 0.0}});
    QVERIFY(result.ok());
    QCOMPARE(result.image.pixel(0, 0), qRgba(0, 9, 9, 77));
    QCOMPARE(result.image.pixel(1, 0), qRgba(85, 9, 9, 77));
    QCOMPARE(result.image.pixel(2, 0), qRgba(170, 9, 9, 77));
    QCOMPARE(result.image.pixel(3, 0), qRgba(255, 9, 9, 77));
    // clip 20% (the maximum): clipCount = round(4*.2) = 1 -> min 100 (cumulative 2 > 1), max 150: 50,100 -> 0; 150,200 -> 255.
    const auto clipped = run("auto_contrast", image, {{"clip_percent", 20.0}});
    QVERIFY(clipped.ok());
    QCOMPARE(clipped.image.pixel(0, 0), qRgba(0, 9, 9, 77));
    QCOMPARE(clipped.image.pixel(1, 0), qRgba(0, 9, 9, 77));
    QCOMPARE(clipped.image.pixel(2, 0), qRgba(255, 9, 9, 77));
    QCOMPARE(clipped.image.pixel(3, 0), qRgba(255, 9, 9, 77));
    // A flat image has nothing to stretch.
    const auto flat = solid(4, 4, qRgba(10, 20, 30, 200));
    QCOMPARE(run("auto_contrast", flat).image, flat);
  }

  void dehazeUsesBrightestPixelsAsAtmosphericLight() {
    // 2 pixels: topN = max(1, 2/1000) = 1 -> atmospheric light = (200,200,200).
    const QImage image = row({qRgba(200, 200, 200, 90), qRgba(100, 100, 100, 91)});
    // amount .5: pixel 2: dark channel = 100/200 = .5, t = 1 - .5*.5 = .75, c = (100 - 200*.25)/.75 = 66.67 -> 67;
    // pixel 1: dark = 1, t = .5, c = (200 - 200*.5)/.5 = 200.
    const auto removed = run("dehaze", image, {{"amount", 50.0}});
    QVERIFY(removed.ok());
    QCOMPARE(removed.image.pixel(0, 0), qRgba(200, 200, 200, 90));
    QCOMPARE(removed.image.pixel(1, 0), qRgba(67, 67, 67, 91));
    // amount -.5 adds haze: 100 + (200-100)*.5 = 150.
    const auto added = run("dehaze", image, {{"amount", -50.0}});
    QCOMPARE(added.image.pixel(0, 0), qRgba(200, 200, 200, 90));
    QCOMPARE(added.image.pixel(1, 0), qRgba(150, 150, 150, 91));
    // Black image: atmospheric light 0 makes 0/0 NaN in the reference; the result must still be a valid black image.
    const auto black = run("dehaze", solid(8, 8, qRgba(0, 0, 0, 255)));
    QVERIFY(black.ok());
    QCOMPARE(black.image, solid(8, 8, qRgba(0, 0, 0, 255)));
  }

  void clarityBoostsEdgesAndLeavesFlatAreasAlone() {
    const QImage flat = solid(16, 16, qRgba(120, 130, 140, 255));
    const auto flatResult = run("clarity", flat, {{"amount", 100.0}});
    QVERIFY(flatResult.ok());
    QCOMPARE(flatResult.image, flat);

    QImage step(64, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 64; ++x) step.setPixel(x, y, x < 32 ? qRgba(100, 100, 100, 200) : qRgba(140, 140, 140, 200));
    const auto boosted = run("clarity", step, {{"amount", 100.0}});
    QVERIFY(boosted.ok());
    QVERIFY(qRed(boosted.image.pixel(31, 4)) < 100);   // dark side of the edge gets darker
    QVERIFY(qRed(boosted.image.pixel(32, 4)) > 140);   // bright side gets brighter
    QCOMPARE(qRed(boosted.image.pixel(2, 4)), 100);    // far from the edge: no detail
    QCOMPARE(qRed(boosted.image.pixel(61, 4)), 140);
    QCOMPARE(qAlpha(boosted.image.pixel(31, 4)), 200);  // alpha is untouched
    const auto softened = run("clarity", step, {{"amount", -100.0}});
    QVERIFY(qRed(softened.image.pixel(31, 4)) > 100);
    QVERIFY(qRed(softened.image.pixel(32, 4)) < 140);
  }

  void alphaBehaviour() {
    const auto image = fixture();
    struct Case { const char *id; ParamValues params; };
    const Case preserving[]{
      {"auto_contrast", {}}, {"channel_swap", {}}, {"clarity", {{"amount", 60.0}}},
      {"color_balance", {{"mid_cyan_red", 40.0}, {"shadow_yellow_blue", -30.0}}}, {"color_matrix", {{"rr", 0.5}}},
      {"colorize", {{"strength", 100.0}}}, {"cross_process", {}}, {"dehaze", {}}, {"duotone_gradient_map", {}},
      {"exposure", {{"amount", 1.0}}}, {"film_emulation", {}}, {"gamma", {{"amount", 2.0}}}, {"hue", {{"amount", 90.0}}},
      {"levels", {{"input_black", 20}}}, {"polaroid", {}}, {"posterize", {}}, {"sepia", {}},
      {"shadows_highlights", {{"shadows", 30.0}}}, {"solarize", {}}, {"split_toning", {{"strength", 80.0}}},
      {"temperature_tint", {{"temperature", 25.0}}}, {"threshold", {}}, {"vibrance", {{"amount", 40.0}}}};
    for (const auto &c : preserving) {
      const auto result = run(c.id, image, c.params);
      QVERIFY2(result.ok(), c.id);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
          QVERIFY2(qAlpha(result.image.pixel(x, y)) == qAlpha(image.pixel(x, y)), c.id);
    }
    // Reference-specific alpha: black_and_white writes opaque colours; replace_color writes the replacement's alpha;
    // a colour-matrix alpha row scales alpha (alpha 128 * .5 = 64).
    const auto bw = run("black_and_white", image);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) QCOMPARE(qAlpha(bw.image.pixel(x, y)), 255);
    const auto replaced = run("replace_color", image,
                              {{"target_color", QColor(40, 100, 200, 255)}, {"replace_color", QColor(9, 9, 9, 33)}, {"tolerance", 0.0}});
    QCOMPARE(replaced.image.pixel(1, 2), qRgba(9, 9, 9, 33));
    QCOMPARE(replaced.image.pixel(0, 0), image.pixel(0, 0));
    const auto scaled = run("color_matrix", image, {{"aa", 0.5}});
    QCOMPARE(qAlpha(scaled.image.pixel(0, 0)), 64);
    QCOMPARE(qAlpha(scaled.image.pixel(1, 0)), 128);
    // Partial colorize composites a tinted copy over the source, so transparent pixels stay transparent.
    const auto colorized = run("colorize", image, {{"strength", 50.0}});
    QCOMPARE(qAlpha(colorized.image.pixel(2, 0)), 0);
    QCOMPARE(qAlpha(colorized.image.pixel(1, 0)), 255);
  }

  void filmGrainUsesOnlyContextSeed() {
    const auto image = solid(4, 4, qRgba(128, 128, 128, 255));
    const ParamValues params{{"tone_strength", 0.0}, {"grain_amount", 100.0}, {"fade_amount", 0.0}, {"contrast_amount", 100.0}};
    EffectContext seeded;
    seeded.seed = 5;
    const auto a = run("film_emulation", image, params, seeded);
    const auto b = run("film_emulation", image, params, seeded);
    const auto c = run("film_emulation", image, params);
    QVERIFY(a.ok() && b.ok() && c.ok());
    QCOMPARE(a.image, b.image);  // deterministic for the same seed
    QVERIFY(a.image != c.image);  // a different seed gives different grain
    // Seed 5 -> hash seed 1977 ^ 5 = 1976; the grain is seed-derived rather than uniform.
    QCOMPARE(a.image.pixel(0, 0), qRgba(133, 110, 110, 255));
    QVERIFY(a.image.pixel(0, 0) != a.image.pixel(1, 0));
  }

  void gammaIdentityTableAndContractOnLargerImage() {
    QImage ramp(256, 1, QImage::Format_ARGB32);
    for (int x = 0; x < 256; ++x) ramp.setPixel(x, 0, qRgba(x, 255 - x, x, 255));
    QCOMPARE(run("gamma", ramp).image, ramp);  // gamma 1.0: (byte)(i/255 * 255) is exact for every level
    QCOMPARE(run("levels", ramp).image, ramp);
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
      QVERIFY2(result.ok(), qPrintable(id));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.size(), image.size());
      QCOMPARE(result.image.colorSpace(), image.colorSpace());
      QCOMPARE(image, fixture());  // the source is never modified
      const auto plain = run(id, untagged);
      QVERIFY(plain.ok());
      QVERIFY(!plain.image.colorSpace().isValid());
      // Non-ARGB32 sources are normalised by runEffect.
      QVERIFY(run(id, image.convertToFormat(QImage::Format_ARGB32_Premultiplied)).ok());
      // Deterministic: the same input and parameters give the same image.
      QCOMPARE(run(id, image).image, result.image);
    }
    // Unknown and invalid parameters are typed errors from the framework.
    QVERIFY(!run("sepia", image, {{"strength", 101.0}}).ok());
    QVERIFY(!run("channel_swap", image, {{"mode", "nope"}}).ok());
    QVERIFY(!run("exposure", image, {{"unknown", 1.0}}).ok());
  }

  void everyEffectCancelsWithoutPartialImage() {
    const auto image = fixture();
    const QVector<QPair<QString, ParamValues>> cases = [&] {
      QVector<QPair<QString, ParamValues>> list;
      for (const QString &id : kIds) list.append({id, {}});
      // Defaults are neutral for some effects; also cover the real multi-pass paths.
      list.append({"clarity", {{"amount", 50.0}}});
      list.append({"exposure", {{"amount", 1.0}}});
      list.append({"film_emulation", {{"grain_amount", 50.0}}});
      return list;
    }();
    for (const auto &entry : cases) {
      const auto *effect = builtinEffects().find(entry.first);
      const auto resolved = resolveParameters(effect->spec(), entry.second);
      QVERIFY(resolved.ok());
      int progress = 0;
      int lastProgress = -1;
      bool monotonic = true;
      EffectContext context{[&] { return progress >= 33; }, [&](int value) {
        monotonic = monotonic && value >= lastProgress && value >= 0 && value <= 100;
        progress = lastProgress = value;
      }};
      const auto direct = effect->apply(image, resolved.values, context);
      QVERIFY2(direct.error == "effect-cancelled", qPrintable(entry.first));
      QVERIFY(direct.image.isNull());
      QVERIFY2(progress >= 33 && progress < 100 && monotonic, qPrintable(entry.first));
      progress = 0;
      lastProgress = -1;
      const auto viaRun = runEffect(*effect, image, entry.second, context);
      QCOMPARE(viaRun.error, QStringLiteral("effect-cancelled"));
      QVERIFY(viaRun.image.isNull());
      QCOMPARE(image, fixture());
      const auto immediate = effect->apply(image, resolved.values, {[] { return true; }});
      QCOMPARE(immediate.error, QStringLiteral("effect-cancelled"));
      QVERIFY(immediate.image.isNull());
      progress = 0;
      lastProgress = -1;
      context.cancelled = [] { return false; };
      QVERIFY2(runEffect(*effect, image, entry.second, context).ok(), qPrintable(entry.first));
      QVERIFY2(progress == 100 && monotonic, qPrintable(entry.first));
    }
  }

  void largerImagesComplete() {
    // Exercise the multi-pass paths (histogram, atmospheric light, three-box blur) beyond the 3x3 fixture.
    QImage image(97, 53, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) image.setPixel(x, y, qRgba((x * 5) & 255, (y * 9) & 255, (x * y) & 255, 255 - (x & 7)));
    for (const QString &id : {"auto_contrast", "clarity", "dehaze", "lomo", "film_emulation"}) {
      const auto params = id == "clarity" ? ParamValues{{"amount", 80.0}} : ParamValues{};
      const auto result = run(id, image, params);
      QVERIFY2(result.ok(), qPrintable(id));
      QCOMPARE(result.image.size(), image.size());
    }
  }
};
QTEST_GUILESS_MAIN(AdjustmentEffects2Test)
#include "AdjustmentEffects2Test.moc"
