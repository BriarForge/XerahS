// Actual native actions, modal dialogs, worker preparation, undo and persisted
// PNG/sidecar reopening. All images and files are temporary fixtures.
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "EditorSource.h"
#include "CanvasRotationDialog.h"
#include "ImageComparisonDialog.h"

#include <QAction>
#include <QColorSpace>
#include <QDialog>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QProgressDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QListWidget>
#include <QSpinBox>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
QImage fixture() {
  QImage image(60, 40, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace::SRgb);
  for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
    image.setPixel(x, y, qRgba(x * 3, y * 5, 100, 255));
  return image;
}
void acceptDimensions(int width, int height, int left = 0, int top = 0) {
  QTimer::singleShot(0, [=] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    QVERIFY(dialog);
    auto *w = dialog->findChild<QSpinBox *>(QStringLiteral("canvasWidth"));
    auto *h = dialog->findChild<QSpinBox *>(QStringLiteral("canvasHeight"));
    QVERIFY(w && h); w->setValue(width); h->setValue(height);
    if (auto *l = dialog->findChild<QSpinBox *>(QStringLiteral("cropLeft"))) l->setValue(left);
    if (auto *t = dialog->findChild<QSpinBox *>(QStringLiteral("cropTop"))) t->setValue(top);
    dialog->accept();
  });
}
void trigger(EditorWindow &window, const QString &name) {
  auto *action = window.findChild<QAction *>(name);
  QVERIFY(action); action->trigger();
}
void runRotationPreview(EditorWindow &window, bool accept) {
  QTimer timer; QElapsedTimer deadline; deadline.start();
  bool configured = false, intermediateReady = false, previewReady = false, timedOut = false;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *dialog = qobject_cast<CanvasRotationDialog *>(QApplication::activeModalWidget());
    if (!dialog) return;
    if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
    if (!configured) {
      auto *angle = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("rotationAngle"));
      auto *interpolation = dialog->findChild<QComboBox *>(QStringLiteral("rotationInterpolation"));
      auto *expand = dialog->findChild<QCheckBox *>(QStringLiteral("rotationExpand"));
      QVERIFY(angle && interpolation && expand);
      angle->setValue(11); interpolation->setCurrentIndex(1); expand->setChecked(false);
      configured = true;
    }
    auto *status = dialog->findChild<QLabel *>(QStringLiteral("rotationStatus")); QVERIFY(status);
    if (!intermediateReady && status->text().startsWith(QStringLiteral("Preview: 60 × 40"))) {
      intermediateReady = true;
      dialog->findChild<QDoubleSpinBox *>(QStringLiteral("rotationAngle"))->setValue(17);
      dialog->findChild<QComboBox *>(QStringLiteral("rotationInterpolation"))->setCurrentIndex(0);
      dialog->findChild<QCheckBox *>(QStringLiteral("rotationExpand"))->setChecked(true);
      dialog->accept();  // The prior prepared preview can no longer be accepted.
      QVERIFY(dialog->isVisible());
    }
    if (intermediateReady && status->text().startsWith(QStringLiteral("Preview: 70 × 56"))) {
      previewReady = true; accept ? dialog->accept() : dialog->reject();
    }
  });
  timer.start(10);
  trigger(window, QStringLiteral("rotateImage")); timer.stop();
  QVERIFY(configured && intermediateReady && previewReady && !timedOut);
}
}  // namespace

