// EDITOR-EFFECTS-001: EE-003, EE-006, EE-008..EE-010, EE-016, EE-018 (outline, shadow, scale).
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QTest>
#include <cmath>

using namespace xerahs::effects;
namespace {
const QStringList kIds{"outline", "shadow", "scale"};
// The reference resamples at the default percentages, so the default keeps the pixels; only a
// different percentage changes them.
const QStringList kMayLeaveFixtureUnchanged{"scale"};
const QStringList kSeeded{};
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
// A transparent image with one opaque pixel.
QImage singlePixel(int w, int h, int x, int y, QRgb color) {
  QImage image(w, h, QImage::Format_ARGB32);
  image.fill(0);
  image.setPixel(x, y, color);
  return image;
}
QImage solid(int w, int h, QRgb color) {
  QImage image(w, h, QImage::Format_ARGB32);
  image.fill(color);
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
}

class FilterEffects7Test : public QObject {
  Q_OBJECT
private slots:
  void allRegisteredWithReferenceAndValidDefaults() {
    QCOMPARE(kIds.size(), QSet<QString>(kIds.begin(), kIds.end()).size());
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      const QString category = id == "scale" ? QStringLiteral("Manipulations/") : QStringLiteral("Filters/");
      QCOMPARE(spec.category, id == "scale" ? Category::Manipulations : Category::Filters);
      QCOMPARE(spec.mode, Mode::Previewable);
      QCOMPARE(spec.changesSize, true);
      QCOMPARE(spec.usesSeed, kSeeded.contains(id));
      QVERIFY2(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/" + category),
               qPrintable(spec.referencePath));
      QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
      QVERIFY2(resolveParameters(spec, {}).ok(), qPrintable(id));
      for (const auto &p : spec.parameters) {
        const QString what = id + "/" + p.id;
        if (p.kind == ParamKind::Float || p.kind == ParamKind::Int) {
          QVERIFY2(p.defaultValue.toDouble() >= p.minimum && p.defaultValue.toDouble() <= p.maximum, qPrintable(what));
          QVERIFY2(!resolveParameters(spec, {{p.id, p.minimum - 1}}).ok(), qPrintable(what));
          QVERIFY2(!resolveParameters(spec, {{p.id, p.maximum + 1}}).ok(), qPrintable(what));
        } else if (p.kind == ParamKind::Enum) {
          QVERIFY2(p.choices.contains(p.defaultValue.toString()), qPrintable(what));
        } else if (p.kind == ParamKind::Color) {
          QVERIFY2(p.defaultValue.value<QColor>().isValid(), qPrintable(what));
        } else {
          QVERIFY(p.kind == ParamKind::Bool || p.kind == ParamKind::Text);
        }
      }
      QVERIFY(!resolveParameters(spec, {{"unknown", 1}}).ok());
    }
  }

