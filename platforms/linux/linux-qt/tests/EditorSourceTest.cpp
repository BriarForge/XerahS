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

  void changedRasterWarnsAndKeepsAnnotations() {
    QTemporaryDir dir;
    const QString path = dir.filePath("shot.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    QVERIFY(blank.save(path));
    LoadedSource first = loadSource(path);
    EditorSession session(*first.document);
    session.createRectangle({5, 5}, {20, 20});
    QVERIFY(saveEdit(path, first.image, session.document()).rasterSaved);
    QImage other(40, 30, QImage::Format_ARGB32);
    other.fill(Qt::black);
    QVERIFY(other.save(path));
    LoadedSource reopened = loadSource(path);
    QVERIFY(reopened.document);
    QCOMPARE(reopened.warnings.size(), 1);
    QCOMPARE(reopened.document->annotations.size(), size_t(1));
    QCOMPARE(reopened.image.pixel(0, 0), qRgba(0, 0, 0, 255));
  }
};

QTEST_MAIN(EditorSourceTest)
#include "EditorSourceTest.moc"
