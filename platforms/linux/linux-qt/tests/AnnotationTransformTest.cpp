// EC-009, EC-011: every annotation type through every canvas transform, with
// exact geometry, inverse round trips, asset pixel mirroring and atomic
// refusal. The geometric oracle is independent of the production routine: a
// sample point of the object's own frame must land where the canvas map sends
// its previous world position.
#include "image-editor/AnnotationTransform.h"
#include "image-editor/ShapePainter.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QTest>

#include <cmath>

// QImage::flipped arrived in Qt 6.9; the 6.4.2 baseline (LINUX-TOOLCHAIN-001) only has mirrored().
[[maybe_unused]] static QImage flippedImage(const QImage &image, Qt::Orientations axes) {
  return image.mirrored(axes.testFlag(Qt::Horizontal), axes.testFlag(Qt::Vertical));
}


using namespace xerahs::editor;

namespace {

const QString kAsset = QStringLiteral("asset");
constexpr qint64 kW = 40, kH = 30;

QByteArray assetPng() {
  QImage image(4, 3, QImage::Format_ARGB32);
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x) image.setPixel(x, y, qRgba(40 + x * 50, 30 + y * 80, 200 - x * 20 - y * 10, 255 - x * 10));
  QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
  return bytes;
}

AnnotationDocument baseDocument() {
  AnnotationDocument d;
  d.canvasWidth = kW; d.canvasHeight = kH;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-11-03T00:00:00Z");
  d.sourceImagePng = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");
  d.embeddedImages = {{kAsset, assetPng()}};
  return d;
}

// One object per document type, each carrying every content field it uses.
RectangleAnnotation make(const QString &type, double rotation) {
  RectangleAnnotation a;
  a.id = QUuid::createUuid();
  a.start = {10, 6}; a.end = {30, 18};
  a.style = {0xFFFF0000, 0x8000FF00, 4, rotation, .75};
  a.extra = {{"vendor", "keep"}};
  a.content.type = type;
  if (type == shape::line) a.content.points = {{0, 0}, {1, .25}};
  if (type == shape::arrow) a.content.points = {{0, .5}, {.75, .25}};
  if (type == shape::freehand) a.content.points = {{0, 0}, {.25, 1}, {.5, .5}, {1, .75}};
  if (type == shape::speechBalloon) a.content.points = {{1.25, .5}};
  if (isTextShape(type)) { a.content.text = QStringLiteral("Hello"); a.content.fontSize = 20; a.content.textAlignment = QStringLiteral("right"); }
  if (type == shape::step) a.content.stepValue = 3;
  if (hasShapeStrength(type)) a.content.strength = 7;
  if (isAssetShape(type)) a.content.imageKey = kAsset;
  return a;
}

AnnotationDocument documentWith(const RectangleAnnotation &a) {
  auto d = baseDocument(); d.annotations = {a}; return d;
}

std::pair<double, double> cosSin(double degrees) {
  const double d = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
  if (d == 0) return {1, 0}; if (d == 90) return {0, 1}; if (d == 180) return {-1, 0}; if (d == 270) return {0, -1};
  const double r = d * std::acos(-1.0) / 180; return {std::cos(r), std::sin(r)};
}
// World position of a point given as fractions of the object's own box.
PointF world(const RectangleAnnotation &r, PointF fraction) {
  const double w = r.right() - r.left(), h = r.bottom() - r.top();
  const double lx = (fraction.x - .5) * w, ly = (fraction.y - .5) * h;
  const auto [c, s] = cosSin(r.style.rotationDegrees);
  return {r.left() / 2 + r.right() / 2 + c * lx - s * ly, r.top() / 2 + r.bottom() / 2 + s * lx + c * ly};
}
std::vector<PointF> samples(const RectangleAnnotation &r) {
  std::vector<PointF> out{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {.5, .5}, {.25, .75}};
  for (PointF p : r.content.points) out.push_back(p);
  return out;
}

