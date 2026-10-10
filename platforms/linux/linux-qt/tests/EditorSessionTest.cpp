// Production-path checks that the conformance vectors cannot cover: the .xann
// gzip container and its decompression limit, PNG header checks, save
// outcomes, sidecar writing, colour case, and rectangle rendering.

#include "image-editor/AnnotationRenderer.h"
#include "image-editor/EditorSession.h"

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
