// Actual native actions, modal dialogs, worker preparation, undo and persisted
// PNG/sidecar reopening. All images and files are temporary fixtures.
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "EditorSource.h"

#include <QAction>
#include <QColorSpace>
#include <QDialog>
#include <QFile>
#include <QSpinBox>
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
}  // namespace

class EditorWindowTest : public QObject {
  Q_OBJECT
private slots:
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
};

QTEST_MAIN(EditorWindowTest)
#include "EditorWindowTest.moc"
