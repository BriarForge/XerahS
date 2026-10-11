// Load/save path of the editor (ES-001, ES-011, ES-016, ES-029) over real files.

#include "EditorSource.h"
#include "RasterSource.h"
#include "ComparisonImages.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace xerahs::app;
using namespace xerahs::editor;

class EditorSourceTest : public QObject {
  Q_OBJECT
private slots:
  void rasterDecodePreservesAlphaProfileAndOriginalBytes() {
    QTemporaryDir dir;
    const QString path = dir.filePath("alpha.png");
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(qRgba(64, 128, 192, 80));
    image.setColorSpace(QColorSpace::SRgbLinear);
    QVERIFY(image.save(path));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray before = file.readAll(); file.close();
    const auto loaded = loadRaster(path);
    QVERIFY(loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.image, image);
    QCOMPARE(loaded.image.colorSpace(), image.colorSpace());
    QCOMPARE(loaded.fileBytes, before);
    int percent = 0;
    const auto cancelled = loadRaster(path, {[&] { return percent >= 20; }, [&](int p) { percent = p; }});
    QCOMPARE(cancelled.diagnostic, QStringLiteral("canvas-cancelled"));
    QVERIFY(cancelled.image.isNull() && cancelled.fileBytes.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
  }

  void orientationIsAppliedExactlyOnceToPixelsAndEmbeddedSource() {
    QTemporaryDir dir;
    QImage image(2, 3, QImage::Format_RGB32);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x)
      image.setPixel(x, y, qRgb(x * 150, y * 90, 40));
    QByteArray jpeg; QBuffer buffer(&jpeg); QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPEG", 100)); buffer.close();
    // Independent TIFF/EXIF fixture: little-endian orientation tag 6 (CW 90).
    const QByteArray exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
    QByteArray segment = QByteArray::fromHex("ffe1");
    segment.append(char((exif.size() + 2) >> 8)); segment.append(char((exif.size() + 2) & 255)); segment.append(exif);
    jpeg.insert(2, segment);
    const QString path = dir.filePath("oriented.jpg");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(jpeg), jpeg.size()); file.close();
    QImageReader rawReader(path); rawReader.setAutoTransform(false);
    const QImage raw = rawReader.read().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(raw.size(), QSize(2, 3));
    const auto loaded = loadRaster(path); QVERIFY(loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.image.size(), QSize(3, 2));
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x)
      QCOMPARE(loaded.image.pixel(x, y), raw.pixel(y, 2 - x));
    const auto source = loadSource(path); QVERIFY(source.document);
    QCOMPARE(source.image, loaded.image);
    const QImage embedded = QImage::fromData(source.document->sourceImagePng, "PNG");
    QCOMPARE(embedded, loaded.image);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), jpeg);
  }

  void rasterHeadersAreLimitedBeforePixelDecode() {
    QTemporaryDir dir;
    QByteArray bmp(54, '\0'); bmp[0] = 'B'; bmp[1] = 'M';
    auto put32 = [&](int offset, quint32 value) { qToLittleEndian(value, bmp.data() + offset); };
    put32(2, 54); put32(10, 54); put32(14, 40); put32(18, 100001); put32(22, 1);
    bmp[26] = 1; bmp[28] = 32;
    QFile file(dir.filePath("oversized.bmp")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bmp); file.close();
    QImageReader reader(file.fileName()); QVERIFY(reader.canRead()); QCOMPARE(reader.size(), QSize(100001, 1));
    const auto loaded = loadRaster(file.fileName());
    QCOMPARE(loaded.diagnostic, diagnostic::sourceTooLarge);
    QVERIFY(loaded.image.isNull() && loaded.fileBytes.isEmpty());
  }

  void comparisonLoadsTheRasterWithoutReadingOrChangingItsSidecar() {
    QTemporaryDir dir;
    const QString path = dir.filePath("raster.png");
    QImage image(2, 3, QImage::Format_ARGB32); image.fill(qRgba(20, 40, 60, 80)); QVERIFY(image.save(path));
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write("corrupt"); sidecar.close();
    QCOMPARE(loadSource(path).diagnostic, diagnostic::documentInvalid);
    const auto comparison = loadComparisonImage(path); QVERIFY(comparison.diagnostic.isEmpty());
    QCOMPARE(comparison.image.size(), image.size());
    QCOMPARE(comparison.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
      QCOMPARE(comparison.image.pixel(x, y), image.pixel(x, y));
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), QByteArray("corrupt"));
    QCOMPARE(QImage(path), image);
  }

  void rejectsNonImages() {
    QTemporaryDir dir;
    QFile f(dir.filePath("bad.png"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not an image");
    f.close();
    QCOMPARE(loadSource(f.fileName()).diagnostic, diagnostic::sourceUnsupported);
    QFile empty(dir.filePath("empty.png"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QCOMPARE(loadSource(empty.fileName()).diagnostic, diagnostic::sourceEmpty);
    QCOMPARE(loadSource(dir.filePath("missing.png")).diagnostic, diagnostic::sourceUnsupported);
  }

  void saveThenReopenKeepsAnnotationsEditable() {
    QTemporaryDir dir;
    const QString path = dir.filePath("shot.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    QVERIFY(blank.save(path));

    LoadedSource first = loadSource(path);
    QVERIFY(first.document);
    QCOMPARE(first.document->canvasWidth, 40);
    EditorSession session(*first.document);
    QVERIFY(session.createRectangle({5, 5}, {20, 20}));

    const SaveOutcome outcome = saveEdit(path, first.image, session.document());
    QVERIFY(outcome.rasterSaved);
    QVERIFY(outcome.sidecarSaved);
    QVERIFY(session.recordSave(outcome.rasterSaved, outcome.sidecarSaved).isEmpty());
    QVERIFY(!session.dirty());
    QVERIFY(QFile::exists(path + ".xann"));
    QCOMPARE(QImage(path).pixel(5, 12), qRgba(255, 0, 0, 255));  // flattened export

    // Reopening edits the embedded unannotated source, not the flattened raster.
    LoadedSource second = loadSource(path);
    QVERIFY(second.document);
    QVERIFY(second.warnings.isEmpty());
    QCOMPARE(second.document->annotations.size(), size_t(1));
    QCOMPARE(second.image.pixel(5, 12), qRgba(255, 255, 255, 255));
  }

  void changedRasterRequiresChoiceAndKeepsValidatedSnapshots() {
    QTemporaryDir dir;
    const QString path = dir.filePath("shot.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    QVERIFY(blank.save(path));
    LoadedSource first = loadSource(path);
    EditorSession session(*first.document);
    session.createRectangle({5, 5}, {20, 20});
    QVERIFY(saveEdit(path, first.image, session.document()).rasterSaved);
    QImage other(24, 20, QImage::Format_ARGB32);
    other.fill(Qt::black);
    QVERIFY(other.save(path));
    LoadedSource reopened = loadSource(path);
    QVERIFY(!reopened.document && reopened.pendingChoice && reopened.image.isNull());
    QVERIFY(resolveSourceChoice(reopened, SourceChoice::Unspecified).pendingChoice);
    // A file changes again while the user decides: use the validated snapshot.
    QImage later(10, 10, QImage::Format_ARGB32); later.fill(Qt::blue); QVERIFY(later.save(path));
    const auto current = resolveSourceChoice(reopened, SourceChoice::CurrentRaster);
    QVERIFY(current.document); QCOMPARE(current.image, other); QCOMPARE(current.document->canvasWidth, 24);
    const auto embedded = resolveSourceChoice(reopened, SourceChoice::EmbeddedSource);
    QVERIFY(embedded.document); QCOMPARE(embedded.image, blank); QCOMPARE(embedded.document->canvasWidth, 40);
    QCOMPARE(embedded.document->annotations.size(), size_t(1));
    QCOMPARE(serializeDocument(*current.document).value(u"annotations"), serializeDocument(*embedded.document).value(u"annotations"));
    QCOMPARE(QImage(path), later);  // no choice rewrites the user's file
  }

  void invalidOrNewerSidecarsNeverCreateABareRasterSession() {
    QTemporaryDir dir; const QString path = dir.filePath("safe.png");
    QImage image(4, 3, QImage::Format_ARGB32); image.fill(Qt::white); QVERIFY(image.save(path));
    const auto initial = loadSource(path); QVERIFY(initial.document);
    auto document = *initial.document; document.version = 2;
    const auto newer = writeXann(document); QVERIFY(newer);
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); QCOMPARE(sidecar.write(*newer), newer->size()); sidecar.close();
    const auto failed = loadSource(path);
    QVERIFY(!failed.document && !failed.pendingChoice); QCOMPARE(failed.diagnostic, diagnostic::documentVersionUnsupported);
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), *newer); sidecar.close(); QCOMPARE(QImage(path), image);
    QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate)); sidecar.write("corrupt"); sidecar.close();
    const auto corrupt = loadSource(path); QVERIFY(!corrupt.document); QCOMPARE(corrupt.diagnostic, diagnostic::documentInvalid);
    document.version = 1; document.sourceImagePng.truncate(29);  // header passes; pixel decoding must fail
    const auto truncated = writeXann(document); QVERIFY(truncated);
    QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate)); sidecar.write(*truncated); sidecar.close();
    const auto damagedSource = loadSource(path); QVERIFY(!damagedSource.document); QCOMPARE(damagedSource.diagnostic, diagnostic::documentInvalid);
  }

  void freeAngleRotationSavesAndReopensWithHistory() {
    QTemporaryDir dir;
    const QString path = dir.filePath("rotated.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::transparent);
    QVERIFY(blank.save(path));
    const LoadedSource first = loadSource(path);
    QVERIFY(first.document);
    EditorSession session(*first.document);
    QVERIFY(session.createRectangle({10, 5}, {30, 25}));
    QVERIFY(session.rotateSelection(17));
    QCOMPARE(session.undoCount(), 2);
    QVERIFY(session.undo());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).style.rotationDegrees, 0);
    QVERIFY(session.redo());
    const auto outcome = saveEdit(path, first.image, session.document());
    QVERIFY(outcome.rasterSaved && outcome.sidecarSaved);
    QVERIFY(session.recordSave(outcome.rasterSaved, outcome.sidecarSaved).isEmpty());
    const LoadedSource reopened = loadSource(path);
    QVERIFY(reopened.document);
    QVERIFY(reopened.warnings.isEmpty());
    QCOMPARE(std::get<RectangleAnnotation>(reopened.document->annotations[0]).style.rotationDegrees, 17);
    QCOMPARE(reopened.image.pixel(10, 15), qRgba(0, 0, 0, 0));
    QVERIFY(qAlpha(QImage(path).pixel(10, 15)) > 0);
  }
};

QTEST_MAIN(EditorSourceTest)
#include "EditorSourceTest.moc"
