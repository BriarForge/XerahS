// EDITOR-EFFECTS-001: EE-003, EE-009, EE-010, EE-016, EE-018.
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QTest>
#include <vector>

using namespace xerahs::effects;
namespace {
const QStringList kManipulations{"flip", "flip_horizontal", "flip_vertical", "rotate_180",
                                 "rotate_90_clockwise", "rotate_90_counter_clockwise", "rounded_corners"};

// A 3x2 image with six distinct pixels:
//   A B C
//   D E F
QImage fixture() {
  QImage image(3, 2, QImage::Format_ARGB32);
  const QRgb pixels[]{qRgba(10, 0, 0, 255), qRgba(20, 0, 0, 255), qRgba(30, 0, 0, 255),
                      qRgba(40, 0, 0, 255), qRgba(50, 0, 0, 255), qRgba(60, 0, 0, 255)};
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 3; ++x) image.setPixel(x, y, pixels[y * 3 + x]);
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}

EffectResult run(const QString &id, const QImage &image, const ParamValues &params = {},
                  const EffectContext &context = {}) {
  const auto *effect = builtinEffects().find(id);
  if (!effect) return {{}, "missing-effect"};
  return runEffect(*effect, image, params, context);
}

void expectPixels(const QImage &image, const std::vector<QRgb> &expected) {
  QVERIFY(image.height() > 0);
  QCOMPARE(image.width(), int(expected.size()) / image.height());
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      QCOMPARE(image.pixel(x, y), expected[size_t(y * image.width() + x)]);
}

