// EDITOR-EFFECTS-001: the Drawings category. EE-003, EE-007, EE-008..EE-010, EE-016, EE-018.
// Text tests assert regions (changed inside, untouched outside), never glyph pixels, so they hold across fonts.
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::effects;
namespace {
const QStringList kIds{"border", "candy_cane_border", "confetti", "draw_background", "draw_background_image",
                       "draw_checkerboard", "draw_image", "draw_line", "draw_particles", "draw_shape", "draw_text",
                       "fake_cursor", "fake_hair", "gradient_overlay", "snowflakes", "text_watermark"};

QImage solid(int w, int h, QRgb color) {
  QImage image(w, h, QImage::Format_ARGB32);
  image.fill(color);
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}
QImage gray(int w = 64, int h = 48) { return solid(w, h, qRgba(128, 128, 128, 255)); }
EffectResult run(const QString &id, const QImage &image, const ParamValues &params = {}, const EffectContext &context = {}) {
  const auto *effect = builtinEffects().find(id);
  return effect ? runEffect(*effect, image, params, context) : EffectResult{{}, "missing"};
}
EffectContext seeded(quint64 seed) {
  EffectContext context;
  context.seed = seed;
  return context;
}
QRect changedBounds(const QImage &a, const QImage &b) {
  QRect box;
  if (a.size() != b.size()) return box;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (a.pixel(x, y) != b.pixel(x, y)) box = box.isNull() ? QRect(x, y, 1, 1) : box.united(QRect(x, y, 1, 1));
  return box;
}
bool same(const QImage &a, const QImage &b) { return a.size() == b.size() && a == b; }

bool writeImage(const QString &path, const QImage &image) { return image.save(path, "PNG"); }
QImage twoByOne(QRgb left, QRgb right) {
  QImage image(2, 1, QImage::Format_ARGB32);
  image.setPixel(0, 0, left);
  image.setPixel(1, 0, right);
  return image;
}
const QRgb kRed = qRgba(255, 0, 0, 255), kBlue = qRgba(0, 0, 255, 255), kGreen = qRgba(0, 255, 0, 255);
}

class DrawingEffectsTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;
  QString file(const QString &name) const { return m_dir.filePath(name); }