class EditorWindowTest : public QObject {
  Q_OBJECT
private slots:
  void comparisonPreservesDirtyHistorySelectionViewportAndFiles() {
    QTemporaryDir dir;
    const QString path = dir.filePath("current.png"), otherPath = dir.filePath("other.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    QImage other(40, 60, QImage::Format_ARGB32); other.fill(Qt::blue); QVERIFY(other.save(otherPath));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({22, 16}).toPoint());
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    trigger(*window, QStringLiteral("undo"));  // both undo and redo branches must survive comparison
    canvas->zoomIn();
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    auto *undo = window->findChild<QAction *>("undo"), *redo = window->findChild<QAction *>("redo");
    auto *remove = window->findChild<QAction *>("deleteSelection");
    QVERIFY(undo && redo && remove); QVERIFY(undo->isEnabled() && redo->isEnabled() && remove->isEnabled());
    bool loaded = false, exercised = false, timedOut = false;
    QTimer timer; QElapsedTimer deadline; deadline.start();
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<ImageComparisonDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      auto *first = dialog->findChild<QLabel *>("comparisonInput0"); QVERIFY(first);
      if (!loaded && first->text().startsWith("Current rendered image")) { loaded = dialog->loadFile(1, otherPath); }
      auto *slider = dialog->findChild<QSlider *>("comparisonDivider"); QVERIFY(slider);
      if (!slider->isEnabled()) return;
      dialog->findChild<QComboBox *>("comparisonAlignment")->setCurrentIndex(1);
      slider->setValue(75); QTest::keyClick(slider, Qt::Key_Left); QCOMPARE(slider->value(), 74);
      QVERIFY(!window->close());  // snapshots stay alive until the comparison closes
      exercised = true; dialog->reject();
    });
    timer.start(10); trigger(*window, QStringLiteral("compareImages")); timer.stop();
    QVERIFY(loaded && exercised && !timedOut);
    QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
    QVERIFY(undo->isEnabled() && redo->isEnabled() && remove->isEnabled());
    QCOMPARE(QImage(path), original); QCOMPARE(QImage(otherPath), other);
    QVERIFY(!QFile::exists(path + ".xann") && !QFile::exists(otherPath + ".xann"));
    trigger(*window, QStringLiteral("redo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("40 × 60")));
    trigger(*window, QStringLiteral("undo")); trigger(*window, QStringLiteral("saveImage"));
    const auto saved = loadSource(path); QVERIFY(saved.document);
    QCOMPARE(saved.document->annotations.size(), std::size_t(1)); QCOMPARE(saved.image, original);
    const auto &rectangle = std::get<RectangleAnnotation>(saved.document->annotations[0]);
    QCOMPARE(rectangle.style.rotationDegrees, 0);
    QVERIFY(std::abs(rectangle.start.x - 8) <= .51 && std::abs(rectangle.end.x - 22) <= .51);
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("0 annotation(s)")));
    QVERIFY(!undo->isEnabled());
  }

  void nativeSourceMismatchChoice_data() {
    QTest::addColumn<int>("choice");
    QTest::newRow("cancel") << int(SourceChoice::Unspecified);
    QTest::newRow("current") << int(SourceChoice::CurrentRaster);
    QTest::newRow("embedded") << int(SourceChoice::EmbeddedSource);
  }
  void nativeSourceMismatchChoice() {
    QFETCH(int, choice);
    QTemporaryDir dir; const QString path = dir.filePath("mismatch.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    const auto first = loadSource(path); QVERIFY(first.document);
    EditorSession session(*first.document); QVERIFY(session.createRectangle({8, 8}, {22, 16}));
    const auto saved = saveEdit(path, original, session.document()); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    QImage current(24, 20, QImage::Format_ARGB32); current.fill(Qt::black); QVERIFY(current.save(path));
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::ReadOnly)); const QByteArray sidecarBefore = sidecar.readAll(); sidecar.close();
    QTimer::singleShot(0, [choice] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
      if (choice == int(SourceChoice::Unspecified)) { box->button(QMessageBox::Cancel)->click(); return; }
      const QString prefix = choice == int(SourceChoice::CurrentRaster) ? QStringLiteral("Current raster") : QStringLiteral("Embedded source");
      for (auto *button : box->buttons()) if (button->text().startsWith(prefix)) { button->click(); return; }
      QFAIL("The source-choice dialog did not expose both validated images.");
    });
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path));
    if (choice == int(SourceChoice::Unspecified)) QVERIFY(!window);
    else {
      QVERIFY(window);
      const QString dimensions = choice == int(SourceChoice::CurrentRaster) ? QStringLiteral("24 × 20") : QStringLiteral("60 × 40");
      QVERIFY(window->statusBar()->currentMessage().startsWith(dimensions));
      QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("1 annotation(s)")));
      QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    }
    QCOMPARE(QImage(path), current);
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), sidecarBefore);
  }

  void cropIsUndoableAndReopensTheCroppedSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("crop.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    canvas->resetZoom();
    const QPoint a = canvas->viewState().toView({8, 9}).toPoint();
    const QPoint b = canvas->viewState().toView({24, 25}).toPoint();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
    acceptDimensions(40, 20, 5, 6);
    trigger(*window, QStringLiteral("cropImage"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("40 × 20")));
    QCOMPARE(QImage(path).size(), original.size());  // editing never overwrites source
    trigger(*window, QStringLiteral("saveImage"));
    QCOMPARE(QImage(path).size(), QSize(40, 20));
    QCOMPARE(QImage(path).pixel(39, 0), original.pixel(44, 6));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QVERIFY(reopened.warnings.isEmpty());
    QCOMPARE(reopened.document->annotations.size(), std::size_t(1));
    const auto &r = std::get<RectangleAnnotation>(reopened.document->annotations[0]);
    QVERIFY(std::abs(r.start.x - 3) <= .51 && std::abs(r.start.y - 3) <= .51);
    QVERIFY(std::abs(r.end.x - 19) <= .51 && std::abs(r.end.y - 19) <= .51);
    QCOMPARE(reopened.image.size(), QSize(40, 20));
    QCOMPARE(reopened.image.colorSpace(), original.colorSpace());
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
    QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, QStringLiteral("redo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("40 × 20")));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void canvasResizeDoesNotResampleAndUndoRestoresTheSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("canvas.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    acceptDimensions(64, 44);
    trigger(*window, QStringLiteral("resizeCanvas"));
    trigger(*window, QStringLiteral("saveImage"));
    const QImage saved(path); QCOMPARE(saved.size(), QSize(64, 44));
    QCOMPARE(qAlpha(saved.pixel(0, 0)), 0);
    for (int y = 0; y < original.height(); ++y) for (int x = 0; x < original.width(); ++x)
      QCOMPARE(saved.pixel(x + 2, y + 2), original.pixel(x, y));
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
  }

  void imageRotationPersistsPixelsAndUndoSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("rotation.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    trigger(*window, QStringLiteral("saveImage"));
    const QImage saved(path); QCOMPARE(saved.size(), QSize(40, 60));
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 40; ++x) QCOMPARE(saved.pixel(x, y), original.pixel(y, 39 - x));
    QCOMPARE(saved.colorSpace(), original.colorSpace());
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
  }

  void cancelledDialogPreservesCleanHistoryAndSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("cancel.png");
    QVERIFY(fixture().save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    const QString status = window->statusBar()->currentMessage();
    QTimer::singleShot(0, [] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject();
    });
    trigger(*window, QStringLiteral("resizeImage"));
    QCOMPARE(window->statusBar()->currentMessage(), status);
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void nativeAutoCropPersistsAlphaAndCreatesExactlyOneUndoStep() {
    QTemporaryDir dir; const QString path = dir.filePath("auto.png");
    QImage original(8, 6, QImage::Format_ARGB32); original.fill(Qt::transparent);
    original.setColorSpace(QColorSpace::SRgb);
    for (int y = 1; y < 5; ++y) for (int x = 2; x < 6; ++x) original.setPixel(x, y, qRgba(90, 20, 40, 128));
    QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show();
    const auto runCrop = [&] {
      QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        QVERIFY(dialog->findChild<QSpinBox *>(QStringLiteral("cropAlpha"))); dialog->accept();
      });
      trigger(*window, QStringLiteral("autoCrop"));
    };
    runCrop();
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("4 × 4")));
    runCrop();  // No border: no extra history entry.
    trigger(*window, QStringLiteral("saveImage"));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image.size(), QSize(4, 4)); QCOMPARE(reopened.image.colorSpace(), original.colorSpace());
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) QCOMPARE(reopened.image.pixel(x, y), original.pixel(x + 2, y + 1));
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("8 × 6")));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    const QString status = window->statusBar()->currentMessage();
    QTimer::singleShot(0, [] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject(); });
    trigger(*window, QStringLiteral("autoCrop"));
    QCOMPARE(window->statusBar()->currentMessage(), status);
    QVERIFY(window->findChild<QAction *>(QStringLiteral("redo"))->isEnabled());
    trigger(*window, QStringLiteral("redo")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void cancelledSeventeenDegreePreviewPreservesDirtyAnnotationsAndHistory() {
    QTemporaryDir dir; const QString path = dir.filePath("cancel-rotation.png");
    const QImage source = fixture(); QVERIFY(source.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {8, 8}; r.end = {22, 16}; r.style.rotationDegrees = 17;
    loaded.document->annotations = {r};
    const auto saved = saveEdit(path, source, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({35, 25}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({45, 30}).toPoint());
    const QString before = window->statusBar()->currentMessage(); const QString title = window->windowTitle();
    QVERIFY(title.contains(QChar(0x2022)));
    runRotationPreview(*window, false);
    QCOMPARE(window->statusBar()->currentMessage(), before); QCOMPARE(window->windowTitle(), title);
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, QStringLiteral("saveImage"));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image, source); QCOMPARE(reopened.document->annotations.size(), std::size_t(1));
    const auto &retained = std::get<RectangleAnnotation>(reopened.document->annotations[0]);
    QCOMPARE(retained.id, r.id); QCOMPARE(retained.start, r.start); QCOMPARE(retained.end, r.end); QCOMPARE(retained.style.rotationDegrees, 17);
  }

  void acceptedPreviewPersistsTransformedSourceAndEditableAnnotationsOnce() {
    QTemporaryDir dir; const QString path = dir.filePath("custom-rotation.png");
    const QImage source = fixture(); QVERIFY(source.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {8, 8}; r.end = {22, 16}; r.style.rotationDegrees = 17;
    loaded.document->annotations = {r};
    const auto saved = saveEdit(path, source, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    runRotationPreview(*window, true);
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("70 × 56")));
    trigger(*window, QStringLiteral("saveImage"));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image.size(), QSize(70, 56)); QCOMPARE(reopened.image.colorSpace(), source.colorSpace());
    QCOMPARE(reopened.image.pixel(35, 28), source.pixel(30, 20)); QCOMPARE(qAlpha(reopened.image.pixel(0, 0)), 0);
    QCOMPARE(reopened.document->annotations.size(), std::size_t(1));
    const auto &moved = std::get<RectangleAnnotation>(reopened.document->annotations[0]);
    QCOMPARE(moved.id, r.id); QCOMPARE(moved.style.rotationDegrees, 34);
    QCOMPARE(moved.right() - moved.left(), 14.0); QCOMPARE(moved.bottom() - moved.top(), 8.0);
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    trigger(*window, QStringLiteral("redo"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void nativePreviewCancellationWaitsForTheLiveWorkerWithoutCommitting() {
    QTemporaryDir dir; const QString path = dir.filePath("large-preview.png");
    QImage source(1500, 1000, QImage::Format_ARGB32); source.fill(Qt::red); QVERIFY(source.save(path));
    const auto loaded = loadSource(path); QVERIFY(loaded.document);
    CanvasRotationDialog dialog(*loaded.document, source);
    dialog.findChild<QDoubleSpinBox *>(QStringLiteral("rotationAngle"))->setValue(17);
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool cancelledLiveWorker = false;
    connect(&timer, &QTimer::timeout, &dialog, [&] {
      const int progress = dialog.findChild<QProgressBar *>()->value();
      if (progress > 0 && progress < 100) { cancelledLiveWorker = true; dialog.reject(); timer.stop(); }
      else if (deadline.elapsed() > 5000) { dialog.reject(); timer.stop(); }
    });
    timer.start(5);
    QCOMPARE(dialog.exec(), int(QDialog::Rejected)); QVERIFY(cancelledLiveWorker);
    QVERIFY(!dialog.takeEdit());
    QCOMPARE(QImage(path), source);
  }

  void flattenPersistsOnlyRenderedObjectsAndShowsUnsupportedPlaceholders() {
    QTemporaryDir dir; const QString path = dir.filePath("flatten.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation shown; shown.id = QUuid::createUuid(); shown.start = {1, 1}; shown.end = {4, 4};
    shown.style.strokeColor = 0; shown.style.fillColor = 0xFF00FF00;
    RectangleAnnotation hidden = shown; hidden.id = QUuid::createUuid(); hidden.visible = false;
    const QUuid id = QUuid::createUuid();
    UnsupportedAnnotation future{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"visible", false}, {"vendor", "preserve"}}};
    loaded.document->annotations = {shown, hidden, future};
    const auto saved = saveEdit(path, original, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *placeholders = window->findChild<QListWidget *>(QStringLiteral("unsupportedAnnotations")); QVERIFY(placeholders);
    QCOMPARE(placeholders->count(), 1); QVERIFY(placeholders->isVisible());
    QVERIFY(placeholders->item(0)->text().contains(QStringLiteral("future-art")));
    trigger(*window, QStringLiteral("flattenImage"));
    QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("2 annotation(s)")));
    trigger(*window, QStringLiteral("saveImage"));
    const auto flattened = loadSource(path); QVERIFY(flattened.document);
    QCOMPARE(flattened.image.pixel(2, 2), QRgb(0xFF00FF00)); QCOMPARE(flattened.image.colorSpace(), original.colorSpace());
    QCOMPARE(annotationId(flattened.document->annotations[0]), hidden.id);
    QCOMPARE(std::get<UnsupportedAnnotation>(flattened.document->annotations[1]).raw.value(u"vendor").toString(), QStringLiteral("preserve"));
    trigger(*window, QStringLiteral("flattenImage"));  // no represented annotations: no second history entry
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    trigger(*window, QStringLiteral("saveImage"));
    const auto restored = loadSource(path); QVERIFY(restored.document); QCOMPARE(restored.image, original);
    QCOMPARE(restored.document->annotations.size(), std::size_t(3));
  }

  void clearAnnotationsKeepsSourcePixelsAndRestoresUnsupportedObjectsWithUndo() {
    QTemporaryDir dir; const QString path = dir.filePath("clear-objects.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    const QUuid id = QUuid::createUuid();
    loaded.document->annotations = {UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "preserve"}}}};
    const auto saved = saveEdit(path, original, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    trigger(*window, QStringLiteral("clearAnnotations"));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("clearAnnotations"))->isEnabled());
    QCOMPARE(window->findChild<QListWidget *>(QStringLiteral("unsupportedAnnotations"))->count(), 0);
    trigger(*window, QStringLiteral("saveImage")); QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann"));
    trigger(*window, QStringLiteral("undo"));
    QCOMPARE(window->findChild<QListWidget *>(QStringLiteral("unsupportedAnnotations"))->count(), 1);
    trigger(*window, QStringLiteral("saveImage"));
    const auto restored = loadSource(path); QVERIFY(restored.document);
    QCOMPARE(std::get<UnsupportedAnnotation>(restored.document->annotations[0]).raw.value(u"vendor").toString(), QStringLiteral("preserve"));
  }

  void clearImageConfirmationAndRecovery_data() {
    QTest::addColumn<bool>("dirty"); QTest::addColumn<int>("answer");
    QTest::newRow("clean-cancel") << false << int(QMessageBox::Cancel);
    QTest::newRow("clean-clear") << false << int(QMessageBox::Ok);
    QTest::newRow("dirty-cancel") << true << int(QMessageBox::Cancel);
    QTest::newRow("dirty-save-first") << true << int(QMessageBox::Save);
    QTest::newRow("dirty-clear-without-saving") << true << int(QMessageBox::Discard);
  }
  void clearImageConfirmationAndRecovery() {
    QFETCH(bool, dirty); QFETCH(int, answer);
    QTemporaryDir dir; const QString path = dir.filePath("clear.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    if (dirty) {
      QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
      QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({22, 16}).toPoint());
    }
    const QString status = window->statusBar()->currentMessage(); const QString title = window->windowTitle();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    QTimer::singleShot(0, [answer] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
      auto *button = box->button(QMessageBox::StandardButton(answer)); QVERIFY(button); button->click();
    });
    trigger(*window, QStringLiteral("clearImage"));
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
    if (answer == QMessageBox::Cancel) { QCOMPARE(window->statusBar()->currentMessage(), status); QCOMPARE(window->windowTitle(), title); }
    else {
      QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("0 annotation(s)")));
      trigger(*window, QStringLiteral("undo")); QCOMPARE(window->statusBar()->currentMessage(), status);
      QCOMPARE(window->windowTitle().contains(QChar(0x2022)), dirty && answer != QMessageBox::Save);
      if (dirty) trigger(*window, QStringLiteral("undo"));
      QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    }
    if (answer != QMessageBox::Save) QCOMPARE(QImage(path), original);
    else { QVERIFY(QFile::exists(path + ".xann")); QVERIFY(QImage(path) != original); }
  }

  void clearedImageSavesTransparentPixelsAndUndoRestoresTheImmutableSource() {
    QTemporaryDir dir; const QString path = dir.filePath("transparent.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Ok)->click(); });
    trigger(*window, QStringLiteral("clearImage")); trigger(*window, QStringLiteral("saveImage"));
    const QImage cleared(path); QCOMPARE(cleared.size(), original.size()); QCOMPARE(cleared.colorSpace(), original.colorSpace());
    for (int y = 0; y < cleared.height(); ++y) for (int x = 0; x < cleared.width(); ++x) QCOMPARE(cleared.pixel(x, y), QRgb(0));
    trigger(*window, QStringLiteral("undo")); trigger(*window, QStringLiteral("saveImage")); QCOMPARE(QImage(path), original);
  }

  void cancellingNativeFlattenLeavesFilesDocumentAndHistoryUntouched() {
    QTemporaryDir dir; const QString path = dir.filePath("cancel-flatten.png");
    QImage original(600, 400, QImage::Format_ARGB32); original.fill(Qt::blue); QVERIFY(original.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {50, 50}; r.end = {550, 350};
    r.style.rotationDegrees = 17; r.style.fillColor = 0x80FF0000; loaded.document->annotations = {r};
    const auto bytes = writeXann(*loaded.document); QVERIFY(bytes);
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write(*bytes); sidecar.close();
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    const QString status = window->statusBar()->currentMessage(), title = window->windowTitle();
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool cancelledLive = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget());
      if (!progress) return;
      if ((progress->value() > 5 && progress->value() < 90) || deadline.elapsed() > 5000) {
        cancelledLive = progress->value() > 5 && progress->value() < 90;
        auto *button = progress->findChild<QPushButton *>(); QVERIFY(button); button->click();
        QVERIFY(!window->close());  // the editor stays alive until its worker has stopped
        timer.stop();
      }
    });
    timer.start(5); trigger(*window, QStringLiteral("flattenImage")); timer.stop();
    QVERIFY(cancelledLive); QCOMPARE(window->statusBar()->currentMessage(), status); QCOMPARE(window->windowTitle(), title);
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    QCOMPARE(QImage(path), original); QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), *bytes);
  }

  void failedSaveBeforeClearKeepsTheDirtySessionAndHistory() {
    QTemporaryDir dir; const QString path = dir.filePath("failed-save.png"), backup = dir.filePath("original.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({22, 16}).toPoint());
    const QString before = window->statusBar()->currentMessage(), title = window->windowTitle(); QVERIFY(title.contains(QChar(0x2022)));
    QVERIFY(QFile::rename(path, backup)); QVERIFY(QDir().mkdir(path));  // destination is now a directory
    QTimer timer; bool saveRequested = false, failureShown = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (box->windowTitle() == u"Clear Image and Annotations" && !saveRequested) { saveRequested = true; box->button(QMessageBox::Save)->click(); }
      else if (box->windowTitle() == u"Save incomplete") { failureShown = true; box->accept(); }
    });
    timer.start(5); trigger(*window, QStringLiteral("clearImage")); timer.stop();
    QVERIFY(saveRequested && failureShown); QCOMPARE(window->statusBar()->currentMessage(), before); QCOMPARE(window->windowTitle(), title);
    QVERIFY(QDir(path).exists()); QCOMPARE(QImage(backup), original);
    trigger(*window, QStringLiteral("undo")); QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
  }
};

QTEST_MAIN(EditorWindowTest)
#include "EditorWindowTest.moc"