std::vector<QRgb> pixelsOf(const QImage &image) {
  std::vector<QRgb> pixels;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) pixels.push_back(image.pixel(x, y));
  return pixels;
}
}
class ManipulationEffectsTest : public QObject {
  Q_OBJECT
private slots:
  void referenceMetadataMatchesCensus() {
    struct Expected { QString id, name, reference; Mode mode; bool changesSize; };
    const Expected expected[]{
      {QStringLiteral("flip"), QStringLiteral("Flip"), QStringLiteral("FlipImageEffect.cs"), Mode::Previewable, false},
      {QStringLiteral("flip_horizontal"), QStringLiteral("Flip horizontal"), QStringLiteral("FlipHorizontalImageEffect.cs"), Mode::Immediate, false},
      {QStringLiteral("flip_vertical"), QStringLiteral("Flip vertical"), QStringLiteral("FlipVerticalImageEffect.cs"), Mode::Immediate, false},
      {QStringLiteral("rotate_180"), QStringLiteral("Rotate 180°"), QStringLiteral("Rotate180ImageEffect.cs"), Mode::Immediate, false},
      {QStringLiteral("rotate_90_clockwise"), QStringLiteral("Rotate 90° clockwise"), QStringLiteral("Rotate90CWImageEffect.cs"), Mode::Immediate, true},
      {QStringLiteral("rotate_90_counter_clockwise"), QStringLiteral("Rotate 90° counter clockwise"), QStringLiteral("Rotate90CCWImageEffect.cs"), Mode::Immediate, true},
      {QStringLiteral("rounded_corners"), QStringLiteral("Rounded Corners"), QStringLiteral("RoundedCornersImageEffect.cs"), Mode::Previewable, false}};
    QCOMPARE(size_t(std::size(expected)), kManipulations.size());
    for (const auto &e : expected) {
      const auto *effect = builtinEffects().find(e.id);
      QVERIFY2(effect, qPrintable(e.id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.displayName, e.name);
      QCOMPARE(spec.category, Category::Manipulations);
      QCOMPARE(spec.mode, e.mode);
      QCOMPARE(spec.changesSize, e.changesSize);
      QCOMPARE(spec.usesSeed, false);
      QCOMPARE(spec.referencePath, QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Manipulations/") + e.reference);
    }
  }

  void referenceDefaultsAndRanges() {
    const auto *flip = builtinEffects().find("flip");
    QVERIFY(flip);
    QCOMPARE(flip->spec().parameters.size(), 2);
    QCOMPARE(flip->spec().parameters[0].id, QStringLiteral("horizontally"));
    QCOMPARE(flip->spec().parameters[0].kind, ParamKind::Bool);
    QCOMPARE(flip->spec().parameters[0].label, QStringLiteral("Horizontally"));
    QCOMPARE(flip->spec().parameters[0].defaultValue.toBool(), false);
    QCOMPARE(flip->spec().parameters[1].id, QStringLiteral("vertically"));
    QCOMPARE(flip->spec().parameters[1].kind, ParamKind::Bool);
    QCOMPARE(flip->spec().parameters[1].label, QStringLiteral("Vertically"));
    QCOMPARE(flip->spec().parameters[1].defaultValue.toBool(), false);
    const auto resolved = resolveParameters(flip->spec(), {});
    QVERIFY(resolved.ok());
    QCOMPARE(resolved.values.value("horizontally").toBool(), false);
    QCOMPARE(resolved.values.value("vertically").toBool(), false);
    QVERIFY(!resolveParameters(flip->spec(), {{"horizontally", 1}}).ok());
    QVERIFY(!resolveParameters(flip->spec(), {{"unknown", true}}).ok());

    const auto *rounded = builtinEffects().find("rounded_corners");
    QVERIFY(rounded);
    QCOMPARE(rounded->spec().parameters.size(), 1);
    const auto &radius = rounded->spec().parameters.front();
    QCOMPARE(radius.id, QStringLiteral("corner_radius"));
    QCOMPARE(radius.kind, ParamKind::Int);
    QCOMPARE(radius.label, QStringLiteral("Corner radius"));
    QCOMPARE(radius.defaultValue.toInt(), 20);
    QCOMPARE(radius.minimum, 0.0);
    QCOMPARE(radius.maximum, 500.0);
    QVERIFY(resolveParameters(rounded->spec(), {{"corner_radius", 0}}).ok());
    QVERIFY(resolveParameters(rounded->spec(), {{"corner_radius", 500}}).ok());
    QVERIFY(!resolveParameters(rounded->spec(), {{"corner_radius", -1}}).ok());
    QVERIFY(!resolveParameters(rounded->spec(), {{"corner_radius", 501}}).ok());

    for (const QString &id : kManipulations) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      QVERIFY2(resolveParameters(effect->spec(), {}).ok(), qPrintable(id));
      for (const auto &p : effect->spec().parameters) {
        if (p.kind != ParamKind::Float && p.kind != ParamKind::Int) continue;
        QVERIFY2(p.defaultValue.toDouble() >= p.minimum && p.defaultValue.toDouble() <= p.maximum,
                 qPrintable(id + "/" + p.id));
      }
    }
    for (const QString &id : {"flip_horizontal", "flip_vertical", "rotate_180", "rotate_90_clockwise",
                               "rotate_90_counter_clockwise"})
      QVERIFY(builtinEffects().find(id)->spec().parameters.isEmpty());
  }

  void flipsMoveEveryPixel() {
    const auto image = fixture();
    const QRgb a = image.pixel(0, 0), b = image.pixel(1, 0), c = image.pixel(2, 0);
    const QRgb d = image.pixel(0, 1), e = image.pixel(1, 1), f = image.pixel(2, 1);

    const auto horizontal = run("flip_horizontal", image);
    QVERIFY(horizontal.ok());
    QCOMPARE(horizontal.image.size(), image.size());
    expectPixels(horizontal.image, {c, b, a, f, e, d});

    const auto vertical = run("flip_vertical", image);
    QVERIFY(vertical.ok());
    QCOMPARE(vertical.image.size(), image.size());
    expectPixels(vertical.image, {d, e, f, a, b, c});

    const auto both = run("flip", image, {{"horizontally", true}, {"vertically", true}});
    QVERIFY(both.ok());
    expectPixels(both.image, {f, e, d, c, b, a});

    const auto horizontalOnly = run("flip", image, {{"horizontally", true}});
    QVERIFY(horizontalOnly.ok());
    expectPixels(horizontalOnly.image, {c, b, a, f, e, d});

    const auto verticalOnly = run("flip", image, {{"vertically", true}});
    QVERIFY(verticalOnly.ok());
    expectPixels(verticalOnly.image, {d, e, f, a, b, c});

    // The reference returns an unchanged copy when neither axis is selected.
    const auto neither = run("flip", image);
    QVERIFY(neither.ok());
    expectPixels(neither.image, {a, b, c, d, e, f});
  }

  void rotationsMoveEveryPixel() {
    const auto image = fixture();
    const QRgb a = image.pixel(0, 0), b = image.pixel(1, 0), c = image.pixel(2, 0);
    const QRgb d = image.pixel(0, 1), e = image.pixel(1, 1), f = image.pixel(2, 1);

    const auto half = run("rotate_180", image);
    QVERIFY(half.ok());
    QCOMPARE(half.image.size(), image.size());
    expectPixels(half.image, {f, e, d, c, b, a});

    const auto clockwise = run("rotate_90_clockwise", image);
    QVERIFY(clockwise.ok());
    QCOMPARE(clockwise.image.size(), QSize(2, 3));
    expectPixels(clockwise.image, {d, a, e, b, f, c});

    const auto counter = run("rotate_90_counter_clockwise", image);
    QVERIFY(counter.ok());
    QCOMPARE(counter.image.size(), QSize(2, 3));
    expectPixels(counter.image, {c, f, b, e, a, d});
  }

  void quarterTurnsRoundTrip() {
    const auto image = fixture();
    const auto clockwise = run("rotate_90_clockwise", image);
    QVERIFY(clockwise.ok());
    const auto back = run("rotate_90_counter_clockwise", clockwise.image);
    QVERIFY(back.ok());
    QCOMPARE(back.image.size(), image.size());
    expectPixels(back.image, pixelsOf(image));

    const auto counter = run("rotate_90_counter_clockwise", image);
    QVERIFY(counter.ok());
    const auto forward = run("rotate_90_clockwise", counter.image);
    QVERIFY(forward.ok());
    QCOMPARE(forward.image.size(), image.size());
    expectPixels(forward.image, pixelsOf(image));
  }

  void halfTurnRoundTrip() {
    const auto image = fixture();
    const auto once = run("rotate_180", image);
    QVERIFY(once.ok());
    const auto twice = run("rotate_180", once.image);
    QVERIFY(twice.ok());
    QCOMPARE(twice.image.size(), image.size());
    expectPixels(twice.image, pixelsOf(image));
  }

  void roundedCornersClearTheCorners() {
    QImage image(20, 20, QImage::Format_ARGB32);
    image.fill(qRgba(10, 20, 30, 255));
    image.setColorSpace(QColorSpace::DisplayP3);

    const auto rounded = run("rounded_corners", image);
    QVERIFY(rounded.ok());
    QCOMPARE(rounded.image.size(), image.size());
    QCOMPARE(qAlpha(rounded.image.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(rounded.image.pixel(19, 0)), 0);
    QCOMPARE(qAlpha(rounded.image.pixel(0, 19)), 0);
    QCOMPARE(qAlpha(rounded.image.pixel(19, 19)), 0);
    QCOMPARE(qAlpha(rounded.image.pixel(10, 10)), 255);
    QCOMPARE(qRed(rounded.image.pixel(10, 10)), 10);
    QCOMPARE(qGreen(rounded.image.pixel(10, 10)), 20);
    QCOMPARE(qBlue(rounded.image.pixel(10, 10)), 30);

    // A smaller radius keeps the corners transparent and anti-aliases the arc.
    const auto smaller = run("rounded_corners", image, {{"corner_radius", 5}});
    QVERIFY(smaller.ok());
    QCOMPARE(qAlpha(smaller.image.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(smaller.image.pixel(10, 10)), 255);
    bool antiAliased = false;
    for (int y = 0; y < smaller.image.height() && !antiAliased; ++y)
      for (int x = 0; x < smaller.image.width(); ++x) {
        const int alpha = qAlpha(smaller.image.pixel(x, y));
        if (alpha > 0 && alpha < 255) {
          antiAliased = true;
          break;
        }
      }
    QVERIFY(antiAliased);

    // A zero radius leaves every pixel untouched.
    const auto none = run("rounded_corners", image, {{"corner_radius", 0}});
    QVERIFY(none.ok());
    expectPixels(none.image, pixelsOf(image));

    // A radius larger than the image scales down to the shape that fits.
    QImage tall(20, 40, QImage::Format_ARGB32);
    tall.fill(qRgba(10, 20, 30, 255));
    const auto scaled = run("rounded_corners", tall, {{"corner_radius", 20}});
    QVERIFY(scaled.ok());
    QCOMPARE(scaled.image.size(), tall.size());
    QCOMPARE(qAlpha(scaled.image.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(scaled.image.pixel(19, 39)), 0);
    QCOMPARE(qAlpha(scaled.image.pixel(10, 20)), 255);
  }

  void everyEffectCancels() {
    const auto image = fixture();
    for (const QString &id : kManipulations) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto resolved = resolveParameters(effect->spec(), {});
      QVERIFY(resolved.ok());
      const auto cancelled = effect->apply(image, resolved.values, {[] { return true; }});
      QCOMPARE(cancelled.error, QStringLiteral("effect-cancelled"));
      QVERIFY(cancelled.image.isNull());
      const auto result = runEffect(*effect, image, {}, {[] { return true; }});
      QCOMPARE(result.error, QStringLiteral("effect-cancelled"));
      QVERIFY(result.image.isNull());
      QCOMPARE(image, fixture());
    }
  }

  void cancellationStopsAtTheNextRow() {
    const auto image = fixture();
    const auto *effect = builtinEffects().find("rotate_90_clockwise");
    QVERIFY(effect);
    int progress = 0;
    int lastProgress = -1;
    bool monotonic = true;
    EffectContext context{[&] { return progress >= 50; }, [&](int value) {
      monotonic = monotonic && value >= lastProgress && value >= 0 && value <= 100;
      progress = lastProgress = value;
    }};
    const auto result = runEffect(*effect, image, {}, context);
    QCOMPARE(result.error, QStringLiteral("effect-cancelled"));
    QVERIFY(result.image.isNull());
    QVERIFY(progress >= 50 && progress < 100);
    QVERIFY(monotonic);
    QCOMPARE(image, fixture());
  }

  void runEffectAcceptsEveryOutput() {
    const auto image = fixture();
    for (const QString &id : kManipulations) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto result = runEffect(*effect, image, {}, {});
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.colorSpace(), image.colorSpace());
      if (effect->spec().changesSize)
        QCOMPARE(result.image.size(), QSize(image.height(), image.width()));
      else
        QCOMPARE(result.image.size(), image.size());
    }
  }

  void untaggedSourceKeepsItsUntaggedColourSpace() {
    auto image = fixture();
    image.setColorSpace({});
    for (const QString &id : kManipulations) {
      const auto result = run(id, image);
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QVERIFY(!result.image.colorSpace().isValid());
    }
  }
};
QTEST_GUILESS_MAIN(ManipulationEffectsTest)
#include "ManipulationEffectsTest.moc"