private slots:
  void initTestCase() {
    QVERIFY(m_dir.isValid());
    QVERIFY(writeImage(file("red8.png"), solid(8, 8, kRed)));
    QVERIFY(writeImage(file("green8.png"), solid(8, 8, kGreen)));
    QVERIFY(writeImage(file("rb.png"), twoByOne(kRed, kBlue)));
    QVERIFY(writeImage(file("huge.png"), solid(16385, 1, kRed)));
    QFile bad(file("not-an-image.png"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("this is not a png");
    bad.close();
    QVERIFY(QDir().mkpath(file("particles")));
    QVERIFY(writeImage(file("particles/a.png"), solid(8, 8, kRed)));
    QVERIFY(writeImage(file("particles/b.PNG"), solid(6, 6, kBlue)));
    QVERIFY(QDir().mkpath(file("empty")));
    QVERIFY(QDir().mkpath(file("corrupt")));
    QVERIFY(QFile::copy(file("not-an-image.png"), file("corrupt/x.png")));
    QVERIFY(writeImage(file("corrupt/ok.png"), solid(4, 4, kRed)));
  }

  void sixteenRegisteredWithReferencesAndValidDefaults() {
    QCOMPARE(kIds.size(), 16);
    const QStringList seeded{"confetti", "draw_particles", "snowflakes", "fake_hair", "fake_cursor"};
    for (const QString &id : kIds) {
      const auto *effect = builtinEffects().find(id);
      QVERIFY2(effect, qPrintable(id));
      const auto &spec = effect->spec();
      QCOMPARE(spec.id, id);
      QCOMPARE(spec.category, Category::Drawings);
      QCOMPARE(spec.mode, Mode::Previewable);
      QVERIFY(spec.referencePath.startsWith("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/"));
      QVERIFY(spec.referencePath.endsWith(".cs"));
      QCOMPARE(spec.usesSeed, seeded.contains(id));
      QCOMPARE(spec.changesSize, id == "border" || id == "candy_cane_border");
      QVERIFY2(resolveParameters(spec, {}).ok(), qPrintable(id));
      for (const auto &p : spec.parameters) {
        if (p.kind == ParamKind::Float || p.kind == ParamKind::Int) {
          QVERIFY2(p.defaultValue.toDouble() >= p.minimum && p.defaultValue.toDouble() <= p.maximum, qPrintable(id + "/" + p.id));
          QVERIFY(!resolveParameters(spec, {{p.id, p.minimum - 1}}).ok());
          QVERIFY(!resolveParameters(spec, {{p.id, p.maximum + 1}}).ok());
        } else if (p.kind == ParamKind::Enum) {
          QVERIFY(p.choices.contains(p.defaultValue.toString()));
          QVERIFY(!resolveParameters(spec, {{p.id, QString("no-such-choice")}}).ok());
        } else if (p.kind == ParamKind::Color) {
          QVERIFY(p.defaultValue.value<QColor>().isValid());
        } else if (p.kind == ParamKind::FilePath) {
          QVERIFY(p.defaultValue.toString().isEmpty());
        }
      }
    }
  }

  void outputContractAndCancellation_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariantMap>("params");
    QTest::addColumn<int>("growth");
    const QVariantMap none;
    QTest::newRow("border") << "border" << none << 10;
    QTest::newRow("candy_cane_border") << "candy_cane_border" << none << 20;
    QTest::newRow("confetti") << "confetti" << none << 0;
    QTest::newRow("draw_background") << "draw_background" << none << 0;
    QTest::newRow("draw_background_image") << "draw_background_image" << QVariantMap{{"image_file_path", file("red8.png")}} << 0;
    QTest::newRow("draw_checkerboard") << "draw_checkerboard" << none << 0;
    QTest::newRow("draw_image") << "draw_image" << QVariantMap{{"image_location", file("red8.png")}} << 0;
    QTest::newRow("draw_line") << "draw_line" << none << 0;
    QTest::newRow("draw_particles") << "draw_particles" << QVariantMap{{"image_folder", file("particles")}} << 0;
    QTest::newRow("draw_shape") << "draw_shape" << none << 0;
    QTest::newRow("draw_text") << "draw_text" << none << 0;
    QTest::newRow("fake_cursor") << "fake_cursor" << none << 0;
    QTest::newRow("fake_hair") << "fake_hair" << none << 0;
    QTest::newRow("gradient_overlay") << "gradient_overlay" << none << 0;
    QTest::newRow("snowflakes") << "snowflakes" << none << 0;
    QTest::newRow("text_watermark") << "text_watermark" << none << 0;
  }
  void outputContractAndCancellation() {
    QFETCH(QString, id);
    QFETCH(QVariantMap, params);
    QFETCH(int, growth);
    ParamValues values;
    for (auto it = params.cbegin(); it != params.cend(); ++it) values.insert(it.key(), it.value());
    // Transparent and semi-transparent sources must also satisfy the pixel contract.
    for (const QImage &source : {gray(), solid(40, 30, qRgba(10, 20, 30, 0)), solid(40, 30, qRgba(200, 100, 50, 77))}) {
      const auto result = run(id, source, values, seeded(7));
      QVERIFY2(result.ok(), qPrintable(id + ": " + result.error));
      QCOMPARE(result.image.format(), QImage::Format_ARGB32);
      QCOMPARE(result.image.size(), source.size() + QSize(growth, growth));
      QCOMPARE(result.image.colorSpace(), source.colorSpace());
    }
    EffectContext cancelled;
    cancelled.cancelled = [] { return true; };
    const auto stopped = run(id, gray(), values, cancelled);
    QCOMPARE(stopped.error, QString("effect-cancelled"));
    QVERIFY(stopped.image.isNull());
    // Cancelling part-way through the work also stops it.
    int polls = 0;
    EffectContext midway;
    midway.cancelled = [&polls] { return ++polls > 2; };
    QCOMPARE(run(id, gray(), values, midway).error, QString("effect-cancelled"));
  }

  void borderOutsideAddsSolidMargin() {
    const auto result = run("border", gray(), {{"size", 5}});
    QVERIFY(result.ok());
    QCOMPARE(result.image.size(), QSize(74, 58));
    for (const QPoint p : {QPoint(0, 0), QPoint(2, 30), QPoint(73, 57), QPoint(40, 4), QPoint(40, 53)})
      QCOMPARE(result.image.pixel(p), qRgba(0, 0, 0, 255));
    QCOMPARE(result.image.pixel(6, 6), qRgba(128, 128, 128, 255));
    QCOMPARE(result.image.pixel(37, 29), qRgba(128, 128, 128, 255));
    QCOMPARE(result.image.pixel(5, 5), qRgba(128, 128, 128, 255));
  }
  void borderInsideKeepsSizeAndPaintsEdge() {
    const auto result = run("border", gray(), {{"type", "inside"}, {"size", 4}, {"color", QColor(255, 0, 0)}});
    QVERIFY(result.ok());
    QCOMPARE(result.image.size(), QSize(64, 48));
    QCOMPARE(result.image.pixel(1, 20), kRed);
    QCOMPARE(result.image.pixel(62, 20), kRed);
    QCOMPARE(result.image.pixel(30, 1), kRed);
    QCOMPARE(result.image.pixel(30, 46), kRed);
    QCOMPARE(result.image.pixel(30, 24), qRgba(128, 128, 128, 255));
    QCOMPARE(result.image.pixel(4, 24), qRgba(128, 128, 128, 255));
  }
  void borderDashStylesLeaveGaps() {
    const auto solidResult = run("border", gray(), {{"size", 4}});
    const auto dashed = run("border", gray(), {{"size", 4}, {"dash_style", "dash"}});
    const auto dotted = run("border", gray(), {{"size", 4}, {"dash_style", "dot"}});
    QVERIFY(solidResult.ok() && dashed.ok() && dotted.ok());
    auto transparentInTopMargin = [](const QImage &image) {
      int count = 0;
      for (int x = 4; x < image.width() - 4; ++x) count += qAlpha(image.pixel(x, 1)) < 255;
      return count;
    };
    QCOMPARE(transparentInTopMargin(solidResult.image), 0);
    QVERIFY(transparentInTopMargin(dashed.image) > 0);
    QVERIFY(transparentInTopMargin(dotted.image) > transparentInTopMargin(dashed.image) / 2);
    QVERIFY(!same(dashed.image, dotted.image));
  }

  void candyCaneAddsStripedBorderAroundUntouchedImage() {
    const QImage source = gray();
    const auto result = run("candy_cane_border", source, {{"border_width", 8}, {"rounded_corners", false}});
    QVERIFY(result.ok());
    QCOMPARE(result.image.size(), QSize(80, 64));
    QCOMPARE(result.image.copy(8, 8, 64, 48), source);
    bool reddish = false, whitish = false;
    for (int x = 8; x < 72; ++x) {
      const QRgb p = result.image.pixel(x, 4);
      QCOMPARE(qAlpha(p), 255);
      reddish = reddish || (qRed(p) > 150 && qGreen(p) < 80);
      whitish = whitish || (qRed(p) > 200 && qGreen(p) > 200 && qBlue(p) > 200);
    }
    QVERIFY(reddish && whitish);
    QCOMPARE(qAlpha(result.image.pixel(0, 0)), 255);
  }
  void candyCaneRoundedCornersCutOuterCorner() {
    const auto rounded = run("candy_cane_border", gray(), {{"border_width", 20}, {"rounded_corners", true}});
    const auto square = run("candy_cane_border", gray(), {{"border_width", 20}, {"rounded_corners", false}});
    QVERIFY(rounded.ok() && square.ok());
    QCOMPARE(qAlpha(rounded.image.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(square.image.pixel(0, 0)), 255);
    QCOMPARE(qAlpha(rounded.image.pixel(20, 3)), 255);
  }
  void candyCaneGlossAndColoursAreHonoured() {
    const auto flat = run("candy_cane_border", gray(), {{"gloss", 0.0}, {"rounded_corners", false}});
    const auto glossy = run("candy_cane_border", gray(), {{"gloss", 100.0}, {"rounded_corners", false}});
    QVERIFY(!same(flat.image, glossy.image));
    const auto custom = run("candy_cane_border", gray(), {{"color_a", QColor(0, 0, 255)}, {"color_b", QColor(0, 0, 255)},
                                                          {"gloss", 0.0}, {"rounded_corners", false}});
    QVERIFY(custom.ok());
    QCOMPARE(custom.image.pixel(30, 5), kBlue);
  }

  void drawBackgroundShowsThroughTransparency() {
    const auto onClear = run("draw_background", solid(8, 8, 0), {{"color", QColor(10, 20, 30)}});
    QVERIFY(onClear.ok());
    QCOMPARE(onClear.image.pixel(3, 3), qRgba(10, 20, 30, 255));
    const QImage opaque = gray();
    QCOMPARE(run("draw_background", opaque).image, opaque);
    const auto half = run("draw_background", solid(4, 4, qRgba(255, 255, 255, 128)), {{"color", QColor(0, 0, 0)}});
    QVERIFY(half.ok());
    QCOMPARE(qAlpha(half.image.pixel(0, 0)), 255);
    QVERIFY(std::abs(qRed(half.image.pixel(0, 0)) - 128) <= 1);
  }
  void checkerboardAlternatesCells() {
    const auto result = run("draw_checkerboard", solid(16, 16, 0),
                            {{"size", 4}, {"color", QColor(255, 0, 0)}, {"color2", QColor(0, 0, 255)}});
    QVERIFY(result.ok());
    QCOMPARE(result.image.pixel(0, 0), kRed);
    QCOMPARE(result.image.pixel(3, 3), kRed);
    QCOMPARE(result.image.pixel(4, 0), kBlue);
    QCOMPARE(result.image.pixel(0, 4), kBlue);
    QCOMPARE(result.image.pixel(4, 4), kRed);
    QCOMPARE(result.image.pixel(8, 0), kRed);
    const QImage opaque = gray();
    QCOMPARE(run("draw_checkerboard", opaque).image, opaque);
    QVERIFY(!same(run("draw_checkerboard", solid(40, 40, 0)).image, solid(40, 40, 0)));
  }

  void backgroundImageCoversTilesAndRejectsBadFiles() {
    const auto cover = run("draw_background_image", solid(20, 10, 0), {{"image_file_path", file("red8.png")}});
    QVERIFY(cover.ok());
    for (const QPoint p : {QPoint(0, 0), QPoint(19, 9), QPoint(10, 5)}) QCOMPARE(cover.image.pixel(p), kRed);
    const auto tiled = run("draw_background_image", solid(4, 1, 0),
                           {{"image_file_path", file("rb.png")}, {"tile", true}, {"center", false}});
    QVERIFY(tiled.ok());
    QCOMPARE(tiled.image.pixel(0, 0), kRed);
    QCOMPARE(tiled.image.pixel(1, 0), kBlue);
    QCOMPARE(tiled.image.pixel(2, 0), kRed);
    QCOMPARE(tiled.image.pixel(3, 0), kBlue);
    const QImage opaque = gray();  // the source is drawn over the background
    QCOMPARE(run("draw_background_image", opaque, {{"image_file_path", file("red8.png")}}).image, opaque);
    for (const QString &bad : {QString(), file("missing.png"), file("not-an-image.png"), file("huge.png"), m_dir.path()}) {
      const auto result = run("draw_background_image", gray(), {{"image_file_path", bad}});
      QCOMPARE(result.error, QString("effect-invalid-input"));
      QVERIFY(result.image.isNull());
    }
  }

  void drawImagePlacementOffsetAndBounds() {
    const QImage source = gray();
    const auto topLeft = run("draw_image", source, {{"image_location", file("green8.png")}});
    QVERIFY(topLeft.ok());
    QCOMPARE(changedBounds(source, topLeft.image), QRect(0, 0, 8, 8));
    QCOMPARE(topLeft.image.pixel(3, 3), kGreen);
    const auto corner = run("draw_image", source, {{"image_location", file("green8.png")}, {"placement", "bottom_right"},
                                                  {"offset_x", 2}, {"offset_y", 3}});
    QCOMPARE(changedBounds(source, corner.image), QRect(64 - 8 - 2, 48 - 8 - 3, 8, 8));
    const auto middle = run("draw_image", source, {{"image_location", file("green8.png")}, {"placement", "middle_center"}});
    QCOMPARE(changedBounds(source, middle.image), QRect(28, 20, 8, 8));
  }
  void drawImageSizeModesOpacityAndAutoHide() {
    const QImage source = gray();
    const auto absolute = run("draw_image", source, {{"image_location", file("green8.png")}, {"size_mode", "absolute_size"},
                                                    {"size_width", 16}, {"size_height", 0}});
    QCOMPARE(changedBounds(source, absolute.image), QRect(0, 0, 16, 16));
    const auto canvas = run("draw_image", source, {{"image_location", file("green8.png")}, {"size_mode", "percentage_of_canvas"},
                                                  {"size_width", 50}, {"size_height", 0}});
    QCOMPARE(changedBounds(source, canvas.image), QRect(0, 0, 32, 32));
    const auto degenerate = run("draw_image", source, {{"image_location", file("green8.png")}, {"size_mode", "absolute_size"}});
    QCOMPARE(degenerate.image, source);
    const auto faded = run("draw_image", source, {{"image_location", file("green8.png")}, {"opacity", 50}});
    const QRgb mixed = faded.image.pixel(2, 2);
    QVERIFY(qGreen(mixed) > 128 && qGreen(mixed) < 255 && qRed(mixed) > 0 && qRed(mixed) < 128);
    QCOMPARE(run("draw_image", source, {{"image_location", file("green8.png")}, {"opacity", 0}}).image, source);
    const auto hidden = run("draw_image", source, {{"image_location", file("green8.png")}, {"offset_x", -4}, {"auto_hide", true}});
    QCOMPARE(hidden.image, source);
    const auto clipped = run("draw_image", source, {{"image_location", file("green8.png")}, {"offset_x", -4}});
    QCOMPARE(changedBounds(source, clipped.image), QRect(0, 0, 4, 8));
    const auto tiled = run("draw_image", source, {{"image_location", file("rb.png")}, {"tile", true}, {"size_mode", "absolute_size"},
                                                 {"size_width", 6}, {"size_height", 1}});
    QCOMPARE(tiled.image.pixel(0, 0), kRed);
    QCOMPARE(tiled.image.pixel(1, 0), kBlue);
    QCOMPARE(tiled.image.pixel(4, 0), kRed);
    QCOMPARE(tiled.image.pixel(6, 0), qRgba(128, 128, 128, 255));
  }
  void drawImageFlipsKeepExactPixelsOnTheMinimumQtApi_data() {
    QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("expected"); QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
    QTest::newRow("horizontal") << QString("flip_x") << QString("214365") << 2 << 3;
    QTest::newRow("vertical") << QString("flip_y") << QString("563412") << 2 << 3;
    QTest::newRow("clockwise-then-horizontal") << QString("rotate_90_flip_x") << QString("135246") << 3 << 2;
    QTest::newRow("clockwise-then-vertical") << QString("rotate_90_flip_y") << QString("642531") << 3 << 2;
  }
  void drawImageFlipsKeepExactPixelsOnTheMinimumQtApi() {
    QFETCH(QString, mode); QFETCH(QString, expected); QFETCH(int, width); QFETCH(int, height);
    QImage asset(2, 3, QImage::Format_ARGB32); asset.setColorSpace(QColorSpace::DisplayP3);
    const auto colour = [](int value) { return qRgba(20 * value, 30 * value, 40 * value, 255); };
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x) asset.setPixel(x, y, colour(y * 2 + x + 1));
    const auto path = file("asymmetric-flips.png"); QVERIFY(asset.save(path, "PNG"));
    const auto source = gray(4, 4); const auto result = run("draw_image", source, {{"image_location", path}, {"rotate_flip", mode}, {"interpolation_mode", "nearest_neighbor"}});
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.image.size(), source.size()); QCOMPARE(result.image.colorSpace(), source.colorSpace());
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) QCOMPARE(result.image.pixel(x, y), colour(expected[y * width + x].digitValue()));
    QCOMPARE(result.image.pixel(3, 3), source.pixel(3, 3)); QCOMPARE(QImage(path), asset);
  }
  void drawImageRotateFlipAndCompositing() {
    const QImage source = gray();
    const auto flipped = run("draw_image", source, {{"image_location", file("rb.png")}, {"rotate_flip", "flip_x"}});
    QCOMPARE(flipped.image.pixel(0, 0), kBlue);
    QCOMPARE(flipped.image.pixel(1, 0), kRed);
    const auto rotated = run("draw_image", source, {{"image_location", file("rb.png")}, {"rotate_flip", "rotate_90"}});
    QCOMPARE(changedBounds(source, rotated.image), QRect(0, 0, 1, 2));
    QCOMPARE(rotated.image.pixel(0, 0), kRed);
    QCOMPARE(rotated.image.pixel(0, 1), kBlue);
    const auto upsideDown = run("draw_image", source, {{"image_location", file("rb.png")}, {"rotate_flip", "rotate_180"}});
    QCOMPARE(upsideDown.image.pixel(0, 0), kBlue);
    const auto copy = run("draw_image", solid(4, 4, qRgba(255, 255, 255, 255)),
                          {{"image_location", file("red8.png")}, {"compositing_mode", "source_copy"}});
    QCOMPARE(copy.image.pixel(1, 1), kRed);
  }
  void drawImageRejectsBadFiles() {
    for (const QString &bad : {QString(), file("missing.png"), file("not-an-image.png"), file("huge.png"), m_dir.path()}) {
      const auto result = run("draw_image", gray(), {{"image_location", bad}});
      QCOMPARE(result.error, QString("effect-invalid-input"));
      QVERIFY(result.image.isNull());
    }
  }

  void drawLineInsideAndOutsideRegions() {
    const QImage source = gray();
    const auto def = run("draw_line", source);
    QVERIFY(def.ok());
    QVERIFY(def.image.pixel(30, 0) != source.pixel(30, 0));
    QCOMPARE(def.image.pixel(30, 10), source.pixel(30, 10));
    const auto custom = run("draw_line", source, {{"start_x", 5}, {"start_y", 20}, {"end_x", 50}, {"end_y", 20}, {"thickness", 4.0},
                                                 {"color", QColor(255, 0, 0)}});
    QCOMPARE(custom.image.pixel(30, 20), kRed);
    QCOMPARE(custom.image.pixel(30, 19), kRed);
    QCOMPARE(custom.image.pixel(30, 24), source.pixel(30, 24));
    QCOMPARE(custom.image.pixel(60, 20), source.pixel(60, 20));
    const QRect box = changedBounds(source, custom.image);
    QVERIFY(QRect(2, 17, 51, 8).contains(box));
    QCOMPARE(run("draw_line", source, {{"color", QColor(255, 0, 0, 0)}}).image, source);
  }
  void drawShapeRegionsPerShape() {
    const QImage source = gray();
    const ParamValues base{{"offset_x", 10}, {"offset_y", 6}, {"size_width", 30}, {"size_height", 20}, {"color", QColor(255, 0, 0)}};
    auto shape = [&](const QString &name) {
      ParamValues values = base;
      values.insert("shape", name);
      const auto result = run("draw_shape", source, values);
      return result.image;
    };
    const QRect box(10, 6, 30, 20);
    const QImage rect = shape("rectangle");
    QCOMPARE(changedBounds(source, rect), box);
    QCOMPARE(rect.pixel(10, 6), kRed);
    QCOMPARE(rect.pixel(9, 6), source.pixel(9, 6));
    const QImage rounded = shape("rounded_rectangle");
    QCOMPARE(rounded.pixel(10, 6), source.pixel(10, 6));
    QCOMPARE(rounded.pixel(25, 16), kRed);
    const QImage ellipse = shape("ellipse");
    QCOMPARE(ellipse.pixel(10, 6), source.pixel(10, 6));
    QCOMPARE(ellipse.pixel(39, 25), source.pixel(39, 25));
    QCOMPARE(ellipse.pixel(25, 16), kRed);
    const QImage triangle = shape("triangle");
    QCOMPARE(triangle.pixel(10, 6), source.pixel(10, 6));
    QCOMPARE(triangle.pixel(39, 6), source.pixel(39, 6));
    QCOMPARE(triangle.pixel(25, 24), kRed);
    QCOMPARE(triangle.pixel(25, 10), kRed);
    const QImage diamond = shape("diamond");
    QCOMPARE(diamond.pixel(10, 6), source.pixel(10, 6));
    QCOMPARE(diamond.pixel(39, 25), source.pixel(39, 25));
    QCOMPARE(diamond.pixel(25, 16), kRed);
    QVERIFY(box.contains(changedBounds(source, diamond)));
    const auto full = run("draw_shape", source, {{"size_width", -1}, {"size_height", -1}, {"color", QColor(255, 0, 0)}});
    QCOMPARE(full.image.pixel(63, 47), kRed);
    const auto placed = run("draw_shape", source, {{"placement", "bottom_right"}, {"offset_x", 4}, {"offset_y", 2},
                                                  {"size_width", 10}, {"size_height", 10}});
    QCOMPARE(changedBounds(source, placed.image), QRect(64 - 10 - 4, 48 - 10 - 2, 10, 10));
    QCOMPARE(run("draw_shape", source, {{"size_width", 0}}).image, source);
  }
  void gradientOverlayBlendsAlongAngle() {
    const QImage source = gray(64, 16);
    QCOMPARE(run("gradient_overlay", source, {{"opacity", 0.0}}).image, source);
    const auto left = run("gradient_overlay", source, {{"color1", QColor(255, 0, 0)}, {"color2", QColor(0, 0, 255)},
                                                      {"opacity", 100.0}, {"angle", 0}});
    QVERIFY(left.ok());
    const QRgb a = left.image.pixel(1, 8), b = left.image.pixel(62, 8);
    QVERIFY(qRed(a) > qBlue(a));
    QVERIFY(qBlue(b) > qRed(b));
    QCOMPARE(left.image.pixel(1, 2), left.image.pixel(1, 12));  // angle 0 varies along x only
    const auto vertical = run("gradient_overlay", gray(16, 64), {{"color1", QColor(255, 0, 0)}, {"color2", QColor(0, 0, 255)},
                                                               {"opacity", 100.0}, {"angle", 90}});
    QVERIFY(qRed(vertical.image.pixel(8, 1)) > qBlue(vertical.image.pixel(8, 1)));
    QVERIFY(qBlue(vertical.image.pixel(8, 62)) > qRed(vertical.image.pixel(8, 62)));
    QVERIFY(!same(run("gradient_overlay", source).image, source));
    QCOMPARE(qAlpha(run("gradient_overlay", source).image.pixel(0, 0)), 255);
  }

  void seededEffectsAreDeterministicAndSeedSensitive_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariantMap>("params");
    QTest::newRow("confetti") << "confetti" << QVariantMap{};
    QTest::newRow("snowflakes") << "snowflakes" << QVariantMap{};
    QTest::newRow("fake_hair") << "fake_hair" << QVariantMap{{"count", 5}};
    QTest::newRow("fake_cursor") << "fake_cursor" << QVariantMap{{"count", 5}};
    QTest::newRow("draw_particles") << "draw_particles"
                                    << QVariantMap{{"image_folder", file("particles")}, {"image_count", 12}, {"random_angle", true},
                                                   {"random_size", true}, {"random_opacity", true}};
  }
  void seededEffectsAreDeterministicAndSeedSensitive() {
    QFETCH(QString, id);
    QFETCH(QVariantMap, params);
    ParamValues values;
    for (auto it = params.cbegin(); it != params.cend(); ++it) values.insert(it.key(), it.value());
    const QImage source = gray(120, 90);
    const auto first = run(id, source, values, seeded(42));
    const auto again = run(id, source, values, seeded(42));
    const auto other = run(id, source, values, seeded(43));
    QVERIFY2(first.ok() && again.ok() && other.ok(), qPrintable(id));
    QCOMPARE(first.image, again.image);
    QVERIFY2(!same(first.image, other.image), qPrintable(id));
    QVERIFY2(!same(first.image, source), qPrintable(id));
    QVERIFY(!same(run(id, source, values, seeded(0)).image, first.image));
  }
  void confettiAndSnowflakesRespectCountOpacityAndBackground() {
    const QImage source = gray(120, 90);
    for (const QString &id : {QString("confetti"), QString("snowflakes")}) {
      QCOMPARE(run(id, source, {{"opacity", 0.0}}, seeded(1)).image, source);
      const auto few = run(id, source, {{"count", 10}}, seeded(1));
      const auto many = run(id, source, {{"count", 400}}, seeded(1));
      int fewChanged = 0, manyChanged = 0;
      for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 120; ++x) {
          fewChanged += few.image.pixel(x, y) != source.pixel(x, y);
          manyChanged += many.image.pixel(x, y) != source.pixel(x, y);
        }
      QVERIFY2(manyChanged > fewChanged && fewChanged > 0, qPrintable(id));
      // With Background the pieces sit behind the (opaque) image, so nothing changes; behind a clear image they show.
      QCOMPARE(run(id, source, {{"background", true}}, seeded(1)).image, source);
      const QImage clear = solid(120, 90, 0);
      QVERIFY2(!same(run(id, clear, {{"background", true}}, seeded(1)).image, clear), qPrintable(id));
    }
  }
  void fakeHairAndCursorStayInsideTheCanvasAndHonourColour() {
    const QImage source = gray(120, 90);
    const auto hair = run("fake_hair", source, {{"count", 3}, {"hair_color", QColor(0, 0, 0)}}, seeded(9));
    QVERIFY(hair.ok() && !same(hair.image, source));
    const auto cursor = run("fake_cursor", source, {{"fill_color", QColor(255, 0, 0)}, {"border_color", QColor(0, 0, 255)}}, seeded(3));
    QVERIFY(cursor.ok());
    bool sawFill = false;
    for (int y = 0; y < 90; ++y)
      for (int x = 0; x < 120; ++x) sawFill = sawFill || cursor.image.pixel(x, y) == kRed;
    QVERIFY(sawFill);
    const auto big = run("fake_cursor", source, {{"scale", 5.0}}, seeded(3));
    const auto small = run("fake_cursor", source, {{"scale", 0.5}}, seeded(3));
    QVERIFY(changedBounds(source, big.image).width() > changedBounds(source, small.image).width());
    const auto rotated = run("fake_cursor", source, {{"random_angle", true}, {"random_angle_min", 90}, {"random_angle_max", 90}}, seeded(3));
    QVERIFY(!same(rotated.image, run("fake_cursor", source, {}, seeded(3)).image));
  }

  void particlesPlaceImagesAndRejectBadFolders() {
    const QImage source = gray(120, 90);
    const auto result = run("draw_particles", source, {{"image_folder", file("particles")}, {"image_count", 6}}, seeded(5));
    QVERIFY(result.ok());
    QVERIFY(!changedBounds(source, result.image).isNull());
    bool red = false, blue = false;
    for (int y = 0; y < 90; ++y)
      for (int x = 0; x < 120; ++x) {
        red = red || result.image.pixel(x, y) == kRed;
        blue = blue || result.image.pixel(x, y) == kBlue;
      }
    QVERIFY(red || blue);
    QCOMPARE(run("draw_particles", source, {{"image_folder", file("particles")}, {"background", true}}, seeded(5)).image, source);
    const auto noOverlap = run("draw_particles", source,
                               {{"image_folder", file("particles")}, {"image_count", 4}, {"no_overlap", true}, {"no_overlap_offset", 2}},
                               seeded(5));
    QVERIFY(noOverlap.ok());
    for (const QString &bad : {QString(), file("missing"), file("empty"), file("corrupt"), file("red8.png")}) {
      const auto failed = run("draw_particles", source, {{"image_folder", bad}}, seeded(5));
      QCOMPARE(failed.error, QString("effect-invalid-input"));
      QVERIFY(failed.image.isNull());
    }
  }

  void drawTextChangesOnlyTheExpectedRegion() {
    const QImage source = solid(200, 100, qRgba(30, 30, 30, 255));
    const auto topLeft = run("draw_text", source, {{"text", "Hi"}, {"font_size", 20.0}});
    QVERIFY(topLeft.ok());
    const QRect box = changedBounds(source, topLeft.image);
    QVERIFY(!box.isNull());
    QVERIFY2(QRect(0, 0, 70, 45).contains(box), qPrintable(QString("%1,%2 %3x%4").arg(box.x()).arg(box.y()).arg(box.width()).arg(box.height())));
    const auto bottomRight = run("draw_text", source, {{"text", "Hi"}, {"font_size", 20.0}, {"placement", "bottom_right"}});
    const QRect low = changedBounds(source, bottomRight.image);
    QVERIFY(!low.isNull() && low.left() >= 100 && low.top() >= 50 && low.right() < 200 && low.bottom() < 100);
    // An unavailable family is a declared fallback to the default font, not a failure.
    const auto fallback = run("draw_text", source, {{"text", "Hi"}, {"font_size", 20.0}, {"font_family", "No Such Font Family 123"}});
    QVERIFY(fallback.ok() && !same(fallback.image, source));
    QCOMPARE(run("draw_text", source, {{"text", "   "}}).image, source);
    QCOMPARE(run("draw_text", source, {{"text", "Hi"}, {"font_size", 400.0}, {"auto_hide", true}}).image, source);
    QCOMPARE(run("draw_text", source, {{"text", "Hi"}, {"color", QColor(235, 235, 235, 0)}}).image, source);
  }
  void drawTextOutlineShadowAndAngle() {
    const QImage source = solid(200, 100, qRgba(30, 30, 30, 255));
    const ParamValues base{{"text", "Hi"}, {"font_size", 20.0}, {"offset_x", 20}, {"offset_y", 20}};
    auto with = [&](ParamValues extra) {
      ParamValues values = base;
      for (auto it = extra.cbegin(); it != extra.cend(); ++it) values.insert(it.key(), it.value());
      return run("draw_text", source, values).image;
    };
    const QImage plain = with({});
    const QRect plainBox = changedBounds(source, plain);
    const QImage outlined = with({{"outline", true}, {"outline_size", 6}});
    QVERIFY(changedBounds(source, outlined).width() > plainBox.width());
    const QImage shadowed = with({{"shadow", true}, {"shadow_offset_x", 0}, {"shadow_offset_y", 25}});
    QVERIFY(changedBounds(source, shadowed).height() > plainBox.height());
    const QImage rotated = with({{"angle", 90}});
    QVERIFY(changedBounds(source, rotated).height() > changedBounds(source, rotated).width());
    const QRect rotatedBox = changedBounds(source, rotated);
    QVERIFY(rotatedBox.left() >= 20 - 1 && rotatedBox.top() >= 20 - 1);
    QCOMPARE(changedBounds(source, plain).topLeft().x() >= 19, true);
  }
  void textWatermarkDrawsBackgroundAtBottomRight() {
    const QImage source = solid(200, 100, qRgba(200, 200, 200, 255));
    const auto result = run("text_watermark", source);
    QVERIFY(result.ok());
    const QRect box = changedBounds(source, result.image);
    QVERIFY(!box.isNull());
    QVERIFY2(box.left() >= 40 && box.top() >= 60 && box.right() <= 199 && box.bottom() <= 99,
             qPrintable(QString("%1,%2 %3x%4").arg(box.x()).arg(box.y()).arg(box.width()).arg(box.height())));
    // Offset 5 from the bottom-right corner leaves a gap there; the dark background is inside the box.
    QCOMPARE(result.image.pixel(199, 99), source.pixel(199, 99));
    QCOMPARE(result.image.pixel(box.left() + 8, box.top() + 2), qRgba(42, 47, 56, 255));
    const auto bare = run("text_watermark", source, {{"draw_background", false}, {"draw_border", false}});
    const QRect bareBox = changedBounds(source, bare.image);
    QVERIFY(!bareBox.isNull() && box.contains(bareBox) && bareBox != box);
    const auto topLeft = run("text_watermark", source, {{"placement", "top_left"}, {"offset_x", 0}, {"offset_y", 0}});
    const QRect topBox = changedBounds(source, topLeft.image);
    QVERIFY(!topBox.isNull() && topBox.left() <= 1 && topBox.top() <= 1 && topBox.right() < 150);
  }
  void textWatermarkVariablesPaddingAndAutoHide() {
    const QImage source = solid(200, 100, qRgba(200, 200, 200, 255));
    QCOMPARE(run("text_watermark", source, {{"text", "%width"}}).image, run("text_watermark", source, {{"text", "200"}}).image);
    QCOMPARE(run("text_watermark", source, {{"text", "%height"}}).image, run("text_watermark", source, {{"text", "100"}}).image);
    QCOMPARE(run("text_watermark", source, {{"text", "  "}}).image, source);
    const auto narrow = run("text_watermark", source, {{"padding_left", 0}, {"padding_right", 0}});
    const auto wide = run("text_watermark", source, {{"padding_left", 20}, {"padding_right", 20}});
    QVERIFY(changedBounds(source, wide.image).width() > changedBounds(source, narrow.image).width());
    QCOMPARE(run("text_watermark", source, {{"font_size", 300.0}, {"auto_hide", true}}).image, source);
    QVERIFY(!same(run("text_watermark", source, {{"draw_text_shadow", true}}).image, run("text_watermark", source).image));
    QVERIFY(!same(run("text_watermark", source, {{"font_family", "No Such Font 123"}}).image, source));
  }
};

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  DrawingEffectsTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "DrawingEffectsTest.moc"
