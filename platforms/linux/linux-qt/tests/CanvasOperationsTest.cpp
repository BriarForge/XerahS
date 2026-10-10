#include "image-editor/CanvasOperations.h"
#include "image-editor/EditorSession.h"
#include "EditorCanvasOperations.h"

#include <QBuffer>
#include <QTest>

#include <cmath>
#include <limits>

using namespace xerahs::editor;

namespace {
AnnotationDocument document(qint64 width, qint64 height) {
  AnnotationDocument d;
  d.canvasWidth = width; d.canvasHeight = height;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-10-11T00:00:00Z");
  d.sourceImagePng = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");
  return d;
}
RectangleAnnotation rectangle(PointF start, PointF end) {
  RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = start; r.end = end; r.style.strokeWidth = 1;
  return r;
}
AnnotationDocument withSource(const QImage &source) {
  auto d = document(source.width(), source.height());
  QBuffer png(&d.sourceImagePng); png.open(QIODevice::WriteOnly | QIODevice::Truncate); source.save(&png, "PNG");
  return d;
}
}  // namespace

class CanvasOperationsTest : public QObject {
  Q_OBJECT
private slots:
  void rotationsAndFlipsPermutePixels_data() {
    QTest::addColumn<int>("action"); QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
    QTest::addColumn<QList<quint32>>("expected");
    QTest::newRow("clockwise") << int(CanvasAction::RotateClockwise) << 2 << 3 << QList<quint32>{4, 1, 5, 2, 6, 3};
    QTest::newRow("counter-clockwise") << int(CanvasAction::RotateCounterClockwise) << 2 << 3 << QList<quint32>{3, 6, 2, 5, 1, 4};
    QTest::newRow("180") << int(CanvasAction::Rotate180) << 3 << 2 << QList<quint32>{6, 5, 4, 3, 2, 1};
    QTest::newRow("horizontal") << int(CanvasAction::FlipHorizontal) << 3 << 2 << QList<quint32>{3, 2, 1, 6, 5, 4};
    QTest::newRow("vertical") << int(CanvasAction::FlipVertical) << 3 << 2 << QList<quint32>{4, 5, 6, 1, 2, 3};
  }
  void rotationsAndFlipsPermutePixels() {
    QFETCH(int, action); QFETCH(int, width); QFETCH(int, height); QFETCH(QList<quint32>, expected);
    const ArgbImage source{3, 2, {1, 2, 3, 4, 5, 6}};
    CanvasOperation op; op.action = CanvasAction(action);
    const auto result = applyCanvasOperation(document(3, 2), source, op);
    QVERIFY(result.changed && result.image);
    QCOMPARE(result.image->width, width); QCOMPARE(result.image->height, height);
    for (std::size_t i = 0; i < result.image->pixels.size(); ++i) QCOMPARE(result.image->pixels[i], expected[qsizetype(i)]);
    QVERIFY(!result.pixelsResampled);
    QVERIFY(source.pixels == (std::vector<Argb>{1, 2, 3, 4, 5, 6}));
  }

  void transformedAnnotationsKeepAuthoritativePixels() {
    AnnotationDocument d = document(40, 30);
    auto r = rectangle({8, 9}, {22, 18}); r.style.rotationDegrees = 17; r.style.opacity = .6;
    r.style.fillColor = 0x801122FF; r.style.strokeWidth = 2.5;
    d.annotations = {r};
    const ArgbImage source = solidImage(40, 30, 0xAABBCCDD);
    const auto before = render(source, d.annotations); QVERIFY(before.image);
    for (CanvasAction action : {CanvasAction::RotateClockwise, CanvasAction::RotateCounterClockwise, CanvasAction::Rotate180,
                               CanvasAction::FlipHorizontal, CanvasAction::FlipVertical}) {
      CanvasOperation op; op.action = action;
      const auto changed = applyCanvasOperation(d, source, op); QVERIFY(changed.image && changed.document);
      const auto after = render(*changed.image, changed.document->annotations); QVERIFY(after.image);
      for (qint64 y = 0; y < after.image->height; ++y) for (qint64 x = 0; x < after.image->width; ++x) {
        qint64 sx = x, sy = y;
        switch (action) {
          case CanvasAction::RotateClockwise: sx = y; sy = 29 - x; break;
          case CanvasAction::RotateCounterClockwise: sx = 39 - y; sy = x; break;
          case CanvasAction::Rotate180: sx = 39 - x; sy = 29 - y; break;
          case CanvasAction::FlipHorizontal: sx = 39 - x; break;
          case CanvasAction::FlipVertical: sy = 29 - y; break;
          default: QFAIL("unreachable");
        }
        const Argb actual = after.image->at(x, y), expected = before.image->at(sx, sy);
        for (int shift : {0, 8, 16, 24}) QVERIFY(std::abs(int((actual >> shift) & 255) - int((expected >> shift) & 255)) <= 1);
      }
      QCOMPARE(annotationId(changed.document->annotations[0]), r.id);
    }
  }

