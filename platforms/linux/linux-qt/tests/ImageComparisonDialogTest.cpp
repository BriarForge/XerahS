// EC-016/EC-017: actual native widgets, raster validation and worker lifetime.
#include "ImageComparisonDialog.h"
#include <QAccessible>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QImage solid(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}
QColor sample(ComparisonView &view, QPointF pixel) {
  return view.grab().toImage().pixelColor(view.toView(pixel).toPoint());
}
} // namespace
class ImageComparisonDialogTest : public QObject {
  Q_OBJECT
private slots:
  void nativeViewShowsNativeAlignedPixelsAndTransparentPadding() {
    ComparisonView view;
    view.resize(400, 400);
    view.show();
    QImage first = solid(4, 2, Qt::red), second = solid(2, 4, Qt::blue);
    const auto topLeft = comparisonLayout({4, 2}, {2, 4}, ComparisonAlignment::TopLeft);
    QVERIFY(topLeft.layout);
    view.setImages(first, second, *topLeft.layout);
    view.setReveal(25);
    QCOMPARE(sample(view, {.5, .5}), QColor(Qt::red));
    QCOMPARE(sample(view, {1.5, 3.5}), QColor(Qt::blue));
    const QColor padding = sample(view, {3.5, 3.5});
    QVERIFY(padding == QColor(Qt::white) || padding == QColor(Qt::lightGray));
    const auto centered = comparisonLayout({4, 2}, {2, 4}, ComparisonAlignment::Center);
    QVERIFY(centered.layout);
    view.setImages(first, second, *centered.layout);
    QCOMPARE(sample(view, {.5, 1.5}), QColor(Qt::red));
    QCOMPARE(sample(view, {2.5, .5}), QColor(Qt::blue));
    view.setReveal(100);
    QCOMPARE(sample(view, {3.5, 2.5}), QColor(Qt::red));
    view.setReveal(0);
    QCOMPARE(sample(view, {1.5, 3.5}), QColor(Qt::blue));
    QCOMPARE(first, solid(4, 2, Qt::red));
    QCOMPARE(second, solid(2, 4, Qt::blue));
  }

