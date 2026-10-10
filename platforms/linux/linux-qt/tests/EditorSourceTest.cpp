// Load/save path of the editor (ES-001, ES-011, ES-016, ES-029) over real files.

#include "EditorSource.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;
using namespace xerahs::editor;

class EditorSourceTest : public QObject {
  Q_OBJECT
private slots:
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