struct Case { QString name; CanvasOperation op; bool exact; };
CanvasOperation action(CanvasAction a) { CanvasOperation op; op.action = a; return op; }
CanvasOperation resizeTo(qint64 w, qint64 h) {
  auto op = action(CanvasAction::ResizeImage); op.width = w; op.height = h; op.lockAspect = false; return op;
}
CanvasOperation custom(double degrees, bool expand) {
  auto op = action(CanvasAction::RotateCustom); op.rotationDegrees = degrees; op.expandCanvas = expand; return op;
}
std::vector<Case> cases() {
  return {{"cw", action(CanvasAction::RotateClockwise), true}, {"ccw", action(CanvasAction::RotateCounterClockwise), true},
          {"r180", action(CanvasAction::Rotate180), true}, {"flip-h", action(CanvasAction::FlipHorizontal), true},
          {"flip-v", action(CanvasAction::FlipVertical), true}, {"custom-90-expand", custom(90, true), true},
          {"custom-30-expand", custom(30, true), false}, {"custom-200-crop", custom(200, false), false},
          {"resize-2x", resizeTo(80, 60), true}, {"resize-half", resizeTo(20, 15), true},
          {"resize-nonuniform", resizeTo(80, 45), true}};
}

void compare(PointF actual, PointF expected, bool exact) {
  if (exact) { QCOMPARE(actual.x, expected.x); QCOMPARE(actual.y, expected.y); }
  else { QVERIFY2(std::abs(actual.x - expected.x) < 1e-9, qPrintable(QString::number(actual.x, 'g', 17) + " vs " + QString::number(expected.x, 'g', 17)));
         QVERIFY2(std::abs(actual.y - expected.y) < 1e-9, qPrintable(QString::number(actual.y, 'g', 17) + " vs " + QString::number(expected.y, 'g', 17))); }
}

bool sameShapes(const AnnotationDocument &a, const AnnotationDocument &b) {
  return serializeAnnotationList(a.annotations) == serializeAnnotationList(b.annotations);
}
QImage decoded(const QByteArray &png) { return QImage::fromData(png, "PNG").convertToFormat(QImage::Format_ARGB32); }

}  // namespace