  void defaultsRunWithContract() {
    const QImage source = fixture();
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      const auto result = run(id, source);
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.colorSpace(), source.colorSpace());
      if (!effect->spec().changesSize) QCOMPARE(result.image.size(), source.size());
    }
    // outline and shadow grow the canvas; scale keeps it at the default percentages.
    QCOMPARE(img("outline", source).size(), QSize(40 + 6, 32 + 6));
    // The shadow grows by the blur radius plus the offset on each side: 20 + 5 = 25 right and
    // bottom, 20 left and top.
    QCOMPARE(img("shadow", source).size(), QSize(40 + 20 + 25, 32 + 20 + 25));
    QCOMPARE(img("scale", source).size(), source.size());
  }

  void defaultsChangeTheImageWhereTheReferenceDoes() {
    const QImage source = fixture();
    // The outline adds a ring and the shadow a blurred copy, so both change the picture.
    QVERIFY(img("outline", source) != source);
    QVERIFY(img("shadow", source) != source);
    // The reference scales at the default percentages (100 %, 0 %), which is a 1:1 resample and
    // therefore the same pixels.
    QCOMPARE(img("scale", source), source);
  }

  void neutralSettingsGiveIdenticalImages() {
    const QImage source = fixture();
    // scale: 100 % wide, the height follows the aspect ratio, so nothing moves.
    QCOMPARE(img("scale", source, {{"width_percentage", 100.0}, {"height_percentage", 0.0}}), source);
    QCOMPARE(img("scale", source, {{"width_percentage", 100.0}, {"height_percentage", 100.0}}), source);
    // Both percentages zero is the reference's early return.
    QCOMPARE(img("scale", source, {{"width_percentage", 0.0}, {"height_percentage", 0.0}}), source);
    // shadow: no opacity and no canvas growth leaves the source untouched.
    QCOMPARE(img("shadow", source, {{"opacity", 0.0}, {"auto_resize", false}}), source);
    // outline has no neutral setting: the smallest outline still grows the canvas.
    QVERIFY(img("outline", source, {{"size", 1}, {"padding", 0}}) != source);
  }

  void sameInputAndSeedIsDeterministic() {
    const QImage source = fixture();
    for (const QString &id : kIds) QVERIFY2(img(id, source, {}, 7) == img(id, source, {}, 7), qPrintable(id));
  }

  void unseededEffectsDoNotDependOnTheSeed() {
    const QImage source = fixture();
    for (const QString &id : kIds)
      QVERIFY2(img(id, source, {}, 1) == img(id, source, {}, 2), qPrintable(id + " depends on the seed"));
  }

  void cancellationReturnsEffectCancelled() {
    const QImage source = fixture();
    for (const QString &id : kIds) {
      int polls = 0;
      EffectContext context;
      context.cancelled = [&] { return ++polls > 3; };
      const auto result = run(id, source, {}, context);
      QCOMPARE(result.error, QString("effect-cancelled"));
      QVERIFY(result.image.isNull());
      QVERIFY2(polls > 3, qPrintable(id));
    }
  }

  void invalidParametersAndEmptyInputAreRejected() {
    for (const QString &id : kIds) {
      QCOMPARE(run(id, QImage()).error, QString("effect-invalid-input"));
      QCOMPARE(run(id, fixture(), {{"no_such_parameter", 1}}).error, QString("effect-invalid-input"));
    }
    // Out-of-range values are refused rather than clamped.
    QCOMPARE(run("outline", fixture(), {{"size", 0}}).error, QString("effect-invalid-input"));
    QCOMPARE(run("outline", fixture(), {{"size", 51}}).error, QString("effect-invalid-input"));
    QCOMPARE(run("outline", fixture(), {{"padding", -1}}).error, QString("effect-invalid-input"));
    QCOMPARE(run("shadow", fixture(), {{"opacity", 101.0}}).error, QString("effect-invalid-input"));
    QCOMPARE(run("shadow", fixture(), {{"offset_x", 1001}}).error, QString("effect-invalid-input"));
    QCOMPARE(run("scale", fixture(), {{"width_percentage", 501.0}}).error, QString("effect-invalid-input"));
  }

  // ---- outline ----
  void outlineDrawsARingAroundTheSource() {
    // One opaque red pixel at (1,1) in a 4x4 transparent image, size 2, no padding.
    // The source is drawn at (size + padding) = (2,2), so the red pixel lands at (3,3).
    // The tint dilates the source alpha by size + padding = 2, so the ring covers the canvas
    // columns and rows 1..5 (the pixel grows by exactly two pixels in each direction).
    const QImage source = singlePixel(4, 4, 1, 1, qRgb(255, 0, 0));
    const QImage out = img("outline", source, {{"size", 2}, {"padding", 0}});
    QCOMPARE(out.size(), QSize(8, 8));
    QCOMPARE(px(out, 0, 0), qRgba(0, 0, 0, 0));      // outside the ring
    QCOMPARE(px(out, 1, 1), qRgba(0, 0, 0, 255));    // the ring is the outline colour
    QCOMPARE(px(out, 2, 2), qRgba(0, 0, 0, 255));    // inside the ring but over a transparent pixel
    QCOMPARE(px(out, 3, 3), qRgba(255, 0, 0, 255));  // the source is drawn over the ring
    QCOMPARE(px(out, 5, 5), qRgba(0, 0, 0, 255));    // the far edge of the ring
    QCOMPARE(px(out, 6, 6), qRgba(0, 0, 0, 0));
  }

  void outlinePaddingCarvesTheGap() {
    // size 2 and padding 1: the canvas grows by 3 on every side, the ring is the source alpha
    // dilated by 3 (canvas columns 1..7) minus the source alpha dilated by 1 (canvas columns
    // 3..5), so the ring is the columns 1..2 and 6..7 around the picture.
    const QImage source = singlePixel(4, 4, 1, 1, qRgb(255, 0, 0));
    const QImage out = img("outline", source, {{"size", 2}, {"padding", 1}});
    QCOMPARE(out.size(), QSize(10, 10));
    QCOMPARE(px(out, 2, 2), qRgba(0, 0, 0, 255));    // the ring
    QCOMPARE(px(out, 3, 3), qRgba(0, 0, 0, 0));      // the gap, over a transparent source pixel
    QCOMPARE(px(out, 4, 4), qRgba(255, 0, 0, 255));  // the gap, over the source pixel
    QCOMPARE(px(out, 6, 6), qRgba(0, 0, 0, 255));    // the ring on the far side
    QCOMPARE(px(out, 7, 7), qRgba(0, 0, 0, 255));
    QCOMPARE(px(out, 8, 8), qRgba(0, 0, 0, 0));
  }

  void outlineOnlyKeepsTheRingAndTheColourIsUsed() {
    const QImage source = singlePixel(4, 4, 1, 1, qRgb(255, 0, 0));
    const QImage ring = img("outline", source, {{"size", 2}, {"outline_only", true}});
    QCOMPARE(px(ring, 1, 1), qRgba(0, 0, 0, 255));
    QCOMPARE(px(ring, 3, 3), qRgba(0, 0, 0, 0));  // the source is erased, only the ring is left
    // The tint replaces the ring colour.
    const QImage blue = img("outline", source, {{"size", 2}, {"outline_only", true}, {"color", QColor(0, 0, 255)}});
    QCOMPARE(px(blue, 1, 1), qRgba(0, 0, 255, 255));
    // A half transparent tint scales the ring alpha: 128/255 of the dilated alpha.
    const QImage faint = img("outline", source, {{"size", 2}, {"outline_only", true}, {"color", QColor(0, 0, 0, 128)}});
    QCOMPARE(qAlpha(px(faint, 1, 1)), 128);
  }

  // ---- shadow ----
  void shadowOffsetsAnUnblurredCopy() {
    // One opaque red pixel at (1,1), size 0 (no blur), offset x 2, opacity 100 %.
    // The canvas grows by the offset on the right (2) and the shadow is the source alpha drawn
    // at (offset x, offset y) = (2, 0), so the shadow lands at (3, 1).
    const QImage source = singlePixel(4, 4, 1, 1, qRgb(255, 0, 0));
    const QImage out = img("shadow", source, {{"opacity", 100.0}, {"size", 0}, {"offset_x", 2}, {"offset_y", 0}});
    QCOMPARE(out.size(), QSize(6, 4));
    QCOMPARE(px(out, 1, 1), qRgba(255, 0, 0, 255));  // the source
    QCOMPARE(px(out, 3, 1), qRgba(0, 0, 0, 255));    // the shadow, tinted with the shadow colour
    QCOMPARE(px(out, 2, 1), qRgba(0, 0, 0, 0));      // between the source and the shadow
    QCOMPARE(px(out, 0, 0), qRgba(0, 0, 0, 0));
  }

  void shadowBlurSoftensTheEdge() {
    // An opaque 40x40 square with a size 20 shadow (sigma 10) and no offset: the canvas grows by
    // the blur radius on every side, the square stays at (20, 20) and the shadow fades out from
    // the picture edge.
    const QImage source = solid(40, 40, qRgb(0, 0, 0));
    const QImage out = img("shadow", source, {{"opacity", 100.0}, {"size", 20}, {"offset_x", 0}, {"offset_y", 0}});
    QCOMPARE(out.size(), QSize(80, 80));
    QCOMPARE(qAlpha(px(out, 30, 30)), 255);  // the picture itself
    const int edge = qAlpha(px(out, 60, 40));  // just outside the picture: about half covered
    const int further = qAlpha(px(out, 70, 40));  // ten pixels further out: much fainter
    QVERIFY(edge > 80);
    QVERIFY(edge < 180);
    QVERIFY(further < edge);
    QVERIFY(qAlpha(px(out, 75, 75)) < further);  // the corner is the faintest
  }

  void shadowOpacityAndAutoResizeAreHonoured() {
    const QImage source = fixture();
    // Without the canvas growth the shadow is clipped by the source size.
    const QImage clipped = img("shadow", source, {{"auto_resize", false}});
    QCOMPARE(clipped.size(), source.size());
    // An offset of -5 puts the shadow five pixels above the picture, where it is visible: the
    // opacity is the shadow alpha (100 % is 255, 50 % is 127).
    const QImage full = img("shadow", source, {{"opacity", 100.0}, {"size", 0}, {"offset_x", 0}, {"offset_y", -5}});
    const QImage half = img("shadow", source, {{"opacity", 50.0}, {"size", 0}, {"offset_x", 0}, {"offset_y", -5}});
    QCOMPARE(full.size(), QSize(40, 32 + 5));
    QCOMPARE(qAlpha(px(full, 20, 2)), 255);
    QCOMPARE(qAlpha(px(half, 20, 2)), 127);
    // The colour of the shadow is the parameter colour.
    const QImage red = img("shadow", source, {{"opacity", 100.0}, {"size", 0}, {"offset_x", 0}, {"offset_y", -5},
                                              {"color", QColor(255, 0, 0)}});
    QCOMPARE(qRed(px(red, 20, 2)), 255);
    QCOMPARE(qGreen(px(red, 20, 2)), 0);
  }

  // ---- scale ----
  void scaleResamplesWithTheCanvasBilinear() {
    // A 4x4 ramp scaled to 50 %: the canvas resample takes the source position (x + 0.5) / 0.5 -
    // 0.5 = 2x + 0.5, so every output pixel is the mean of four source pixels.
    QImage source(4, 4, QImage::Format_ARGB32);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) source.setPixel(x, y, qRgba(x * 60, y * 60, 0, 255));
    const QImage out = img("scale", source, {{"width_percentage", 50.0}, {"height_percentage", 0.0}});
    QCOMPARE(out.size(), QSize(2, 2));
    // (0,0): mean of r 0, 60, 0, 60 = 30 and g 0, 0, 60, 60 = 30.
    QCOMPARE(px(out, 0, 0), qRgba(30, 30, 0, 255));
    // (1,0): mean of r 120, 180, 120, 180 = 150 and g 0, 0, 60, 60 = 30.
    QCOMPARE(px(out, 1, 0), qRgba(150, 30, 0, 255));
    // (0,1): mean of r 0, 60, 0, 60 = 30 and g 120, 120, 180, 180 = 150.
    QCOMPARE(px(out, 0, 1), qRgba(30, 150, 0, 255));
    QCOMPARE(px(out, 1, 1), qRgba(150, 150, 0, 255));
  }

  void scaleDerivesTheMissingPercentage() {
    const QImage source = fixture(40, 32);
    // The height follows the width: 50 % of 40 is 20 wide, so the height is 20 * 32 / 40 = 16.
    QCOMPARE(img("scale", source, {{"width_percentage", 50.0}, {"height_percentage", 0.0}}).size(), QSize(20, 16));
    // The width follows the height: 50 % of 32 is 16 high, so the width is 16 * 40 / 32 = 20.
    QCOMPARE(img("scale", source, {{"width_percentage", 0.0}, {"height_percentage", 50.0}}).size(), QSize(20, 16));
    // Both percentages are used when both are given.
    QCOMPARE(img("scale", source, {{"width_percentage", 50.0}, {"height_percentage", 25.0}}).size(), QSize(20, 8));
    // Enlarging keeps the aspect ratio of the request.
    QCOMPARE(img("scale", source, {{"width_percentage", 200.0}, {"height_percentage", 0.0}}).size(), QSize(80, 64));
  }

  void scaleKeepsTheColourSpaceAndTheAlpha() {
    const QImage source = fixture();
    const QImage out = img("scale", source, {{"width_percentage", 50.0}, {"height_percentage", 50.0}});
    QCOMPARE(out.format(), QImage::Format_ARGB32);
    QCOMPARE(out.colorSpace(), source.colorSpace());
    QCOMPARE(out.size(), QSize(20, 16));
    // The fixture is fully opaque, so the resample stays opaque.
    QCOMPARE(qAlpha(px(out, 0, 0)), 255);
  }
};
QTEST_GUILESS_MAIN(FilterEffects7Test)
#include "FilterEffects7Test.moc"