  void nativeControlsAreAccessibleAndKeyboardAndDragShareTheDivider() {
    QTemporaryDir dir;
    const QString secondPath = dir.filePath("second.png");
    QVERIFY(solid(2, 4, Qt::blue).save(secondPath));
    ImageComparisonDialog dialog({}, solid(4, 2, Qt::red));
    dialog.show();
    auto *first = dialog.findChild<QLabel *>("comparisonInput0");
    QVERIFY(first);
    QTRY_VERIFY(first->text().startsWith("Current rendered image"));
    QVERIFY(dialog.loadFile(1, secondPath));
    auto *slider = dialog.findChild<QSlider *>("comparisonDivider");
    QVERIFY(slider);
    QTRY_VERIFY(slider->isEnabled());
    auto *view = dialog.findChild<ComparisonView *>();
    QVERIFY(view);
    auto *accessible = QAccessible::queryAccessibleInterface(slider);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Slider);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("First image percentage"));
    QVERIFY(accessible->valueInterface());
    QCOMPARE(accessible->valueInterface()->minimumValue().toInt(), 0);
    QCOMPARE(accessible->valueInterface()->maximumValue().toInt(), 100);
    slider->setFocus();
    QTest::keyClick(slider, Qt::Key_Home);
    QCOMPARE(slider->value(), 0);
    QTest::keyClick(slider, Qt::Key_Right);
    QCOMPARE(slider->value(), 1);
    QTest::keyClick(slider, Qt::Key_End);
    QCOMPARE(slider->value(), 100);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Left);
    QCOMPARE(slider->value(), 99);
    QTest::keyClick(view, Qt::Key_Home);
    QCOMPARE(slider->value(), 0);
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, view->toView({1, 2}).toPoint());
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, view->toView({3, 2}).toPoint());
    QCOMPARE(slider->value(), 75);
    QCOMPARE(accessible->valueInterface()->currentValue().toInt(), 75);
    auto *alignment = dialog.findChild<QComboBox *>("comparisonAlignment");
    QVERIFY(alignment);
    alignment->setCurrentIndex(1);
    QVERIFY(view->comparisonLayout());
    QCOMPARE(view->comparisonLayout()->firstOffset, (PointF{0, 1}));
    QCOMPARE(view->comparisonLayout()->secondOffset, (PointF{1, 0}));
    dialog.reject();
    QVERIFY(!dialog.isVisible());
  }

  void cancelledPickerAndInvalidReplacementKeepThePriorPairAndDivider() {
    QTemporaryDir dir;
    const QString firstPath = dir.filePath("first.png"), secondPath = dir.filePath("second.png");
    QVERIFY(solid(5, 2, Qt::green).save(firstPath));
    QVERIFY(solid(2, 5, Qt::blue).save(secondPath));
    QFile corruptSidecar(firstPath + ".xann");
    QVERIFY(corruptSidecar.open(QIODevice::WriteOnly));
    corruptSidecar.write("corrupt");
    corruptSidecar.close();
    ImageComparisonDialog dialog({}, solid(4, 2, Qt::red));
    dialog.show();
    auto *first = dialog.findChild<QLabel *>("comparisonInput0");
    auto *second = dialog.findChild<QLabel *>("comparisonInput1");
    auto *choose = dialog.findChild<QPushButton *>("chooseComparisonInput0");
    auto *slider = dialog.findChild<QSlider *>("comparisonDivider");
    auto *status = dialog.findChild<QLabel *>("comparisonStatus");
    auto *view = dialog.findChild<ComparisonView *>();
    QVERIFY(first && second && choose && slider && status && view);
    QTRY_VERIFY(first->text().startsWith("Current rendered image"));
    QVERIFY(dialog.loadFile(0, firstPath));
    QTRY_VERIFY(first->text().startsWith("first.png"));
    QVERIFY(dialog.loadFile(1, secondPath));
    QTRY_VERIFY(slider->isEnabled());
    slider->setValue(20);
    const QString firstLabel = first->text(), secondLabel = second->text();
    const QColor before = sample(*view, {.5, .5});
    QCOMPARE(before, QColor(Qt::green));
    QFile bad(dir.filePath("bad.png"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("bad");
    bad.close();
    QVERIFY(dialog.loadFile(0, bad.fileName()));
    QTRY_VERIFY(status->text().contains("not a supported image"));
    QCOMPARE(first->text(), firstLabel);
    QCOMPARE(second->text(), secondLabel);
    QCOMPARE(slider->value(), 20);
    QCOMPARE(sample(*view, {.5, .5}), before);
    bool pickerCancelled = false;
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
        pickerCancelled = true;
        picker->reject();
        timer.stop();
      }
    });
    timer.start(10);
    choose->click();
    timer.stop();
    QVERIFY(pickerCancelled);
    QCOMPARE(first->text(), firstLabel);
    QCOMPARE(second->text(), secondLabel);
    QCOMPARE(slider->value(), 20);
    QVERIFY(corruptSidecar.open(QIODevice::ReadOnly));
    QCOMPARE(corruptSidecar.readAll(), QByteArray("corrupt"));
    dialog.reject();
  }

  void cancellationAndClosingWaitForTheLiveWorkerWithoutPublishingAnInput() {
    const QImage source = solid(600, 400, Qt::transparent), original = source;
    AnnotationDocument document;
    RectangleAnnotation rectangle;
    rectangle.id = QUuid::createUuid();
    rectangle.start = {20, 20};
    rectangle.end = {580, 380};
    rectangle.style.fillColor = 0xFFFFFFFF;
    rectangle.style.rotationDegrees = 17;
    document.annotations.push_back(rectangle);
    const QJsonObject before = serializeDocument(document);
    ImageComparisonDialog dialog(document, source);
    dialog.show();
    auto *progress = dialog.findChild<QProgressBar *>("comparisonProgress");
    auto *cancel = dialog.findChild<QPushButton *>("cancelComparisonLoad");
    auto *label = dialog.findChild<QLabel *>("comparisonInput0");
    auto *status = dialog.findChild<QLabel *>("comparisonStatus");
    QVERIFY(progress && cancel && label && status);
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible() && progress->value() >= 20 && progress->value() < 90, 5000);
    cancel->click();
    QTRY_VERIFY(status->text().startsWith("Load cancelled"));
    QCOMPARE(label->text(), QStringLiteral("No image selected"));
    // Establish a completed pair, then cancel a replacement of its first image.
    QTemporaryDir dir;
    const QString firstPath = dir.filePath("first.png"), secondPath = dir.filePath("second.png");
    QVERIFY(solid(6, 4, Qt::red).save(firstPath));
    QVERIFY(solid(4, 6, Qt::blue).save(secondPath));
    QVERIFY(dialog.loadFile(0, firstPath));
    QTRY_VERIFY(label->text().startsWith("first.png"));
    auto *slider = dialog.findChild<QSlider *>("comparisonDivider");
    auto *view = dialog.findChild<ComparisonView *>();
    QVERIFY(slider && view);
    QVERIFY(dialog.loadFile(1, secondPath));
    QTRY_VERIFY(slider->isEnabled());
    slider->setValue(25);
    const QString priorLabel = label->text();
    const QColor priorPixel = sample(*view, {.5, .5});
    QVERIFY(dialog.useCurrentImage());
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible() && progress->value() >= 20 && progress->value() < 90, 5000);
    cancel->click();
    QTRY_VERIFY(status->text().startsWith("Load cancelled"));
    QCOMPARE(label->text(), priorLabel);
    QCOMPARE(slider->value(), 25);
    QCOMPARE(sample(*view, {.5, .5}), priorPixel);
    QVERIFY(dialog.useCurrentImage());
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible() && progress->value() >= 20 && progress->value() < 90, 5000);
    QSignalSpy closed(&dialog, &QDialog::finished);
    dialog.reject();
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(label->text(), priorLabel);
    QCOMPARE(source, original);
    QCOMPARE(serializeDocument(document), before);
  }
};
QTEST_MAIN(ImageComparisonDialogTest)
#include "ImageComparisonDialogTest.moc"
