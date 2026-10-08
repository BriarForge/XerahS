// Production-path checks that the conformance vectors cannot cover: the .xann
// gzip container and its decompression limit, PNG header checks, save
// outcomes, sidecar writing, colour case, and quarter-turn rendering.

#include "image-editor/AnnotationRenderer.h"
#include "image-editor/EditorSession.h"

#include <QJsonArray>
#include <QTest>

#include <map>

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
    turned.style.rotationDegrees = 45;
    QCOMPARE(render(source, {Annotation(turned)}).error, std::optional<RenderError>(RenderError::UnsupportedRotation));
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
};

QTEST_GUILESS_MAIN(EditorSessionTest)
#include "EditorSessionTest.moc"
