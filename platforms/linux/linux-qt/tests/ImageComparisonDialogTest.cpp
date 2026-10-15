// EC-016/EC-017: actual native widgets, raster validation and worker lifetime.
#include "ImageComparisonDialog.h"
#include <QAccessible>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <thread>
#include <stdexcept>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
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
class EnabledObserver final : public QObject {
public:
  std::function<void()> changed;
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::EnabledChange && changed) changed();
    return false;
  }
};
struct ComparisonGate {
  std::atomic<int> calls{0};
  std::atomic<bool> entered{false}, released{false}, finished{false}, workerThread{false};
};
ComparisonHooks gatedComparison(const std::shared_ptr<ComparisonGate> &gate, int hold = 1, bool fail = false) {
  return {[gate, hold, fail] {
    if (++gate->calls != hold) return;
    gate->workerThread.store(QThread::currentThread() != qApp->thread());
    gate->entered.store(true);
    while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    gate->finished.store(true);
    if (fail) throw std::runtime_error("private comparison path and decoder detail");
  }};
}
} // namespace
class ImageComparisonDialogTest : public QObject {
  Q_OBJECT
  QTemporaryDir preferences;
private slots:
  void initTestCase() {
    QVERIFY(preferences.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, preferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    // Exercise the actual owned Qt picker. Platform-theme helpers require
    // separate physical desktop qualification and are not offscreen widgets.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  }
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
    const auto qa = qEnvironmentVariable("XERAHS_COMPARISON_QA"); if (!qa.isEmpty()) QVERIFY(dialog.grab().save(qa));
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

  void cancellationAndClosingRetireTheWorkerWithoutPublishingAnInput() {
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
  void cancelledJobCannotReplaceANewerPair_data() {
    QTest::addColumn<int>("input"); QTest::addColumn<bool>("fail");
    QTest::newRow("first-late-success") << 0 << false;
    QTest::newRow("second-late-success") << 1 << false;
    QTest::newRow("first-late-exception") << 0 << true;
    QTest::newRow("second-late-exception") << 1 << true;
  }
  void cancelledJobCannotReplaceANewerPair() {
    QFETCH(int, input); QFETCH(bool, fail);
    QTemporaryDir dir;
    const auto secondPath = dir.filePath("second.png"), oldPath = dir.filePath("abandoned.png"), newPath = dir.filePath("replacement.png");
    QVERIFY(solid(2, 4, Qt::blue).save(secondPath)); QVERIFY(solid(6, 2, Qt::yellow).save(oldPath)); QVERIFY(solid(8, 6, Qt::green).save(newPath));
    const auto gate = std::make_shared<ComparisonGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); });
    ImageComparisonDialog dialog({}, solid(4, 2, Qt::red), nullptr, gatedComparison(gate, 3, fail)); dialog.show();
    auto *first = dialog.findChild<QLabel *>("comparisonInput0"); auto *second = dialog.findChild<QLabel *>("comparisonInput1");
    auto *slider = dialog.findChild<QSlider *>("comparisonDivider"); auto *alignment = dialog.findChild<QComboBox *>("comparisonAlignment"); auto *view = dialog.findChild<ComparisonView *>();
    auto *status = dialog.findChild<QLabel *>("comparisonStatus"); auto *cancel = dialog.findChild<QPushButton *>("cancelComparisonLoad");
    QTRY_VERIFY(first->text().startsWith("Current rendered image")); QVERIFY(dialog.loadFile(1, secondPath)); QTRY_VERIFY(slider->isEnabled());
    alignment->setCurrentIndex(1); slider->setValue(25);
    const auto priorFirst = first->text(), priorSecond = second->text(); const auto before = sample(*view, {.5, 1.5});
    QVERIFY(dialog.loadFile(input, oldPath)); QTRY_VERIFY(gate->entered.load()); QVERIFY(gate->workerThread.load());
    QVERIFY(!dialog.loadFile(1 - input, newPath));
    QElapsedTimer elapsed; elapsed.start(); cancel->click(); QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->finished.load());
    QCOMPARE(first->text(), priorFirst); QCOMPARE(second->text(), priorSecond); QCOMPARE(slider->value(), 25); QCOMPARE(alignment->currentIndex(), 1); QCOMPARE(sample(*view, {.5, 1.5}), before);
    QVERIFY(dialog.loadFile(input, newPath)); auto *replaced = input == 0 ? first : second;
    QTRY_VERIFY(replaced->text().startsWith("replacement.png")); QVERIFY(!gate->finished.load());
    const auto finalFirst = first->text(), finalSecond = second->text(), finalStatus = status->text(); const auto pixels = view->grab().toImage();
    gate->released.store(true); QVERIFY(QThreadPool::globalInstance()->waitForDone(5000)); QTest::qWait(40);
    QCOMPARE(first->text(), finalFirst); QCOMPARE(second->text(), finalSecond); QCOMPARE(status->text(), finalStatus); QCOMPARE(view->grab().toImage(), pixels); QCOMPARE(slider->value(), 25); QCOMPARE(alignment->currentIndex(), 1);
    dialog.reject();
  }

  void blockedWorkerTeardownIsImmediate_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"reject", "accept", "escape", "dialog-destroy", "owner-destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void blockedWorkerTeardownIsImmediate() {
    QFETCH(QString, mode);
    const auto gate = std::make_shared<ComparisonGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); });
    std::unique_ptr<QWidget> parent(new QWidget); QPointer<ImageComparisonDialog> dialog(new ImageComparisonDialog({}, solid(4, 2, Qt::red), parent.get(), gatedComparison(gate))); dialog->show();
    QTRY_VERIFY(gate->entered.load()); QVERIFY(gate->workerThread.load());
    auto *progress = dialog->findChild<QProgressBar *>("comparisonProgress"); QVERIFY(progress->isVisible());
    auto *accessible = QAccessible::queryAccessibleInterface(progress); QVERIFY(accessible); QCOMPARE(accessible->role(), QAccessible::ProgressBar); QCOMPARE(accessible->text(QAccessible::Name), QString("Comparison image loading progress"));
    QSignalSpy finished(dialog, &QDialog::finished); QElapsedTimer elapsed; elapsed.start();
    if (mode == "dialog-destroy") delete dialog.data();
    else if (mode == "owner-destroy") parent.reset();
    else if (mode == "accept") dialog->accept();
    else if (mode == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
    else dialog->reject();
    QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->finished.load());
    if (dialog) { QCOMPARE(finished.count(), 1); QVERIFY(!dialog->isVisible()); QVERIFY(!dialog->loadFile(1, "ignored.png")); QCOMPARE(dialog->findChild<QLabel *>("comparisonInput0")->text(), QString("No image selected")); }
    gate->released.store(true); QVERIFY(QThreadPool::globalInstance()->waitForDone(5000)); QTest::qWait(40);
    if (dialog) { QCOMPARE(finished.count(), 1); delete dialog.data(); }
  }

  void comparisonOwnsItsRenderedSnapshotAndRedactsFailure_data() {
    QTest::addColumn<bool>("fail"); QTest::newRow("owned-render") << false; QTest::newRow("redacted-exception") << true;
  }
  void comparisonOwnsItsRenderedSnapshotAndRedactsFailure() {
    QFETCH(bool, fail);
    QImage source = solid(4, 4, QColor(128, 0, 0)); source.setColorSpace(QColorSpace::SRgbLinear);
    AnnotationDocument document; RectangleAnnotation rectangle; rectangle.id = QUuid::createUuid(); rectangle.start = {2, 0}; rectangle.end = {4, 2}; rectangle.style.strokeColor = 0; rectangle.style.fillColor = 0xFF00FF00; document.annotations.push_back(rectangle);
    const auto gate = std::make_shared<ComparisonGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); });
    ImageComparisonDialog dialog(document, source, nullptr, gatedComparison(gate, 1, fail)); dialog.show();
    QTRY_VERIFY(gate->entered.load()); source.fill(Qt::black); document.annotations.clear();
    gate->released.store(true); QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    auto *first = dialog.findChild<QLabel *>("comparisonInput0"); auto *status = dialog.findChild<QLabel *>("comparisonStatus");
    if (fail) {
      QTRY_VERIFY(status->text().contains("could not be prepared")); QVERIFY(!status->text().contains("private")); QCOMPARE(first->text(), QString("No image selected"));
      QVERIFY(dialog.useCurrentImage()); QTRY_VERIFY(first->text().startsWith("Current rendered image"));
    } else QTRY_VERIFY(first->text().startsWith("Current rendered image"));
    QTemporaryDir dir; const auto path = dir.filePath("second.png"); QVERIFY(solid(4, 4, Qt::blue).save(path)); QVERIFY(dialog.loadFile(1, path));
    auto *slider = dialog.findChild<QSlider *>("comparisonDivider"); QTRY_VERIFY(slider->isEnabled()); slider->setValue(100); auto *view = dialog.findChild<ComparisonView *>();
    const auto red = sample(*view, {.5, 2.5}); QVERIFY(std::abs(red.red() - 188) <= 1); QCOMPARE(red.green(), 0); QCOMPARE(sample(*view, {2.5, .5}), QColor(Qt::green));
    QCOMPARE(source.pixelColor(0, 0), QColor(Qt::black)); QVERIFY(document.annotations.empty()); dialog.reject();
  }

  void oversizedUnionKeepsTheValidatedPairAndControls() {
    QTemporaryDir dir; const auto valid = dir.filePath("small.png"), invalidUnion = dir.filePath("tall.png");
    QVERIFY(solid(4, 2, Qt::blue).save(valid)); QVERIFY(solid(1, 100000, Qt::green).save(invalidUnion));
    ImageComparisonDialog dialog({}, solid(100000, 1, Qt::red)); dialog.show();
    auto *first = dialog.findChild<QLabel *>("comparisonInput0"); auto *second = dialog.findChild<QLabel *>("comparisonInput1"); auto *slider = dialog.findChild<QSlider *>("comparisonDivider"); auto *view = dialog.findChild<ComparisonView *>(); auto *alignment = dialog.findChild<QComboBox *>("comparisonAlignment");
    QTRY_VERIFY(first->text().startsWith("Current rendered image")); QVERIFY(dialog.loadFile(1, valid)); QTRY_VERIFY(slider->isEnabled()); alignment->setCurrentIndex(1); slider->setValue(37);
    const auto priorFirst = first->text(), priorSecond = second->text(); const auto priorGeometry = *view->comparisonLayout();
    QVERIFY(!dialog.loadFile(2, valid)); QVERIFY(!dialog.loadFile(0, {})); QVERIFY(dialog.loadFile(1, invalidUnion));
    QTRY_VERIFY(dialog.findChild<QLabel *>("comparisonStatus")->text().contains("size or memory limits"));
    QCOMPARE(first->text(), priorFirst); QCOMPARE(second->text(), priorSecond); QCOMPARE(slider->value(), 37); QCOMPARE(alignment->currentIndex(), 1); QCOMPARE(view->comparisonLayout()->canvas.width, priorGeometry.canvas.width); QCOMPARE(view->comparisonLayout()->canvas.height, priorGeometry.canvas.height); QCOMPARE(view->comparisonLayout()->firstOffset, priorGeometry.firstOffset); QCOMPARE(view->comparisonLayout()->secondOffset, priorGeometry.secondOffset);
    QVERIFY(dialog.findChild<QPushButton *>("chooseComparisonInput1")->isEnabled()); dialog.reject();
  }

  void completionNotificationsKeepPublicationAtomic_data() {
    QTest::addColumn<bool>("cancel"); QTest::newRow("cancel-on-ready-progress") << true; QTest::newRow("reload-on-enable-refused") << false;
  }
  void completionNotificationsKeepPublicationAtomic() {
    QFETCH(bool, cancel); QTemporaryDir dir; const auto path = dir.filePath("second.png"); QVERIFY(solid(2, 4, Qt::blue).save(path));
    ImageComparisonDialog dialog({}, solid(4, 2, Qt::red)); dialog.show();
    auto *first = dialog.findChild<QLabel *>("comparisonInput0"); QTRY_VERIFY(first->text().startsWith("Current rendered image"));
    auto *second = dialog.findChild<QLabel *>("comparisonInput1"); auto *progress = dialog.findChild<QProgressBar *>("comparisonProgress"); auto *button = dialog.findChild<QPushButton *>("chooseComparisonInput0"); auto *slider = dialog.findChild<QSlider *>("comparisonDivider");
    bool observed = false, dividerObserved = false; EnabledObserver observer, dividerObserver;
    observer.changed = [&] {
      if (!button->isEnabled()) return; observed = true;
      QVERIFY(!dialog.loadFile(0, path)); QVERIFY(!dialog.useCurrentImage());
      if (!cancel) { QVERIFY(slider->isEnabled()); QVERIFY(second->text().startsWith("second.png")); QVERIFY(dialog.findChild<ComparisonView *>()->comparisonLayout()); }
    };
    button->installEventFilter(&observer);
    dividerObserver.changed = [&] {
      if (!slider->isEnabled()) return;
      dividerObserved = true;
      QVERIFY(!cancel); QVERIFY(!dialog.loadFile(0, path));
      QVERIFY(dialog.findChild<ComparisonView *>()->comparisonLayout());
      QCOMPARE(dialog.findChild<QLabel *>("comparisonGeometry")->text(), QString("Comparison canvas: 4 × 4 pixels. No image resizing."));
      QCOMPARE(dialog.findChild<QLabel *>("comparisonStatus")->text(), QString("Move the divider or use arrow keys, Home and End."));
    };
    slider->installEventFilter(&dividerObserver);
    connect(progress, &QProgressBar::valueChanged, &dialog, [&](int value) { if (cancel && value == 100) dialog.findChild<QPushButton *>("cancelComparisonLoad")->click(); });
    QVERIFY(dialog.loadFile(1, path)); QTRY_VERIFY(observed);
    if (cancel) { QCOMPARE(second->text(), QString("No image selected")); QVERIFY(!slider->isEnabled()); QVERIFY(dialog.findChild<QLabel *>("comparisonStatus")->text().startsWith("Load cancelled")); }
    else { QVERIFY(slider->isEnabled()); QVERIFY(dividerObserved); }
    button->removeEventFilter(&observer); slider->removeEventFilter(&dividerObserver); disconnect(progress, nullptr, &dialog, nullptr);
    if (cancel) { QVERIFY(dialog.loadFile(1, path)); QTRY_VERIFY(slider->isEnabled()); }
    dialog.reject();
  }

  void ownedFilePickerRecoversFromCancellationAndDestruction_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"choose-first", "choose-second", "cancel", "picker-destroy", "dialog-destroy", "owner-destroy", "cleanup-owner-destroy", "close-comparison"}) QTest::newRow(mode) << QString(mode);
  }
  void ownedFilePickerRecoversFromCancellationAndDestruction() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("selected.png"); const auto image = solid(8, 6, Qt::green); QVERIFY(image.save(path));
    QPointer<QWidget> parent(new QWidget); const auto cleanup = qScopeGuard([&] { delete parent.data(); }); QPointer<ImageComparisonDialog> dialog(new ImageComparisonDialog({}, solid(4, 2, Qt::red), parent)); dialog->show();
    QTRY_VERIFY(dialog->findChild<QLabel *>("comparisonInput0")->text().startsWith("Current rendered image"));
    bool acted = false, timedOut = false; QElapsedTimer elapsed; elapsed.start(); QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); if (!picker) return;
      if (elapsed.elapsed() > 5000) { timedOut = true; picker->reject(); return; }
      if (acted) return; acted = true;
      QCOMPARE(picker->objectName(), QString("comparisonFilePicker")); QVERIFY(!dialog->loadFile(0, path));
      if (mode == "picker-destroy") delete picker;
      else if (mode == "dialog-destroy") delete dialog.data();
      else if (mode == "owner-destroy") delete parent.data();
      else if (mode == "close-comparison") dialog->reject();
      else if (mode.startsWith("choose")) { picker->selectFile(path); static_cast<QDialog *>(picker)->accept(); }
      else { if (mode == "cleanup-owner-destroy") connect(picker, &QObject::destroyed, [&] { delete parent.data(); }); picker->reject(); }
    });
    timer.start(5); dialog->findChild<QPushButton *>(mode == "choose-second" ? "chooseComparisonInput1" : "chooseComparisonInput0")->click(); timer.stop();
    QVERIFY(acted && !timedOut);
    if (mode == "dialog-destroy" || mode == "owner-destroy" || mode == "cleanup-owner-destroy") QVERIFY(!dialog);
    else if (mode == "close-comparison") QVERIFY(dialog && !dialog->isVisible());
    else {
      QVERIFY(dialog); if (mode.startsWith("choose")) QTRY_VERIFY(dialog->findChild<QLabel *>(mode == "choose-second" ? "comparisonInput1" : "comparisonInput0")->text().startsWith("selected.png"));
      else { QCOMPARE(dialog->findChild<QLabel *>("comparisonInput1")->text(), QString("No image selected")); QVERIFY(dialog->loadFile(1, path)); QTRY_VERIFY(dialog->findChild<QSlider *>("comparisonDivider")->isEnabled()); }
    }
    QCOMPARE(QImage(path), image); QVERIFY(!QFile::exists(path + ".xann"));
  }

};
QTEST_MAIN(ImageComparisonDialogTest)
#include "ImageComparisonDialogTest.moc"
