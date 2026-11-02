// EC-004 / EA-010: real native source selection, embedded assets and history.
#include "EditorCanvas.h"
#include "ImageInsertDialog.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QSemaphore>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QImage source() { QImage image(800, 600, QImage::Format_ARGB32); image.fill(Qt::white); return image; }
QImage asset() {
  QImage image(4, 2, QImage::Format_ARGB32);
  for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) image.setPixel(x, y, qRgba(x * 60, y * 110, 90, 30 + x * 60));
  return image;
}
AnnotationDocument document() {
  AnnotationDocument result; result.canvasWidth = 800; result.canvasHeight = 600;
  result.createdAt = result.modifiedAt = "2026-11-02T00:00:00Z";
  QBuffer encoded(&result.sourceImagePng); encoded.open(QIODevice::WriteOnly); source().save(&encoded, "PNG");
  return result;
}
void begin(EditorCanvas &canvas, QPointF first = {100, 100}, QPointF last = {300, 300}) {
  canvas.resize(820, 640); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom(); canvas.setTool(EditorCanvas::Tool::Image);
  QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView(first).toPoint());
  QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView(last).toPoint());
}
}
class ImageInsertTest : public QObject {
  Q_OBJECT
private slots:
  void fileAndClipboard_data() {
    QTest::addColumn<bool>("clipboard"); QTest::newRow("file") << false; QTest::newRow("clipboard") << true;
  }
  void fileAndClipboard() {
    QFETCH(bool, clipboard); QTemporaryDir dir; const auto path = dir.filePath("asset.png"); const auto input = asset(); QVERIFY(input.save(path));
    auto pixels = source(); EditorSession session(document()); const auto original = serializeDocument(session.document());
    EditorCanvas canvas(session, pixels); begin(canvas);
    auto *dialog = canvas.findChild<ImageInsertDialog *>(); QVERIFY(dialog); QVERIFY(canvas.interactionActive());
    QCOMPARE(session.undoCount(), 0);
    if (clipboard) {
      QApplication::clipboard()->setImage(input);
      QTest::mouseClick(dialog->findChild<QPushButton *>("insertImageClipboard"), Qt::LeftButton);
      // The selected clipboard pixels were frozen before another application replaced them.
      QImage next(2, 5, QImage::Format_ARGB32); next.fill(Qt::green); QApplication::clipboard()->setImage(next);
    } else {
      QTest::mouseClick(dialog->findChild<QPushButton *>("insertImageFile"), Qt::LeftButton);
      auto *picker = dialog->findChild<QFileDialog *>("insertImageFilePicker"); QVERIFY(picker);
      QTRY_VERIFY(picker->isVisible()); picker->setDirectory(dir.path()); picker->selectFile(path);
      QTRY_COMPARE(picker->selectedFiles(), QStringList{path});
      QSignalSpy selected(picker, &QFileDialog::fileSelected);
      QVERIFY(QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection));
      QTRY_COMPARE(selected.count(), 1);
    }
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), std::size_t(1));
    const auto *shape = session.selectedRectangle(); QVERIFY(shape); QCOMPARE(shape->type(), shape::image);
    QCOMPARE(shape->right() - shape->left(), 200.0); QCOMPARE(shape->bottom() - shape->top(), 100.0);
    QCOMPARE(session.document().embeddedImages.size(), std::size_t(1));
    QCOMPARE(QImage::fromData(session.document().embeddedImages.front().second, "PNG"), input);
    QCOMPARE(pixels, source());
    const auto stored = serializeDocument(session.document()); const auto xann = writeXann(session.document()); QVERIFY(xann);
    const auto reopened = readXann(*xann); QVERIFY(reopened.document); QCOMPARE(serializeDocument(*reopened.document), stored);
    const auto key = shape->content.imageKey; QCOMPARE(reopened.document->embeddedImages.front().first, key);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), original);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), stored);
    if (!clipboard) QCOMPARE(QImage(path), input);
  }
  void clickUsesNaturalAspectAndSize() {
    const auto pixels = source(); EditorSession session(document()); EditorCanvas canvas(session, pixels); begin(canvas, {100, 100}, {100, 100});
    auto *dialog = canvas.findChild<ImageInsertDialog *>(); QVERIFY(dialog); QApplication::clipboard()->setImage(asset()); dialog->useClipboard();
    QTRY_VERIFY(!canvas.interactionActive()); const auto *shape = session.selectedRectangle(); QVERIFY(shape);
    QCOMPARE(shape->right() - shape->left(), 4.0); QCOMPARE(shape->bottom() - shape->top(), 2.0); QCOMPARE(session.undoCount(), 1);
  }
  void sourceValidationAndCancellation() {
    QTemporaryDir dir; QFile file(dir.filePath("bad.ppm")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("P6\n16385 1\n255\n"); file.close();
    QCOMPARE(loadImageAsset(file.fileName()).diagnostic, QString("source-too-large"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("not an image"); file.close();
    QCOMPARE(loadImageAsset(file.fileName()).diagnostic, QString("source-unsupported"));
    QVERIFY(!prepareImageAsset({}).valid()); QVERIFY(!insertImageSizeAllowed({16385, 1}));
    QVERIFY(!insertImageSizeAllowed({8193, 8193})); QVERIFY(insertImageSizeAllowed({8192, 8192}));
    QCOMPARE(prepareImageAsset(asset(), {[] { return true; }, {}}).diagnostic, QString("canvas-cancelled"));
    QCOMPARE(loadImageAsset(file.fileName(), {[] { return true; }, {}}).diagnostic, QString("canvas-cancelled"));
    bool cancel = false; const auto result = prepareImageAsset(asset(), {[&] { return cancel; }, [&](int) { cancel = true; }});
    QVERIFY(!result.valid()); QCOMPARE(result.diagnostic, QString("canvas-cancelled"));
  }
  void fileOrientationAppliedExactlyOnce() {
    QTemporaryDir dir; QImage input(2, 3, QImage::Format_RGB32);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x) input.setPixel(x, y, qRgb(x * 150, y * 90, 40));
    QByteArray jpeg; QBuffer buffer(&jpeg); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(input.save(&buffer, "JPEG", 100)); buffer.close();
    const auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
    auto segment = QByteArray::fromHex("ffe1"); segment.append(char((exif.size() + 2) >> 8)); segment.append(char((exif.size() + 2) & 255)); segment.append(exif); jpeg.insert(2, segment);
    QFile file(dir.filePath("oriented.jpg")); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(jpeg), jpeg.size()); file.close();
    QImageReader reader(file.fileName()); reader.setAutoTransform(false); const auto raw = reader.read();
    const auto prepared = loadImageAsset(file.fileName()); QVERIFY(prepared.valid()); QCOMPARE(prepared.dimensions, QSize(3, 2));
    const auto decoded = QImage::fromData(prepared.png, "PNG");
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) QCOMPARE(decoded.pixel(x, y), raw.pixel(y, 2 - x));
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), jpeg);
  }
  void invalidSourcesKeepSessionAndClipboard() {
    const auto pixels = source(); EditorSession session(document()); const auto before = serializeDocument(session.document()); EditorCanvas canvas(session, pixels); begin(canvas);
    auto *dialog = canvas.findChild<ImageInsertDialog *>(); QVERIFY(dialog);
    QApplication::clipboard()->setText("private clipboard text"); dialog->useClipboard();
    QCOMPARE(QApplication::clipboard()->text(), QString("private clipboard text")); QCOMPARE(session.undoCount(), 0); QVERIFY(dialog->isVisible());
    QTemporaryDir dir; QFile file(dir.filePath("bad.png")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("broken"); file.close();
    QVERIFY(dialog->loadFile(file.fileName())); QTRY_VERIFY(!dialog->busy()); QVERIFY(dialog->isVisible()); QCOMPARE(session.undoCount(), 0);
    QVERIFY(dialog->findChild<QLabel *>("insertImageStatus")->text().contains("source-unsupported"));
    dialog->reject(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before);
  }
  void cancellationAndStaleAdmission_data() {
    QTest::addColumn<QString>("mode");
    for (const char *mode : {"escape", "close", "destroy-dialog", "tool", "source", "state", "selection", "disabled"}) QTest::newRow(mode) << QString(mode);
  }
  void cancellationAndStaleAdmission() {
    QFETCH(QString, mode); const auto pixels = source(); EditorSession session(document());
    if (mode == "selection") QVERIFY(session.createRectangle({20, 20}, {40, 40}));
    EditorCanvas canvas(session, pixels); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected); begin(canvas);
    auto *dialog = canvas.findChild<ImageInsertDialog *>(); QVERIFY(dialog);
    if (mode == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
    else if (mode == "close") dialog->close();
    else if (mode == "destroy-dialog") delete dialog;
    else if (mode == "tool") canvas.setTool(EditorCanvas::Tool::Select);
    else if (mode == "source") canvas.sourceChanged();
    else {
      if (mode == "state") QVERIFY(session.createRectangle({10, 10}, {50, 50}));
      if (mode == "selection") session.select({annotationId(session.document().annotations.front())});
      if (mode == "disabled") canvas.setEnabled(false);
      QApplication::clipboard()->setImage(asset()); dialog->useClipboard();
    }
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    if (mode == "state" || mode == "selection" || mode == "disabled") {
      QCOMPARE(rejected.count(), 1); QCOMPARE(rejected.front().front().toString(), QString("annotation-edit-stale"));
    }
  }
  void ownedWorkerRemainsResponsiveAndCannotPublishAfterDestruction() {
    struct State { QSemaphore entered, release; std::atomic_bool returned{false}, worker{false}; };
    auto state = std::make_shared<State>(); QPointer<ImageInsertDialog> dialog = new ImageInsertDialog(nullptr, {[state] {
      state->worker = QThread::currentThread() != qApp->thread(); state->entered.release(); state->release.acquire(); state->returned = true;
    }});
    const auto release = qScopeGuard([state] { state->release.release(); });
    dialog->show(); QApplication::clipboard()->setImage(asset()); dialog->useClipboard();
    QTRY_VERIFY(state->entered.available() == 1); QVERIFY(state->worker); QVERIFY(dialog->busy());
    bool heartbeat = false; QTimer::singleShot(0, qApp, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    delete dialog.data(); QVERIFY(!dialog); state->release.release(); QTRY_VERIFY(state->returned.load());
    QTest::qWait(30); // drain the worker completion; deleted owner receives nothing
  }
};
QTEST_MAIN(ImageInsertTest)
#include "ImageInsertTest.moc"