class AnnotationTransformTest : public QObject {
  Q_OBJECT
private slots:
  void everyTypeEveryTransform_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<double>("rotation"); QTest::addColumn<int>("caseIndex");
    const auto all = cases();
    for (const QString &type : shapeTypes()) for (double rotation : {0.0, 90.0, 37.5}) for (std::size_t i = 0; i < all.size(); ++i)
      QTest::newRow(qPrintable(QString("%1/%2/%3").arg(type).arg(rotation).arg(all[i].name))) << type << rotation << int(i);
  }
  void everyTypeEveryTransform() {
    QFETCH(QString, type); QFETCH(double, rotation); QFETCH(int, caseIndex);
    const Case c = cases()[std::size_t(caseIndex)];
    const auto original = make(type, rotation);
    const auto d = documentWith(original);
    const auto result = applyCanvasOperation(d, solidImage(kW, kH, 0xFF336699), c.op);
    const bool nonUniform = c.name == "resize-nonuniform";
    QVERIFY2(!result.error, qPrintable(result.error.value_or(QString()))); QVERIFY(result.document && result.image);
    QCOMPARE(result.document->annotations.size(), std::size_t(1));
    const auto &moved = std::get<RectangleAnnotation>(result.document->annotations[0]);
    QCOMPARE(result.document->canvasWidth, result.image->width); QCOMPARE(result.document->canvasHeight, result.image->height);
    // Identity and content carry over untouched.
    QCOMPARE(moved.id, original.id); QCOMPARE(moved.type(), original.type()); QCOMPARE(moved.visible, original.visible);
    QCOMPARE(moved.extra, original.extra); QCOMPARE(moved.style.opacity, original.style.opacity);
    QCOMPARE(moved.style.strokeColor, original.style.strokeColor); QCOMPARE(moved.style.fillColor, original.style.fillColor);
    QCOMPARE(moved.content.text, original.content.text);
    QCOMPARE(moved.content.stepValue, original.content.stepValue); QCOMPARE(moved.content.strength, original.content.strength);
    QCOMPARE(moved.content.imageKey, original.content.imageKey); QCOMPARE(moved.content.textAlignment, original.content.textAlignment);
    QCOMPARE(moved.content.points.size(), original.content.points.size());
    // Reference-app scaling: stroke *= min(sx, sy), font size *= sy; strength is never rescaled.
    const bool resize = c.op.action == CanvasAction::ResizeImage;
    const double sx = !resize ? 1 : double(c.op.width) / kW, sy = !resize ? 1 : double(c.op.height) / kH;
    QCOMPARE(moved.style.strokeWidth, 4 * std::min(sx, sy));
    QCOMPARE(moved.content.fontSize, original.content.fontSize * sy);
    if (nonUniform && rotation == 37.5) {
      // No shear is stored: local extents scale by the world factors, rotation is kept.
      QCOMPARE(moved.style.rotationDegrees, 37.5);
      QCOMPARE(moved.right() - moved.left(), 20 * sx); QCOMPARE(moved.bottom() - moved.top(), 12 * sy);
      compare({moved.left() / 2 + moved.right() / 2, moved.top() / 2 + moved.bottom() / 2}, {20 * sx, 12 * sy}, true);
      for (std::size_t i = 0; i < original.content.points.size(); ++i) compare(moved.content.points[i], original.content.points[i], true);
      return;
    }
    // Oracle: the sample at the (possibly mirrored) local fraction is where the map put the old one.
    const auto t = [&](PointF p) { return result.transform.map(p); };
    const auto before = samples(original);
    // Which local axis, if any, carries the reflection: discover it from the corners.
    const PointF probe = t(world(original, {0, 0}));
    bool mirrorX = false, mirrorY = false;
    for (int mask = 0; mask < 4; ++mask) {
      const PointF corner{(mask & 1) ? 1.0 : 0.0, (mask & 2) ? 1.0 : 0.0};
      const PointF actual = world(moved, corner);
      if (std::abs(actual.x - probe.x) < 1e-9 && std::abs(actual.y - probe.y) < 1e-9) { mirrorX = mask & 1; mirrorY = mask & 2; break; }
    }
    // Only reflections may mirror, and only along one axis.
    const bool reflection = c.op.action == CanvasAction::FlipHorizontal || c.op.action == CanvasAction::FlipVertical;
    QVERIFY(reflection || (!mirrorX && !mirrorY)); QVERIFY(!(mirrorX && mirrorY));
    for (std::size_t i = 0; i < before.size(); ++i) {
      const PointF local = before[i];
      const PointF expectedWorld = t(world(original, local));
      const PointF image{mirrorX ? 1 - local.x : local.x, mirrorY ? 1 - local.y : local.y};
      compare(world(moved, image), expectedWorld, c.exact && rotation != 37.5);
    }
    // Path and tail points are stored mirrored in the object's own frame.
    for (std::size_t i = 0; i < original.content.points.size(); ++i) {
      const PointF p = original.content.points[i];
      compare(moved.content.points[i], {mirrorX ? 1 - p.x : p.x, mirrorY ? 1 - p.y : p.y}, true);
    }
    // Text-bearing objects are never turned upside down by a flip.
    if (reflection && isTextShape(type) && rotation == 0) QVERIFY(moved.style.rotationDegrees == 0);
    QVERIFY(std::signbit(moved.style.rotationDegrees) == false);
    QVERIFY(moved.style.rotationDegrees >= 0 && moved.style.rotationDegrees < 360);
    // Exact transforms of exact input stay on exact values (no drift).
    if (c.exact && rotation != 37.5 && !resize) {
      QCOMPARE(moved.right() - moved.left(), original.right() - original.left());
      QCOMPARE(moved.bottom() - moved.top(), original.bottom() - original.top());
      QCOMPARE(std::fmod(moved.style.rotationDegrees, 90.0), 0.0);
    }
    // The transformed document survives a .xann reload byte for byte.
    const auto bytes = writeXann(*result.document); QVERIFY(bytes);
    const auto reread = readXann(*bytes); QVERIFY(reread.document);
    QVERIFY(sameShapes(*reread.document, *result.document));
    QCOMPARE(reread.document->embeddedImages, result.document->embeddedImages);
  }

  void flipsGiveAnExactNamedRotationAndMirroredPoints() {
    // Hand-computed: flipping an arrow on a 40x30 canvas.
    auto arrow = make(shape::arrow, 0);
    auto h = applyCanvasOperation(documentWith(arrow), solidImage(kW, kH, 0), action(CanvasAction::FlipHorizontal));
    const auto &a = std::get<RectangleAnnotation>(h.document->annotations[0]);
    QCOMPARE(a.start, (PointF{10, 6})); QCOMPARE(a.end, (PointF{30, 18}));  // centred box maps onto itself
    QCOMPARE(a.content.points[0], (PointF{1, .5})); QCOMPARE(a.content.points[1], (PointF{.25, .25}));
    QCOMPARE(a.style.rotationDegrees, 0.0);
    arrow.style.rotationDegrees = 30;
    h = applyCanvasOperation(documentWith(arrow), solidImage(kW, kH, 0), action(CanvasAction::FlipVertical));
    const auto &v = std::get<RectangleAnnotation>(h.document->annotations[0]);
    QCOMPARE(v.start, (PointF{10, 12})); QCOMPARE(v.end, (PointF{30, 24}));
    QCOMPARE(v.style.rotationDegrees, 330.0);
    QCOMPARE(v.content.points[0], (PointF{0, .5})); QCOMPARE(v.content.points[1], (PointF{.75, .75}));
    // Quarter turn: centre (20,12) maps to (H-12, 20) = (18,20) on the 30x40 canvas.
    h = applyCanvasOperation(documentWith(make(shape::speechBalloon, 15)), solidImage(kW, kH, 0), action(CanvasAction::RotateClockwise));
    const auto &q = std::get<RectangleAnnotation>(h.document->annotations[0]);
    QCOMPARE(q.start, (PointF{18 - 10, 20 - 6})); QCOMPARE(q.end, (PointF{18 + 10, 20 + 6}));
    QCOMPARE(q.style.rotationDegrees, 105.0); QCOMPARE(q.content.points[0], (PointF{1.25, .5}));
  }

  void exactInversesRestoreTheOriginalDocument_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<double>("rotation");
    for (const QString &type : shapeTypes()) for (double rotation : {0.0, 90.0, 37.5, 300.0})
      QTest::newRow(qPrintable(QString("%1/%2").arg(type).arg(rotation))) << type << rotation;
  }
  void exactInversesRestoreTheOriginalDocument() {
    QFETCH(QString, type); QFETCH(double, rotation);
    const auto original = documentWith(make(type, rotation));
    const auto source = solidImage(kW, kH, 0xFF336699);
    const std::vector<std::vector<CanvasAction>> sequences{
        {CanvasAction::RotateClockwise, CanvasAction::RotateCounterClockwise},
        {CanvasAction::RotateCounterClockwise, CanvasAction::RotateClockwise},
        {CanvasAction::Rotate180, CanvasAction::Rotate180},
        {CanvasAction::FlipHorizontal, CanvasAction::FlipHorizontal},
        {CanvasAction::FlipVertical, CanvasAction::FlipVertical},
        {CanvasAction::RotateClockwise, CanvasAction::RotateClockwise, CanvasAction::RotateClockwise, CanvasAction::RotateClockwise}};
    for (const auto &sequence : sequences) {
      AnnotationDocument current = original; ArgbImage image = source;
      for (CanvasAction a : sequence) {
        const auto step = applyCanvasOperation(current, image, action(a));
        QVERIFY2(step.document && step.image, qPrintable(step.error.value_or(QString())));
        current = *step.document; image = *step.image;
      }
      QCOMPARE(current.canvasWidth, kW); QCOMPARE(current.canvasHeight, kH);
      QVERIFY2(sameShapes(current, original), qPrintable(QString::fromUtf8(QJsonDocument(serializeAnnotationList(current.annotations)).toJson(QJsonDocument::Compact))));
      QCOMPARE(image.pixels, source.pixels);
      // Mirroring is lossless: asset pixels equal the original after the pair.
      QCOMPARE(decoded(current.embeddedImages[0].second), decoded(original.embeddedImages[0].second));
    }
    // Resize by an exact factor and back restores exact geometry (2x then half).
    auto up = applyCanvasOperation(original, source, resizeTo(80, 60));
    if (up.document) {
      const auto back = applyCanvasOperation(*up.document, *up.image, resizeTo(40, 30));
      QVERIFY(back.document); QVERIFY(sameShapes(*back.document, original));
    } else QVERIFY(false);
  }

  void flipMirrorsDirectionalAssetPixelsOnTheRightAxis() {
    const QImage origin = decoded(assetPng());
    for (double rotation : {0.0, 90.0, 180.0, 270.0, 37.5}) for (CanvasAction flip : {CanvasAction::FlipHorizontal, CanvasAction::FlipVertical}) {
      const auto shapeAfter = [&](const QString &type) {
        const auto r = applyCanvasOperation(documentWith(make(type, rotation)), solidImage(kW, kH, 0), action(flip));
        return std::make_pair(std::get<RectangleAnnotation>(r.document->annotations[0]), r.document->embeddedImages[0].second);
      };
      for (const QString &type : {shape::image, shape::emoji, shape::cursor}) {
        const auto [moved, png] = shapeAfter(type);
        const auto before = make(type, rotation);
        // Derive the local reflection from the geometry: the asset's top-left
        // corner pixel must land where the map sent it.
        const CanvasTransform map = flip == CanvasAction::FlipHorizontal ? CanvasTransform{-1, 0, 0, 1, double(kW), 0} : CanvasTransform{1, 0, 0, -1, 0, double(kH)};
        const PointF target = map.map(world(before, {0, 0}));
        const bool mirroredX = std::abs(world(moved, {1, 0}).x - target.x) < 1e-9 && std::abs(world(moved, {1, 0}).y - target.y) < 1e-9;
        const bool mirroredY = std::abs(world(moved, {0, 1}).x - target.x) < 1e-9 && std::abs(world(moved, {0, 1}).y - target.y) < 1e-9;
        QVERIFY(mirroredX != mirroredY);
        QCOMPARE(decoded(png), flippedImage(origin, Qt::Orientations((mirroredX ? Qt::Horizontal : Qt::Orientation()) | (mirroredY ? Qt::Vertical : Qt::Orientation()))));
        QCOMPARE(png == assetPng(), false);
      }
    }
  }

  void sharedAndMissingAssetKeysAreMirroredOnceAndSafely() {
    auto d = baseDocument();
    auto a = make(shape::image, 0), b = make(shape::cursor, 0), missing = make(shape::emoji, 0);
    missing.content.imageKey = QStringLiteral("absent");
    d.annotations = {a, b, missing};
    d.embeddedImages.push_back({QStringLiteral("unused"), assetPng()});
    const auto r = applyCanvasOperation(d, solidImage(kW, kH, 0), action(CanvasAction::FlipHorizontal));
    QVERIFY(r.document);
    QCOMPARE(decoded(r.document->embeddedImages[0].second), flippedImage(decoded(assetPng()), Qt::Horizontal));
    QCOMPARE(r.document->embeddedImages[1].second, assetPng());  // unreferenced entries untouched
    QCOMPARE(r.document->embeddedImages.size(), std::size_t(2));
    QCOMPARE(std::get<RectangleAnnotation>(r.document->annotations[2]).content.imageKey, QStringLiteral("absent"));
    // A non-flip leaves every embedded byte alone.
    const auto turned = applyCanvasOperation(d, solidImage(kW, kH, 0), action(CanvasAction::RotateClockwise));
    QCOMPARE(turned.document->embeddedImages, d.embeddedImages);
  }

  void flippedRenderMatchesFlippedPixelsForDrawnTypes_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<double>("rotation"); QTest::addColumn<int>("action");
    for (const QString &type : {shape::rectangle, shape::ellipse, shape::line, shape::arrow, shape::freehand, shape::highlight, shape::image, shape::emoji, shape::cursor})
      for (double rotation : {0.0, 90.0, 37.5}) for (CanvasAction a : {CanvasAction::FlipHorizontal, CanvasAction::FlipVertical, CanvasAction::RotateClockwise, CanvasAction::RotateCounterClockwise, CanvasAction::Rotate180})
        QTest::newRow(qPrintable(QString("%1/%2/%3").arg(type).arg(rotation).arg(int(a)))) << type << rotation << int(a);
  }
  void flippedRenderMatchesFlippedPixelsForDrawnTypes() {
    QFETCH(QString, type); QFETCH(double, rotation); QFETCH(int, action);
    const auto d = documentWith(make(type, rotation));
    const ArgbImage source = solidImage(kW, kH, 0xFFDDEEAA);
    const auto before = renderDocument(source, d); QVERIFY(before.image);
    CanvasOperation op; op.action = CanvasAction(action);
    const auto changed = applyCanvasOperation(d, source, op); QVERIFY(changed.document && changed.image);
    const auto after = renderDocument(*changed.image, *changed.document); QVERIFY(after.image);
    int worst = 0, outliers = 0;
    for (qint64 y = 0; y < after.image->height; ++y) for (qint64 x = 0; x < after.image->width; ++x) {
      qint64 sx = x, sy = y;
      switch (op.action) {
        case CanvasAction::RotateClockwise: sx = y; sy = kH - 1 - x; break;
        case CanvasAction::RotateCounterClockwise: sx = kW - 1 - y; sy = x; break;
        case CanvasAction::Rotate180: sx = kW - 1 - x; sy = kH - 1 - y; break;
        case CanvasAction::FlipHorizontal: sx = kW - 1 - x; break;
        default: sy = kH - 1 - y; break;
      }
      const Argb a = after.image->at(x, y), e = before.image->at(sx, sy);
      int pixelWorst = 0;
      for (int shift : {0, 8, 16, 24}) pixelWorst = std::max(pixelWorst, std::abs(int((a >> shift) & 255) - int((e >> shift) & 255)));
      worst = std::max(worst, pixelWorst); if (pixelWorst > 6) ++outliers;
    }
    // The Qt stroker joins reversed polylines, and the reference arrow head's
    // curved back edge (EA-006), with a few asymmetric antialiasing pixels.
    QVERIFY2(outliers <= (type == shape::freehand || type == shape::arrow ? 8 : 0), qPrintable(QString("worst channel difference %1 at %2 pixels").arg(worst).arg(outliers)));
  }

  void defaultFontSizesScaleWithResizeLikeStoredOnes() {
    const auto source = solidImage(kW, kH, 0);
    for (const QString &type : {shape::text, shape::speechBalloon}) {
      auto a = make(type, 0); a.content.fontSize = 0;
      const auto r = applyCanvasOperation(documentWith(a), source, resizeTo(80, 45));
      QCOMPARE(std::get<RectangleAnnotation>(r.document->annotations[0]).content.fontSize, 24 * 1.5);
    }
    // A step with no stored size derives it from its (scaled) box height.
    auto step = make(shape::step, 0); step.content.fontSize = 0;
    const auto r = applyCanvasOperation(documentWith(step), source, resizeTo(80, 45));
    QCOMPARE(std::get<RectangleAnnotation>(r.document->annotations[0]).content.fontSize, 0.0);
    // Effect strength is never rescaled.
    const auto blur = applyCanvasOperation(documentWith(make(shape::blur, 0)), source, resizeTo(80, 60));
    QCOMPARE(std::get<RectangleAnnotation>(blur.document->annotations[0]).content.strength, 7.0);
  }

  void customRotationKeepsAnglesAndRefusesNothingForUniformScale() {
    const auto d = documentWith(make(shape::rectangle, 17));
    const auto r = applyCanvasOperation(d, solidImage(kW, kH, 0), custom(30, true));
    QVERIFY(r.document);
    const auto &m = std::get<RectangleAnnotation>(r.document->annotations[0]);
    QCOMPARE(m.style.rotationDegrees, 47.0);  // 17 + 30, no trigonometric noise
    QCOMPARE(m.style.strokeWidth, 4.0);
    QCOMPARE(m.right() - m.left(), 20.0); QCOMPARE(m.bottom() - m.top(), 12.0);
  }

  void unsupportedStrokeAndShearFailAtomically() {
    const auto source = solidImage(kW, kH, 0xFFFFFFFF);
    const auto good = make(shape::rectangle, 0);
    auto d = baseDocument();
    d.annotations = {good, UnsupportedAnnotation{QUuid::createUuid(), {}}};
    for (const Case &c : cases()) {
      const auto r = applyCanvasOperation(d, source, c.op);
      QCOMPARE(r.error, std::optional<QString>(QStringLiteral("canvas-annotation-transform-unsupported")));
      QVERIFY(!r.document && !r.image && !r.changed);
    }
    // A later opaque object discards the whole operation, even after earlier ones transformed.
    auto e = baseDocument(); e.annotations = {good, make(shape::arrow, 37.5), UnsupportedAnnotation{QUuid::createUuid(), {}}};
    auto r = applyCanvasOperation(e, source, resizeTo(80, 45));
    QCOMPARE(r.error, std::optional<QString>(QStringLiteral("canvas-annotation-transform-unsupported"))); QVERIFY(!r.document && !r.image);
    // Aspect-locked rounding gives unequal scales; every type still scales deterministically.
    auto lockedOp = resizeTo(7, 1); lockedOp.lockAspect = true;
    for (const QString &type : shapeTypes()) {
      const auto scaled = applyCanvasOperation(documentWith(make(type, 0)), source, lockedOp);
      QVERIFY2(scaled.document, qPrintable(type + ": " + scaled.error.value_or(QString())));
    }
    // An undecodable embedded asset cannot be mirrored: refuse, keep nothing.
    auto broken = baseDocument(); broken.embeddedImages = {{kAsset, QByteArray("not a png")}}; broken.annotations = {make(shape::image, 0)};
    r = applyCanvasOperation(broken, source, action(CanvasAction::FlipVertical));
    QCOMPARE(r.error, std::optional<QString>(QStringLiteral("canvas-annotation-transform-unsupported"))); QVERIFY(!r.document && !r.image);
    // ... but rotation never touches asset bytes, so it still works.
    QVERIFY(applyCanvasOperation(broken, source, action(CanvasAction::RotateClockwise)).document);
    // Cancellation leaves no partial output either.
    const auto cancelledResult = applyCanvasOperation(documentWith(make(shape::image, 0)), source, action(CanvasAction::FlipHorizontal), {[] { return true; }, {}});
    QCOMPARE(cancelledResult.error, std::optional<QString>(QStringLiteral("canvas-cancelled"))); QVERIFY(!cancelledResult.document);
  }

  void invalidGeometryAndDegenerateMapsAreRejected() {
    auto bad = make(shape::rectangle, 0); bad.end = bad.start;
    QCOMPARE(transformShape(bad, CanvasTransform{}).error, diagnostic::documentInvalid);
    CanvasTransform singular; singular.xx = 0; singular.yy = 0;
    QCOMPARE(transformShape(make(shape::rectangle, 0), singular).error, diagnostic::documentInvalid);
    CanvasTransform shear; shear.xy = .5;
    QCOMPARE(transformShape(make(shape::rectangle, 0), shear).error, QStringLiteral("canvas-annotation-shear-unsupported"));
  }
};

QTEST_APPLESS_MAIN(AnnotationTransformTest)
#include "AnnotationTransformTest.moc"

