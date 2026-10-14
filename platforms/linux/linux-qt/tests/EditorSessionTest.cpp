// Production-path checks that the conformance vectors cannot cover: the .xann
// gzip container and its decompression limit, PNG header checks, save
// outcomes, sidecar writing, colour case, and rectangle rendering.

#include "image-editor/AnnotationRenderer.h"
#include "image-editor/EditorSession.h"
#include "image-editor/SourceBinding.h"

#include <QJsonArray>
#include <QTest>

#include <map>
#include <limits>

using namespace xerahs::editor;

namespace {

const QByteArray kOnePixelPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");

AnnotationDocument document(qint64 width = 10, qint64 height = 10) {
  AnnotationDocument d;
  d.canvasWidth = width;
  d.canvasHeight = height;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-10-08T00:00:00Z");
  d.sourceImagePng = kOnePixelPng;
  return d;
}

RectangleAnnotation rectangle(double l, double t, double r, double b) {
  RectangleAnnotation a;
  a.id = QUuid::createUuid();
  a.start = {l, t};
  a.end = {r, b};
  return a;
}

// A PNG header claiming the given size, with no pixel data.
QByteArray pngHeader(quint32 width, quint32 height) {
  QByteArray png("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16);
  for (quint32 v : {width, height}) {
    for (int shift = 24; shift >= 0; shift -= 8) png.append(static_cast<char>((v >> shift) & 0xFF));
  }
  return png + QByteArray(5, '\0');
}

class MapStore final : public SidecarStore {
public:
  std::map<QString, QByteArray> files;
  bool failWrites = false;
  bool exists(const QString &path) override { return files.count(path) > 0; }
  bool write(const QString &path, const QByteArray &bytes) override {
    if (failWrites) return false;
    files[path] = bytes;
    return true;
  }
  bool remove(const QString &path) override { return files.erase(path) > 0; }
};

}  // namespace

class EditorSessionTest : public QObject {
  Q_OBJECT

private slots:
  void rejectedCreationPreservesRedoCheckpointAndSelection_data() {
    QTest::addColumn<double>("sx"); QTest::addColumn<double>("sy");
    QTest::addColumn<double>("ex"); QTest::addColumn<double>("ey");
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    QTest::newRow("start-x-nan") << nan << 1.0 << 5.0 << 6.0;
    QTest::newRow("start-y-nan") << 1.0 << nan << 5.0 << 6.0;
    QTest::newRow("end-x-nan") << 1.0 << 1.0 << nan << 6.0;
    QTest::newRow("end-y-nan") << 1.0 << 1.0 << 5.0 << nan;
    QTest::newRow("infinite") << 1.0 << 1.0 << inf << 6.0;
    QTest::newRow("span-overflow") << -1e308 << 1.0 << 1e308 << 6.0;
    QTest::newRow("zero-width") << 1.0 << 1.0 << 1.0 << 6.0;
    QTest::newRow("zero-height") << 1.0 << 1.0 << 5.0 << 1.0;
  }
  void rejectedCreationPreservesRedoCheckpointAndSelection() {
    QFETCH(double, sx); QFETCH(double, sy); QFETCH(double, ex); QFETCH(double, ey);
    EditorSession session(document()); const auto id = session.createRectangle({1, 1}, {4, 4}); QVERIFY(id);
    session.markSaved(); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select({*id});
    const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    QVERIFY(!session.createRectangle({sx, sy}, {ex, ey}));
    QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.stateId(), state);
    QCOMPARE(session.selection(), QSet<QUuid>{*id}); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).left(), 2.0);
  }
  void batchGeometryFailureAndRoundedNoopsNeverTruncateRedo() {
    auto d = document(); const auto small = rectangle(1, 1, 4, 4), large = rectangle(1.2e308, 1, 1.3e308, 4);
    d.annotations = {small, large}; EditorSession session(d);
    QVERIFY(session.createRectangle({5, 5}, {6, 6})); QVERIFY(session.undo()); session.select({small.id, large.id});
    const auto before = serializeDocument(session.document());
    QVERIFY(!session.moveSelection(1e308, 0)); QVERIFY(!session.resizeSelection(1e308, 0));
    QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    QCOMPARE(session.selection(), (QSet<QUuid>{small.id, large.id}));
    session.select({large.id}); QVERIFY(!session.moveSelection(1, 0)); QVERIFY(!session.resizeSelection(1, 0));
    QVERIFY(session.editDiagnostic().isEmpty()); QCOMPARE(session.redoCount(), 1); QCOMPARE(serializeDocument(session.document()), before);
    // Finite angle additions must never overflow before normalization.
    session.select({small.id}); QVERIFY(session.rotateSelection(1e308)); QVERIFY(session.rotateSelection(1e308));
    const double angle = std::get<RectangleAnnotation>(session.document().annotations[0]).style.rotationDegrees;
    QVERIFY(std::isfinite(angle)); QVERIFY(angle >= 0 && angle < 360);
    QVERIFY(render(solidImage(10, 10, 0), session.document().annotations).image);
  }
  void everyEditRestoresTheActualPreCommandSelection() {
    EditorSession session(document()); const auto a = *session.createRectangle({1, 1}, {4, 4});
    const auto b = *session.createRectangle({5, 5}, {8, 8}); session.select({a}); session.markSaved();
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); QCOMPARE(session.selection(), QSet<QUuid>{a}); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); session.select({b});
    const auto c = *session.createRectangle({-5, -4}, {-2, -1});
    QVERIFY(session.undo()); QCOMPARE(session.selection(), QSet<QUuid>{b});
    QVERIFY(session.redo()); QCOMPARE(session.selection(), QSet<QUuid>{c});
    session.select({a, b}); QVERIFY(session.deleteSelection());
    QVERIFY(session.undo()); QCOMPARE(session.selection(), (QSet<QUuid>{a, b}));
    QVERIFY(session.redo()); QVERIFY(session.selection().isEmpty()); QCOMPARE(session.document().annotations.size(), size_t(1));
  }
  void snapshotSaveAcknowledgesItsHistoryStateAndSplitSaveInvalidatesCleanState() {
    EditorSession session(document()); QVERIFY(session.createRectangle({2, 2}, {10, 10}));
    const auto saved = session.stateId(); QVERIFY(session.moveSelection(2, 0));
    QVERIFY(session.recordSnapshotSave(saved, true, true).isEmpty()); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QVERIFY(!session.dirty());
    QCOMPARE(session.recordSnapshotSave(saved, true, false), QStringList{diagnostic::sidecarSaveFailed}); QVERIFY(session.dirty());
    QVERIFY(session.recordSnapshotSave(saved, true, true).isEmpty()); QVERIFY(!session.dirty());
    QVERIFY(session.undo()); QVERIFY(session.createRectangle({12, 12}, {18, 18}));
    QVERIFY(session.recordSnapshotSave(saved, true, true).isEmpty()); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QVERIFY(session.dirty()); // truncated saved snapshot is unreachable
  }
  void creationDefaultsLeaveSelectedObjectsCheckpointAndRedoIntact() {
    EditorSession session(document());
    const auto id = session.createRectangle({1, 1}, {4, 4}); QVERIFY(id);
    session.markSaved();
    const auto before = serializeDocument(session.document());
    const auto selection = session.selection(); const auto state = session.stateId();
    RectangleStyle next; next.strokeColor = 0x1234ABCD; next.fillColor = 0x80665544; next.strokeWidth = 10;
    QVERIFY(session.setCreationStyle(next));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection);
    QCOMPARE(session.stateId(), state); QCOMPARE(session.undoCount(), 1); QVERIFY(!session.dirty());
    QVERIFY(session.undo()); QCOMPARE(session.redoCount(), 1);
    next.strokeWidth = 9; QVERIFY(session.setCreationStyle(next)); QCOMPARE(session.redoCount(), 1);
    QVERIFY(session.redo()); QVERIFY(!session.dirty());
    QVERIFY(session.createRectangle({5, 5}, {8, 8}));
    const auto &created = std::get<RectangleAnnotation>(session.document().annotations.back());
    QCOMPARE(created.style.strokeColor, Argb{0x1234ABCD}); QCOMPARE(created.style.fillColor, Argb{0x80665544});
    QCOMPARE(created.style.strokeWidth, 9.0);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations.front()).style.strokeWidth, 4.0);
    QVERIFY(session.undo()); QVERIFY(!session.dirty()); QCOMPARE(session.toolStyle().strokeWidth, 9.0);
    // Explicit editing of the selected original object remains a history edit.
    QVERIFY(session.setStrokeWidth(7)); QVERIFY(session.dirty());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations.front()).style.strokeWidth, 7.0);
  }
  void invalidCreationDefaultsPreserveThePreviousStyle() {
    EditorSession session(document());
    RectangleStyle valid; valid.strokeWidth = 8; QVERIFY(session.setCreationStyle(valid));
    for (double width : {0.0, -1.0, 10000.1, std::numeric_limits<double>::infinity()}) {
      auto next = valid; next.strokeWidth = width; QVERIFY(!session.setCreationStyle(next));
      QCOMPARE(session.toolStyle().strokeWidth, 8.0);
    }
    for (double opacity : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
      auto next = valid; next.opacity = opacity; QVERIFY(!session.setCreationStyle(next));
    }
    auto next = valid; next.rotationDegrees = std::numeric_limits<double>::infinity();
    QVERIFY(!session.setCreationStyle(next)); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }
  void unsavedInitialPixelsHaveNoCleanCheckpointUntilSuccessfulSave() {
    EditorSession session(document(), EditorSession::InitialPersistence::Unsaved);
    QVERIFY(session.dirty()); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.undo());
    QVERIFY(session.createRectangle({1, 1}, {4, 4}));
    QVERIFY(session.undo()); QVERIFY(session.dirty());
    QCOMPARE(session.recordSave(true, false), QStringList{diagnostic::sidecarSaveFailed});
    QVERIFY(session.dirty());
    QVERIFY(session.recordSave(true, true).isEmpty()); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QVERIFY(!session.dirty());
    EditorSession opened(document()); QVERIFY(!opened.dirty());
  }

  void mismatchedSourceRequiresAnExplicitBindingChoice() {
    auto d = document(10, 10); d.sourceImagePng = pngHeader(10, 10);
    d.imageHash = QStringLiteral("sha256:") + QString(64, QLatin1Char('0'));
    const QString hash = QStringLiteral("sha256:") + QString(64, QLatin1Char('1'));
    const QByteArray current = pngHeader(12, 8);
    const auto r = rectangle(1, 2, 5, 6); d.annotations = {r};
    d.extra.insert(QStringLiteral("vendor"), QStringLiteral("preserve"));
    auto result = bindSource(d, current, QStringLiteral("changed.png"), hash);
    QVERIFY(result.choiceRequired && !result.document && !result.error);
    result = bindSource(d, current, QStringLiteral("changed.png"), hash, SourceChoice::CurrentRaster);
    QVERIFY(result.document); QCOMPARE(result.document->sourceImagePng, current);
    QCOMPARE(result.document->canvasWidth, 12); QCOMPARE(result.document->canvasHeight, 8);
    QCOMPARE(result.document->imageHash, hash); QCOMPARE(result.document->extra, d.extra);
    QCOMPARE(serializeDocument(*result.document).value(u"annotations"), serializeDocument(d).value(u"annotations"));
    result = bindSource(d, current, QStringLiteral("changed.png"), hash, SourceChoice::EmbeddedSource);
    QVERIFY(result.document); QCOMPARE(result.document->sourceImagePng, d.sourceImagePng);
    QCOMPARE(result.document->canvasWidth, 10); QCOMPARE(result.document->canvasHeight, 10);
    QCOMPARE(d.imageHash, QStringLiteral("sha256:") + QString(64, QLatin1Char('0')));
    result = bindSource(d, current, QStringLiteral("unchanged.png"), d.imageHash);
    QVERIFY(result.document && !result.choiceRequired); QCOMPARE(result.document->sourceImagePng, d.sourceImagePng);
  }

  void sourceBindingRejectsVersionsCorruptHeadersAndInvalidChoices() {
    auto d = document(1, 1);
    d.version = 2;
    QCOMPARE(bindSource(d, kOnePixelPng, QStringLiteral("image.png"), QString()).error,
             std::optional<QString>(diagnostic::documentVersionUnsupported));
    d.version = 1;
    QCOMPARE(bindSource(d, QByteArray("bad"), QStringLiteral("image.png"), QString(), SourceChoice::CurrentRaster).error,
             std::optional<QString>(diagnostic::documentInvalid));
    QCOMPARE(bindSource(d, kOnePixelPng, QStringLiteral("image.png"), QString(), SourceChoice(-1)).error,
             std::optional<QString>(diagnostic::documentInvalid));
    d.canvasWidth = 2;
    QCOMPARE(bindSource(d, kOnePixelPng, QStringLiteral("image.png"), QString()).error,
             std::optional<QString>(diagnostic::documentInvalid));
  }

  // ES-013: gzip-wrapped JSON round-trips with uppercase colours.
  void xannRoundTrips() {
    AnnotationDocument d = document();
    RectangleAnnotation r = rectangle(1, 2, 5, 6);
    r.style.fillColor = 0x80ABCDEF;
    d.annotations.emplace_back(r);
    const auto bytes = writeXann(d);
    QVERIFY(bytes);
    QCOMPARE(bytes->left(2), QByteArray("\x1f\x8b", 2));  // gzip magic
    const ParseResult parsed = readXann(*bytes);
    QVERIFY2(parsed.document, qPrintable(parsed.error.value_or(QString())));
    const auto &back = std::get<RectangleAnnotation>(parsed.document->annotations.at(0));
    QCOMPARE(back.id, r.id);
    QCOMPARE(back.style.fillColor, Argb{0x80ABCDEF});
    QCOMPARE(serializeDocument(*parsed.document).value(u"annotations").toArray().at(0).toObject().value(u"fillColor").toString(),
             QStringLiteral("#80ABCDEF"));
  }

  // ES-028: inflation stops at the limit.
  void decompressionLimitIsEnforced() {
    const auto bytes = writeXann(document());
    QVERIFY(bytes);
    QCOMPARE(readXann(*bytes, 64).error, std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(readXann(QByteArray("not gzip")).error, std::optional<QString>(diagnostic::documentInvalid));
  }

  // ES-017, ES-028: PNG headers are checked without decoding pixels.
  void embeddedImagesAreCheckedBeforeDecode() {
    QJsonObject payload = serializeDocument(document());
    payload.insert(QStringLiteral("embeddedImages"),
                   QJsonObject{{QStringLiteral("big"), QString::fromLatin1(pngHeader(200000, 1).toBase64())}});
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentTooLarge));
    payload.insert(QStringLiteral("embeddedImages"), QJsonObject{{QStringLiteral("bad"), QStringLiteral("@@@")}});
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
    payload.insert(QStringLiteral("embeddedImages"),
                   QJsonObject{{QStringLiteral("gif"), QString::fromLatin1(QByteArray("GIF89a-not-a-png-file!!!").toBase64())}});
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
  }

  // ES-029 priority: too large outranks invalid.
  void tooLargeOutranksInvalid() {
    QJsonObject payload = serializeDocument(document(200000, 1));
    payload.insert(QStringLiteral("imageHash"), QStringLiteral("md5:nope"));
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentTooLarge));
  }

  void lowercaseColoursAreAccepted() {
    QCOMPARE(parseColor(QStringLiteral("#ffff0000")), std::optional<Argb>(0xFFFF0000));
    QCOMPARE(formatColor(0xffab0000), QStringLiteral("#FFAB0000"));
  }

  // ES-011, ES-012: a sidecar failure keeps the session dirty.
  void sidecarFailureKeepsSessionDirty() {
    EditorSession session(document());
    session.createRectangle({1, 1}, {4, 4});
    QCOMPARE(session.recordSave(true, false), QStringList{diagnostic::sidecarSaveFailed});
    QVERIFY(session.dirty());
    QVERIFY(session.recordSave(true, true).isEmpty());
    QVERIFY(!session.dirty());
  }

  // ES-014: a document with annotations writes the default sidecar.
  void sidecarIsWrittenAndFailureReported() {
    AnnotationDocument d = document();
    d.annotations.emplace_back(rectangle(1, 1, 3, 3));
    MapStore store;
    const SidecarSaveResult saved = saveSidecar(store, QStringLiteral("a/shot.png"), d, false);
    QCOMPARE(saved.sidecarPath, std::optional<QString>(QStringLiteral("a/shot.png.xann")));
    QVERIFY(readXann(store.files.at(QStringLiteral("a/shot.png.xann"))).document);
    store.failWrites = true;
    QCOMPARE(saveSidecar(store, QStringLiteral("a/shot.png"), d, true).diagnostic,
             std::optional<QString>(diagnostic::sidecarSaveFailed));
  }

  // ES-024, ES-025: a quarter turn renders as the swapped rectangle.
  void quarterTurnSwapsExtents() {
    RectangleAnnotation turned = rectangle(2, 4, 8, 6);
    turned.style.rotationDegrees = -270;
    turned.style.fillColor = 0xFF00FF00;
    RectangleAnnotation upright = rectangle(4, 2, 6, 8);
    upright.style.fillColor = 0xFF00FF00;
    const ArgbImage source = solidImage(10, 10, 0xFFFFFFFF);
    const RenderResult a = render(source, {Annotation(turned)});
    const RenderResult b = render(source, {Annotation(upright)});
    QVERIFY(a.image && b.image);
    QVERIFY(a.image->pixels == b.image->pixels);
  }

  // ES-025: a rotated 2x2 square is a diamond of area 4. In each quadrant
  // the central pixel has area 2*sqrt(2)-2 and the two outside triangles
  // have area (sqrt(2)-1)^2/2. These expected bytes come from those areas,
  // not from another renderer or native preview.
  void diamondHasExactPolygonCoverage_data() {
    QTest::addColumn<double>("angle");
    for (double angle : {45.0, -45.0, 135.0, 405.0, -315.0}) {
      QTest::newRow(qPrintable(QString::number(angle))) << angle;
    }
  }
  void diamondHasExactPolygonCoverage() {
    QFETCH(double, angle);
    RectangleAnnotation r = rectangle(1, 1, 3, 3);
    r.style.rotationDegrees = angle;
    r.style.fillColor = 0xFFFFFFFF;
    r.style.strokeColor = 0;
    const RenderResult result = render(solidImage(4, 4, 0), {r});
    QVERIFY(result.image);
    const int alpha[4][4] = {{0, 22, 22, 0}, {22, 211, 211, 22}, {22, 211, 211, 22}, {0, 22, 22, 0}};
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const Argb expected = alpha[y][x] == 0 ? 0 : (Argb(alpha[y][x]) << 24) | 0xFFFFFF;
        QCOMPARE(result.image->at(x, y), expected);
      }
    }
  }

  // The 1x1 diamond has .25 fill coverage in each pixel. Its 1px stroke
  // grows to a 2x2 diamond with 2*sqrt(2)-2 coverage, and the inner box is
  // empty. Applying ES-025 with red fill, blue stroke and opacity .5 over
  // white gives (#FF9590FA) in each pixel.
  void rotatedFillAndStrokeApplyOpacityOnce() {
    RectangleAnnotation r = rectangle(.5, .5, 1.5, 1.5);
    r.style = {0xFF0000FF, 0xFFFF0000, 1, 45, .5};
    const RenderResult result = render(solidImage(2, 2, 0xFFFFFFFF), {r});
    QVERIFY(result.image);
    for (Argb pixel : result.image->pixels) QCOMPARE(pixel, Argb{0xFF9590FA});
  }

  void rotatedStrokeSubtractsInnerPolygon() {
    RectangleAnnotation r = rectangle(2, 2, 6, 6);
    r.style = {0xFFFF0000, 0, 1, 45, 1};
    const RenderResult result = render(solidImage(8, 8, 0), {r});
    QVERIFY(result.image);
    // This pixel is wholly inside the rotated inner square and has no fill.
    QCOMPARE(result.image->at(3, 3), Argb{0});
    QVERIFY((result.image->at(4, 1) >> 24) > 0);
    // Stroke area = 5^2 - 3^2 = 16; quantization of boundary pixels may
    // differ by at most half a byte per pixel.
    int alpha = 0;
    for (Argb pixel : result.image->pixels) alpha += pixel >> 24;
    QVERIFY(std::abs(alpha - 16 * 255) <= 32);
  }

  void exportRejectsInvalidGeometryAndClipsFarAwayObjects() {
    RectangleAnnotation r = rectangle(1e100, 1e100, 2e100, 2e100);
    const ArgbImage source = solidImage(2, 2, 0xFFFFFFFF);
    for (double angle : {0.0, 17.0}) {
      r.style.rotationDegrees = angle;
      const auto result = render(source, {r});
      QVERIFY(result.image);
      QVERIFY(result.image->pixels == source.pixels);
    }
    r.style.rotationDegrees = std::numeric_limits<double>::infinity();
    QCOMPARE(render(source, {r}).error, std::optional<RenderError>(RenderError::InvalidGeometry));
    r.style.rotationDegrees = 17;
    r.start.x = std::numeric_limits<double>::quiet_NaN();
    QCOMPARE(render(source, {r}).error, std::optional<RenderError>(RenderError::InvalidGeometry));
    QCOMPARE(render(ArgbImage{2, 2, {}}, {}).error, std::optional<RenderError>(RenderError::InvalidSource));
  }

  // ES-025: transparent destination channels become 0 once composited.
  void transparentDestinationIsNormalized() {
    const RenderResult result = render(solidImage(10, 1, 0x00123456), {Annotation(rectangle(0, 0, 1, 1))});
    QVERIFY(result.image);
    QCOMPARE(result.image->at(9, 0), Argb{0});  // outside the 4-pixel stroke
  }

  // ES-006: redo restores the selection the redone state had.
  void redoRestoresSelection() {
    EditorSession session(document());
    session.createRectangle({1, 1}, {4, 4});
    session.deleteSelection();
    session.undo();
    QCOMPARE(session.selection().size(), 1);
    session.redo();
    QCOMPARE(session.selection().size(), 0);
    QCOMPARE(session.document().annotations.size(), std::size_t{0});
  }

  void resizeRotateAndReorder() {
    EditorSession session(document());
    const QUuid a = *session.createRectangle({1, 1}, {4, 4});
    const QUuid b = *session.createRectangle({2, 2}, {5, 5});
    session.select({a});
    QVERIFY(!session.resizeSelection(-3, 0));  // would collapse to zero width
    QVERIFY(session.resizeSelection(2, 1));
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).end.x, 6.0);
    QVERIFY(session.rotateSelection(-90));
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).style.rotationDegrees, 270.0);
    QVERIFY(session.reorderSelection(EditorSession::Order::Front));
    QCOMPARE(annotationId(session.document().annotations[1]), a);
    QVERIFY(!session.reorderSelection(EditorSession::Order::Front));  // already on top
    QVERIFY(session.reorderSelection(EditorSession::Order::Backward));
    QCOMPARE(annotationId(session.document().annotations[0]), a);
    QCOMPARE(annotationId(session.document().annotations[1]), b);
    const int undos = session.undoCount();
    session.undo();
    QCOMPARE(session.undoCount(), undos - 1);  // each was one operation
  }
};

QTEST_GUILESS_MAIN(EditorSessionTest)
#include "EditorSessionTest.moc"