  void canvasExpansionPreservesPixelsAndMovesAnnotations() {
    auto d = document(3, 2); auto r = rectangle({0, 0}, {1, 1}); d.annotations = {r};
    const ArgbImage source{3, 2, {1, 2, 3, 4, 5, 6}};
    CanvasOperation op; op.action = CanvasAction::ResizeCanvas; op.width = 5; op.height = 4; op.fill = 0x80445566;
    const auto result = applyCanvasOperation(d, source, op); QVERIFY(result.image);
    QCOMPARE(result.transform.dx, 1); QCOMPARE(result.transform.dy, 1);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) QCOMPARE(result.image->at(x + 1, y + 1), source.at(x, y));
    QCOMPARE(result.image->at(0, 0), Argb(0x80445566));
    const auto &moved = std::get<RectangleAnnotation>(result.document->annotations[0]);
    QCOMPARE(moved.start, (PointF{1, 1})); QCOMPARE(moved.end, (PointF{2, 2}));
    QVERIFY(!result.pixelsResampled);
  }

  void allNineAnchorsHaveDeterministicOffsets() {
    const ArgbImage source = solidImage(3, 2, 0xFF112233);
    for (int anchor = 0; anchor < 9; ++anchor) {
      CanvasOperation op; op.action = CanvasAction::ResizeCanvas; op.anchor = CanvasAnchor(anchor); op.width = 6; op.height = 5;
      const auto result = applyCanvasOperation(document(3, 2), source, op); QVERIFY(result.image);
      const int offsets[] = {0, 1, 3};
      QCOMPARE(result.transform.dx, offsets[anchor % 3]); QCOMPARE(result.transform.dy, offsets[anchor / 3]);
    }
  }

  void smallerCanvasRetainsPixelsWithoutResampling() {
    const ArgbImage source{3, 2, {1, 2, 3, 4, 5, 6}};
    CanvasOperation op; op.action = CanvasAction::ResizeCanvas; op.width = 2; op.height = 1;
    auto result = applyCanvasOperation(document(3, 2), source, op); QVERIFY(result.image);
    // Centred odd shrink uses floor(-.5): discard the top/left extra pixel.
    QVERIFY(result.image->pixels == (std::vector<Argb>{5, 6}));
    QVERIFY(!result.pixelsResampled);
    op.anchor = CanvasAnchor::TopLeft;
    result = applyCanvasOperation(document(3, 2), source, op); QVERIFY(result.image);
    QVERIFY(result.image->pixels == (std::vector<Argb>{1, 2}));
  }

  void cropNormalizesDragAndUsesRotatedIntersection() {
    auto d = document(10, 12);
    const auto inside = rectangle({3, 3}, {6, 7});
    const auto outside = rectangle({0, 0}, {1, 1});
    const auto partial = rectangle({1, 5}, {4, 8});
    auto rotated = rectangle({-10, 4.5}, {10, 5.5}); rotated.style.rotationDegrees = 90; rotated.style.strokeWidth = .1;
    d.annotations = {inside, outside, partial, rotated};
    ArgbImage source = solidImage(10, 12, 0);
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 10; ++x) source.pixels[y * 10 + x] = y * 10 + x;
    CanvasOperation op; op.action = CanvasAction::Crop; op.start = {7.8, 9.8}; op.end = {2.2, 2.1};
    const auto result = applyCanvasOperation(d, source, op); QVERIFY(result.image);
    QCOMPARE(result.image->width, 6); QCOMPARE(result.image->height, 8);
    QCOMPARE(result.image->at(0, 0), Argb(22)); QCOMPARE(result.image->at(5, 7), Argb(97));
    QCOMPARE(result.document->annotations.size(), std::size_t(2));
    QCOMPARE(annotationId(result.document->annotations[0]), inside.id);
    const auto &kept = std::get<RectangleAnnotation>(result.document->annotations[1]);
    QCOMPARE(kept.id, partial.id); QCOMPARE(kept.start, (PointF{-1, 3}));
  }

  void resizeHasExplicitNearestAndAlphaAwareBilinear() {
    const ArgbImage source{2, 1, {0xFFFF0000, 0x000000FF}};
    CanvasOperation op; op.action = CanvasAction::ResizeImage; op.width = 4; op.height = 1; op.lockAspect = false;
    auto result = applyCanvasOperation(document(2, 1), source, op); QVERIFY(result.image);
    QVERIFY(result.image->pixels == (std::vector<Argb>{0xFFFF0000, 0xFFFF0000, 0x000000FF, 0x000000FF}));
    op.interpolation = Interpolation::Bilinear;
    result = applyCanvasOperation(document(2, 1), source, op); QVERIFY(result.image);
    // The transparent blue endpoint must not contaminate red RGB. Linear
    // alpha at the interior centres is .75 and .25, quantized to 191 and 64.
    QVERIFY(result.image->pixels == (std::vector<Argb>{0xFFFF0000, 0xBFFF0000, 0x40FF0000, 0}));
    op.lockAspect = true;
    result = applyCanvasOperation(document(2, 1), source, op); QVERIFY(result.image);
    QCOMPARE(result.image->height, 2);
  }

  void autoCropUsesAlphaThresholdAndCopiesOriginalPixels() {
    ArgbImage source = solidImage(8, 6, 0x00112233);
    source.pixels[0] = 0x0800FF00;
    for (int y = 1; y < 5; ++y) for (int x = 2; x < 6; ++x) source.pixels[y * 8 + x] = 0x80FF0000;
    CanvasOperation op; op.action = CanvasAction::AutoCrop; op.autoCrop.alphaThreshold = 8;
    const auto result = applyCanvasOperation(document(8, 6), source, op);
    QVERIFY(result.changed && result.image && !result.pixelsResampled);
    QCOMPARE(result.image->width, 4); QCOMPARE(result.image->height, 4);
    QCOMPARE(result.transform.dx, -2); QCOMPARE(result.transform.dy, -1);
    for (Argb pixel : result.image->pixels) QCOMPARE(pixel, Argb(0x80FF0000));
    QCOMPARE(source.at(0, 0), Argb(0x0800FF00));
  }

  void autoCropColorDistanceIncludesAlphaAndInclusiveTolerance() {
    ArgbImage source = solidImage(7, 5, 0xFFEEEEEE);
    source.pixels[1] = 0xFFE9EDEE;  // distance 5, still background
    source.pixels[2 * 7 + 3] = 0xFF000000;
    source.pixels[3 * 7 + 4] = 0xF9EEEEEE;  // alpha distance 6, foreground
    CanvasOperation op; op.action = CanvasAction::AutoCrop;
    op.autoCrop.border = CropBorder::TopLeftColor; op.autoCrop.tolerance = 5;
    const auto result = applyCanvasOperation(document(7, 5), source, op); QVERIFY(result.image);
    QCOMPARE(result.image->width, 2); QCOMPARE(result.image->height, 2);
    QVERIFY(result.image->pixels == (std::vector<Argb>{0xFF000000, 0xFFEEEEEE, 0xFFEEEEEE, 0xF9EEEEEE}));
    op.autoCrop.border = CropBorder::Color; op.autoCrop.color = 0xFFEEEEEE;
    const auto explicitColor = applyCanvasOperation(document(7, 5), source, op); QVERIFY(explicitColor.image);
    QVERIFY(explicitColor.image->pixels == result.image->pixels);
  }

  void autoCropKeepsVisibleAnnotationContentEditable() {
    auto d = document(8, 8);
    auto visible = rectangle({1, 1}, {3, 3}); visible.style.strokeColor = 0; visible.style.fillColor = 0xFFFF0000;
    auto hidden = rectangle({6, 6}, {7, 7}); hidden.visible = false;
    d.annotations = {visible, hidden};
    ArgbImage source = solidImage(8, 8, 0); source.pixels[4 * 8 + 4] = 0xFF0000FF;
    CanvasOperation op; op.action = CanvasAction::AutoCrop;
    auto result = applyCanvasOperation(d, source, op); QVERIFY(result.image && result.document);
    QCOMPARE(result.image->width, 4); QCOMPARE(result.image->height, 4);
    QCOMPARE(result.image->at(0, 0), Argb(0));  // annotations were never flattened into source
    QCOMPARE(result.document->annotations.size(), std::size_t(1));
    const auto &kept = std::get<RectangleAnnotation>(result.document->annotations[0]);
    QCOMPARE(kept.id, visible.id); QCOMPARE(kept.start, (PointF{0, 0})); QCOMPARE(kept.end, (PointF{2, 2}));
    op.autoCrop.includeAnnotations = false;
    result = applyCanvasOperation(d, source, op); QVERIFY(result.image);
    QCOMPARE(result.image->width, 1); QCOMPARE(result.image->height, 1);
    QVERIFY(result.document->annotations.empty());
  }

  void autoCropNoForegroundAndFullBoundsPreserveHistory() {
    QImage source(8, 6, QImage::Format_ARGB32); source.fill(0x00FF00FF);
    EditorSession session(withSource(source));
    CanvasOperation op; op.action = CanvasAction::AutoCrop;
    auto result = xerahs::app::prepareCanvasEdit(session.document(), source, op);
    QVERIFY(!result.result.changed && !result.result.image);
    QCOMPARE(result.result.document->sourceImagePng, session.document().sourceImagePng);
    source.setPixel(0, 0, 0xFFFFFFFF); source.setPixel(7, 5, 0xFFFFFFFF);
    result = xerahs::app::prepareCanvasEdit(withSource(source), source, op);
    QVERIFY(!result.result.changed && !result.result.image);
    QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }

  void autoCropValidationAndCancellationReturnNoPartialResult() {
    auto d = document(40, 40);
    auto r = rectangle({2, 2}, {38, 38}); r.style.fillColor = 0xFFFFFFFF; d.annotations = {r};
    const ArgbImage source = solidImage(40, 40, 0);
    CanvasOperation op; op.action = CanvasAction::AutoCrop;
    op.autoCrop.tolerance = 256;
    QCOMPARE(applyCanvasOperation(d, source, op).error, std::optional<QString>(diagnostic::documentInvalid));
    op.autoCrop.tolerance = 0;
    for (int cancelAt : {5, 45, 75}) {
      int percent = 0;
      const auto result = applyCanvasOperation(d, source, op,
          {[&] { return percent >= cancelAt; }, [&](int p) { percent = p; }});
      QCOMPARE(result.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
      QVERIFY(!result.document && !result.image);
    }
  }

  void invalidAndCancelledEditsHaveNoPartialOutputs() {
    const ArgbImage source = solidImage(10, 12, 0xFFFFFFFF);
    const auto d = document(10, 12);
    CanvasOperation op; op.action = CanvasAction::ResizeCanvas; op.width = 200000; op.height = 10;
    QCOMPARE(applyCanvasOperation(d, source, op).error, std::optional<QString>(diagnostic::documentTooLarge));
    op.width = std::numeric_limits<qint64>::min();
    QCOMPARE(applyCanvasOperation(d, source, op).error, std::optional<QString>(diagnostic::documentInvalid));
    op.action = CanvasAction::Crop; op.start = {NAN, 0}; op.end = {10, 10};
    QCOMPARE(applyCanvasOperation(d, source, op).error, std::optional<QString>(diagnostic::documentInvalid));
    op.start = {2, 2}; op.end = {8, 8};
    int percent = 0;
    const auto result = applyCanvasOperation(d, source, op, {[&] { return percent >= 50; }, [&](int p) { percent = p; }});
    QCOMPARE(result.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
    QVERIFY(!result.document && !result.image);
    QVERIFY(!result.changed);
    auto unsupported = d;
    unsupported.annotations = {UnsupportedAnnotation{QUuid::createUuid(), {}}};
    QCOMPARE(applyCanvasOperation(unsupported, source, op).error,
             std::optional<QString>(QStringLiteral("canvas-annotation-transform-unsupported")));
  }

  void encodedEditsAreOneHistoryStepAndRestoreSourceAndSelection() {
    QImage source(10, 12, QImage::Format_ARGB32); source.fill(Qt::white);
    auto d = withSource(source);
    const auto inside = rectangle({3, 3}, {6, 7}), outside = rectangle({0, 0}, {1, 1});
    d.annotations = {inside, outside};
    EditorSession session(d); session.select({outside.id}); session.markSaved();
    CanvasOperation op; op.action = CanvasAction::Crop; op.start = {2, 2}; op.end = {8, 10};
    const auto prepared = xerahs::app::prepareCanvasEdit(session.document(), source, op);
    QVERIFY(prepared.result.changed && prepared.result.document);
    QVERIFY(session.commitCanvas(*prepared.result.document, session.stateId()));
    QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty()); QVERIFY(session.selection().isEmpty());
    QCOMPARE(QImage::fromData(session.document().sourceImagePng).size(), QSize(6, 8));
    QVERIFY(session.undo()); QVERIFY(!session.dirty()); QCOMPARE(session.selection(), QSet<QUuid>{outside.id});
    QCOMPARE(session.document().sourceImagePng, d.sourceImagePng);
    QVERIFY(session.redo()); QVERIFY(session.dirty());
    const quint64 stale = session.stateId(); QVERIFY(session.undo());
    QVERIFY(!session.commitCanvas(*prepared.result.document, stale));
    QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
  }

  void unchangedDimensionsCreateNoHistoryAndNoPixelAllocation() {
    CanvasOperation op; op.action = CanvasAction::ResizeCanvas; op.width = 10; op.height = 12;
    const auto result = applyCanvasOperation(document(10, 12), solidImage(10, 12, 0), op);
    QVERIFY(!result.changed && !result.image);
  }

  void cancelledEncodingAndInvalidCommitLeaveTheSessionUntouched() {
    QImage source(10, 12, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(withSource(source));
    CanvasOperation op; op.action = CanvasAction::RotateClockwise;
    int percent = 0;
    const auto prepared = xerahs::app::prepareCanvasEdit(session.document(), source, op,
        {[&] { return percent >= 90; }, [&](int value) { percent = value; }});
    QCOMPARE(prepared.result.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
    QVERIFY(!prepared.result.document);
    auto invalid = session.document(); invalid.canvasWidth = 200000;
    QVERIFY(!session.commitCanvas(invalid, session.stateId()));
    invalid = session.document(); invalid.canvasWidth = 11;  // unchanged 10px source header
    QVERIFY(!session.commitCanvas(invalid, session.stateId()));
    QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 0); QVERIFY(!session.dirty());
  }

  void rotationOfUniformSourceStillCommitsItsCoordinateTransform() {
    QImage source(4, 4, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(withSource(source));
    CanvasOperation op; op.action = CanvasAction::RotateClockwise;
    const auto prepared = xerahs::app::prepareCanvasEdit(session.document(), source, op);
    QVERIFY(prepared.result.changed);
    QVERIFY(session.commitCanvas(*prepared.result.document, session.stateId()));
    QCOMPARE(session.undoCount(), 1);
    QVERIFY(session.undo()); QVERIFY(!session.dirty());
  }

  void customRotationHasAnalyticalNearestPixelsAndCanvasGeometry() {
    const ArgbImage source{3, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9}};
    auto d = document(3, 3); auto r = rectangle({1, 1}, {2, 2}); r.style.rotationDegrees = 17; d.annotations = {r};
    CanvasOperation op; op.action = CanvasAction::RotateCustom; op.rotationDegrees = 45; op.fill = 99;
    auto result = applyCanvasOperation(d, source, op); QVERIFY(result.image && result.document);
    // Rotated extent is 3*sqrt(2), rounded up to 5, centred on (2.5,2.5).
    QCOMPARE(result.image->width, 5); QCOMPARE(result.image->height, 5); QVERIFY(result.pixelsResampled);
    QVERIFY(result.image->pixels == (std::vector<Argb>{99,99,1,99,99, 99,4,1,2,99, 7,7,5,3,3, 99,8,9,6,99, 99,99,9,99,99}));
    const auto &moved = std::get<RectangleAnnotation>(result.document->annotations[0]);
    QCOMPARE(moved.id, r.id); QCOMPARE(moved.start, (PointF{2, 2})); QCOMPARE(moved.end, (PointF{3, 3}));
    QCOMPARE(moved.style.rotationDegrees, 62); QCOMPARE(moved.style.strokeWidth, r.style.strokeWidth);
    op.expandCanvas = false;
    result = applyCanvasOperation(d, source, op); QVERIFY(result.image);
    QCOMPARE(result.image->width, 3); QCOMPARE(result.image->height, 3);
    QVERIFY(result.image->pixels == (std::vector<Argb>{4,1,2, 7,5,3, 8,9,6}));
    const auto &cropped = std::get<RectangleAnnotation>(result.document->annotations[0]);
    QCOMPARE(cropped.start.x, r.start.x); QCOMPARE(cropped.start.y, r.start.y);
  }

  void customBilinearSamplesExplicitBorderAndPremultipliedAlpha() {
    CanvasOperation op; op.action = CanvasAction::RotateCustom; op.rotationDegrees = 45; op.interpolation = Interpolation::Bilinear;
    const ArgbImage source{1, 1, {0xFFFF0000}};
    op.fill = 0x000000FF;  // transparent RGB must not leak into red
    auto result = applyCanvasOperation(document(1, 1), source, op); QVERIFY(result.image);
    QCOMPARE(result.image->width, 2); QCOMPARE(result.image->height, 2);
    // Each corner has source weight 1 - 1/sqrt(2), so alpha rounds to 75.
    for (Argb pixel : result.image->pixels) QCOMPARE(pixel, Argb(0x4BFF0000));
    op.fill = 0xFF0000FF;
    result = applyCanvasOperation(document(1, 1), source, op); QVERIFY(result.image);
    for (Argb pixel : result.image->pixels) QCOMPARE(pixel, Argb(0xFF4B00B4));
  }

  void customQuarterTurnsAreExactAndFullTurnsAreNoOps() {
    const ArgbImage source{3, 2, {1, 2, 3, 4, 5, 6}};
    CanvasOperation op; op.action = CanvasAction::RotateCustom; op.rotationDegrees = 450;
    op.interpolation = Interpolation::Bilinear;
    auto result = applyCanvasOperation(document(3, 2), source, op); QVERIFY(result.image);
    QVERIFY(!result.pixelsResampled);
    QVERIFY(result.image->pixels == (std::vector<Argb>{4, 1, 5, 2, 6, 3}));
    op.rotationDegrees = -720;
    result = applyCanvasOperation(document(3, 2), source, op);
    QVERIFY(!result.changed && !result.image);
  }

  void customRotationRejectsInvalidOrExcessiveDimensionsAndCancels() {
    CanvasOperation op; op.action = CanvasAction::RotateCustom; op.rotationDegrees = NAN;
    QCOMPARE(applyCanvasOperation(document(4, 4), solidImage(4, 4, 0), op).error, std::optional<QString>(diagnostic::documentInvalid));
    op.rotationDegrees = 45;
    QCOMPARE(applyCanvasOperation(document(100000, 1), solidImage(100000, 1, 0), op).error, std::optional<QString>(diagnostic::documentTooLarge));
    int percent = 0;
    const auto cancelled = applyCanvasOperation(document(4, 4), solidImage(4, 4, 0), op,
        {[&] { return percent >= 50; }, [&](int p) { percent = p; }});
    QCOMPARE(cancelled.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
    QVERIFY(!cancelled.image && !cancelled.document);
    auto d = document(4, 4); d.annotations = {rectangle({1, 1}, {3, 3}), rectangle({-10, -10}, {-9, -9})};
    op.expandCanvas = false;
    const auto clipped = applyCanvasOperation(d, solidImage(4, 4, 0), op); QVERIFY(clipped.document);
    QCOMPARE(clipped.document->annotations.size(), std::size_t(1));
  }

  void preparedPreviewCompositesAnnotationsAndCommitsOnlyOnRequest() {
    QImage source(4, 4, QImage::Format_ARGB32); source.fill(Qt::blue);
    auto d = withSource(source); auto r = rectangle({1, 1}, {3, 3});
    r.style.strokeColor = 0; r.style.fillColor = 0xFFFF0000; d.annotations = {r};
    EditorSession session(d); const quint64 state = session.stateId();
    CanvasOperation op; op.action = CanvasAction::RotateCustom; op.rotationDegrees = 90;
    const auto preview = xerahs::app::prepareCanvasPreview(d, source, op);
    QVERIFY(!preview.edit.result.error && preview.edit.result.document);
    QCOMPARE(preview.composite.pixel(1, 1), QRgb(0xFFFF0000));
    QCOMPARE(preview.edit.source.pixel(1, 1), QRgb(0xFF0000FF));
    QCOMPARE(session.document().sourceImagePng, d.sourceImagePng); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    QVERIFY(session.commitCanvas(*preview.edit.result.document, state)); QCOMPARE(session.undoCount(), 1);
    QVERIFY(session.undo()); QVERIFY(!session.dirty()); QCOMPARE(session.document().sourceImagePng, d.sourceImagePng);
    for (int cancelAt : {40, 80, 98}) {
      int percent = 0;
      const auto cancelled = xerahs::app::prepareCanvasPreview(d, source, op,
          {[&] { return percent >= cancelAt; }, [&](int p) { percent = p; }});
      QCOMPARE(cancelled.edit.result.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
      QVERIFY(!cancelled.edit.result.document && cancelled.composite.isNull());
    }
  }

  void flattenCompositesOnlyRepresentedObjectsAndRetainsOthers() {
    auto d = document(4, 4);
    auto shown = rectangle({1, 1}, {3, 3}); shown.style.strokeColor = 0; shown.style.fillColor = 0x80FF0000;
    auto hidden = rectangle({0, 0}, {1, 1}); hidden.visible = false;
    auto outside = rectangle({10, 10}, {11, 11});
    auto transparent = rectangle({0, 0}, {1, 1}); transparent.style.strokeColor = transparent.style.fillColor = 0;
    auto zeroOpacity = rectangle({3, 3}, {4, 4}); zeroOpacity.style.opacity = 0;
    const QUuid futureId = QUuid::createUuid();
    const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"visible", false}, {"vendor", "retain"}}};
    d.annotations = {shown, hidden, outside, transparent, zeroOpacity, future};
    d.embeddedImages = {{QStringLiteral("asset"), d.sourceImagePng}};
    const ArgbImage source = solidImage(4, 4, 0x800000FF);
    CanvasOperation op; op.action = CanvasAction::Flatten;
    auto result = applyCanvasOperation(d, source, op); QVERIFY(result.changed && result.image && result.document);
    // Source-over 128/255 red on 128/255 blue gives A=192,R=170,B=85.
    QCOMPARE(result.image->at(1, 1), Argb(0xC0AA0055)); QCOMPARE(result.image->at(0, 0), Argb(0x800000FF));
    QCOMPARE(result.document->annotations.size(), std::size_t(5));
    QCOMPARE(annotationId(result.document->annotations[0]), hidden.id);
    QCOMPARE(annotationId(result.document->annotations[4]), futureId);
    QVERIFY(result.document->embeddedImages == d.embeddedImages);
    const auto repeated = applyCanvasOperation(*result.document, *result.image, op);
    QVERIFY(!repeated.changed && !repeated.image);  // nothing else is represented
    QCOMPARE(source.at(1, 1), Argb(0x800000FF));
    std::get<UnsupportedAnnotation>(d.annotations.back()).raw.insert(QStringLiteral("visible"), true);
    const auto unsupported = applyCanvasOperation(d, source, op);
    QCOMPARE(unsupported.error, std::optional<QString>(QStringLiteral("canvas-annotation-render-unsupported")));
    QVERIFY(!unsupported.document && !unsupported.image);
  }

  void nativeFlattenRestoresSourceAssetsSelectionAndCheckpointWithUndo() {
    QImage source(4, 4, QImage::Format_ARGB32); source.fill(0x800000FF);
    auto d = withSource(source); auto shown = rectangle({1, 1}, {3, 3}); shown.style.strokeColor = 0; shown.style.fillColor = 0x80FF0000;
    auto hidden = rectangle({0, 0}, {1, 1}); hidden.visible = false; d.annotations = {shown, hidden};
    EditorSession session(d); session.select({shown.id, hidden.id});
    CanvasOperation op; op.action = CanvasAction::Flatten;
    const auto prepared = xerahs::app::prepareCanvasEdit(d, source, op); QVERIFY(prepared.result.document);
    QCOMPARE(prepared.source.pixel(1, 1), QRgb(0xC0AA0055));
    QVERIFY(session.commitCanvas(*prepared.result.document, session.stateId()));
    QCOMPARE(session.undoCount(), 1); QCOMPARE(session.selection(), QSet<QUuid>{hidden.id}); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QCOMPARE(session.document().sourceImagePng, d.sourceImagePng);
    QCOMPARE(session.selection(), (QSet<QUuid>{shown.id, hidden.id})); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(session.document().annotations.size(), std::size_t(1));
  }

  void clearCommandsAreDistinctAtomicAndRepeatAsNoOps() {
    QImage source(4, 4, QImage::Format_ARGB32); source.fill(Qt::blue);
    auto d = withSource(source); auto r = rectangle({1, 1}, {3, 3}); d.annotations = {r};
    EditorSession session(d); session.select({r.id});
    CanvasOperation op; op.action = CanvasAction::ClearAnnotations;
    auto prepared = xerahs::app::prepareCanvasEdit(d, source, op);
    QVERIFY(prepared.result.changed && !prepared.result.image); QCOMPARE(prepared.result.document->sourceImagePng, d.sourceImagePng);
    QVERIFY(session.commitCanvas(*prepared.result.document, session.stateId()));
    QVERIFY(session.document().annotations.empty()); QCOMPARE(session.document().sourceImagePng, d.sourceImagePng);
    QVERIFY(!xerahs::app::prepareCanvasEdit(session.document(), source, op).result.changed);
    QVERIFY(session.undo()); QCOMPARE(session.selection(), QSet<QUuid>{r.id}); QVERIFY(!session.dirty());
    op.action = CanvasAction::ClearImageAndAnnotations;
    prepared = xerahs::app::prepareCanvasEdit(d, source, op); QVERIFY(prepared.result.changed);
    QCOMPARE(prepared.source.size(), source.size()); QVERIFY(prepared.result.document->annotations.empty());
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) QCOMPARE(prepared.source.pixel(x, y), QRgb(0));
    QVERIFY(session.commitCanvas(*prepared.result.document, session.stateId()));
    QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0);
    QVERIFY(!xerahs::app::prepareCanvasEdit(session.document(), prepared.source, op).result.changed);
    QVERIFY(session.undo()); QCOMPARE(session.document().sourceImagePng, d.sourceImagePng); QVERIFY(!session.dirty());
  }

  void flattenAndClearCancellationNeverReturnPartialEdits() {
    auto d = document(100, 100); auto r = rectangle({5, 5}, {95, 95}); r.style.fillColor = 0xFFFFFFFF; d.annotations = {r};
    for (CanvasAction action : {CanvasAction::Flatten, CanvasAction::ClearAnnotations, CanvasAction::ClearImageAndAnnotations}) {
      CanvasOperation op; op.action = action; int percent = 0;
      const auto cancelled = applyCanvasOperation(d, solidImage(100, 100, 0), op,
          {[&] { return percent >= 50; }, [&](int p) { percent = p; }});
      QCOMPARE(cancelled.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
      QVERIFY(!cancelled.document && !cancelled.image);
      const auto immediate = applyCanvasOperation(d, solidImage(100, 100, 0), op, {[] { return true; }, {}});
      QCOMPARE(immediate.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
    }
  }
};

QTEST_GUILESS_MAIN(CanvasOperationsTest)
#include "CanvasOperationsTest.moc"
