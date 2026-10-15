// EC-001/EC-017: native validation, real file/clipboard inputs and worker lifetime.
#include "NewImageDialog.h"
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSettings>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <thread>
#include <stdexcept>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
void dimensions(NewImageDialog &dialog, int width, int height) {
  dialog.findChild<QSpinBox *>("newImageWidth")->setValue(width);
  dialog.findChild<QSpinBox *>("newImageHeight")->setValue(height);
}
QImage fixture() {
  QImage image(3, 2, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace::SRgb);
  image.setPixel(0, 0, 0x80FF0000); image.setPixel(1, 0, 0xFF00FF00); image.setPixel(2, 0, 0xFF0000FF);
  image.setPixel(0, 1, 0xFF112233); image.setPixel(1, 1, 0x80445566); image.setPixel(2, 1, 0xFF778899);
  return image;
}
struct NewGate { std::atomic<int> calls{0}; std::atomic<bool> entered{false}, released{false}, finished{false}, workerThread{false}; };
NewImageHooks gatedNew(const std::shared_ptr<NewGate> &gate, bool fail = false) {
  return {[gate, fail] {
    if (++gate->calls != 1) return;
    gate->workerThread.store(QThread::currentThread() != qApp->thread()); gate->entered.store(true);
    while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    gate->finished.store(true); if (fail) throw std::runtime_error("private initial image path");
  }};
}
class NewEnableObserver final : public QObject {
public:
  std::function<void()> changed;
  bool eventFilter(QObject *, QEvent *event) override { if (event->type() == QEvent::EnabledChange && changed) changed(); return false; }
};
}
class NewImageDialogTest : public QObject {
  Q_OBJECT
  QTemporaryDir preferences;
private slots:
  void initTestCase() { QVERIFY(preferences.isValid()); QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, preferences.path()); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path()); }
  void validationPreventsInvalidCreationAndEscapeHasNoResult() {
    NewImageDialog dialog; dialog.show();
    auto *create = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    auto *status = dialog.findChild<QLabel *>("newImageStatus");
    QVERIFY(create && status);
    dimensions(dialog, 100000, 100000);
    QVERIFY(!create->isEnabled()); QVERIFY(status->text().contains("limits"));
    dialog.accept(); QVERIFY(dialog.isVisible()); QVERIFY(!dialog.takeImage());
    dimensions(dialog, 6, 4);
    dialog.findChild<QComboBox *>("newImageBackground")->setCurrentIndex(1);
    dialog.findChild<QLineEdit *>("newImageColour")->setText("invalid");
    QVERIFY(!create->isEnabled()); QVERIFY(status->text().contains("#AARRGGBB"));
    dialog.accept(); QVERIFY(dialog.isVisible()); QVERIFY(!dialog.takeImage());
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(!dialog.isVisible()); QCOMPARE(dialog.result(), int(QDialog::Rejected)); QVERIFY(!dialog.takeImage());
  }
  void keyboardCreationUsesChosenAlphaAndProfileWithAccessibleControls() {
    NewImageDialog dialog; dialog.show(); dimensions(dialog, 6, 4);
    auto *background = dialog.findChild<QComboBox *>("newImageBackground");
    auto *profile = dialog.findChild<QComboBox *>("newImageProfile");
    auto *colour = dialog.findChild<QLineEdit *>("newImageColour");
    auto *width = dialog.findChild<QSpinBox *>("newImageWidth");
    QVERIFY(background && profile && colour && width);
    auto *accessible = QAccessible::queryAccessibleInterface(width); QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("New image width in pixels"));
    auto *profileAccess = QAccessible::queryAccessibleInterface(profile); QVERIFY(profileAccess);
    // On Linux Qt exposes a combo's current value as its name and the field's
    // label through the native Label relation (including the Qt 6.4 baseline).
    QCOMPARE(profileAccess->text(QAccessible::Value), QStringLiteral("sRGB"));
    bool labelled = false;
    for (const auto &relation : profileAccess->relations(QAccessible::Label))
      if (relation.first->text(QAccessible::Name) == QStringLiteral("Colour profile")) labelled = true;
    QVERIFY(labelled);
    background->setFocus(); QTest::keyClick(background, Qt::Key_End); QCOMPARE(background->currentIndex(), 1);
    colour->setText("#80808080"); profile->setFocus(); QTest::keyClick(profile, Qt::Key_Down);
    QCOMPARE(profile->currentIndex(), 1);
    const auto qa = qEnvironmentVariable("XERAHS_NEW_IMAGE_QA"); if (!qa.isEmpty()) QVERIFY(dialog.grab().save(qa));
    auto *create = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    create->setFocus(); QTest::keyClick(create, Qt::Key_Space);
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    auto prepared = dialog.takeImage(); QVERIFY(prepared && prepared->document);
    QCOMPARE(prepared->source.size(), QSize(6, 4));
    QCOMPARE(prepared->source.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));
    QCOMPARE(qAlpha(prepared->source.pixel(0, 0)), 128);
    QVERIFY(std::abs(qRed(prepared->source.pixel(0, 0)) - 55) <= 1);
    QVERIFY(std::abs(qRed(prepared->displaySource.pixel(0, 0)) - 128) <= 1);
    QVERIFY(prepared->document->annotations.empty() && prepared->document->imagePath.isEmpty());
    QVERIFY(!dialog.takeImage());
  }
  void clipboardIsAnExplicitImmutableInputWithCropPaddingAndSourceOver() {
    const QImage initial = fixture(); QApplication::clipboard()->setImage(initial);
    NewImageDialog dialog; dialog.show();
    dialog.findChild<QPushButton *>("pasteNewImageInitial")->click();
    QCOMPARE(dialog.findChild<QSpinBox *>("newImageWidth")->value(), 3);
    QCOMPARE(dialog.findChild<QSpinBox *>("newImageHeight")->value(), 2);
    const QString label = dialog.findChild<QLabel *>("newImageInitial")->text();
    QApplication::clipboard()->setText("not an image");
    dialog.findChild<QPushButton *>("pasteNewImageInitial")->click();
    QCOMPARE(dialog.findChild<QLabel *>("newImageInitial")->text(), label);
    dimensions(dialog, 2, 3);
    dialog.findChild<QComboBox *>("newImageBackground")->setCurrentIndex(1);
    dialog.findChild<QLineEdit *>("newImageColour")->setText("#FF0000FF");
    dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    const auto prepared = dialog.takeImage(); QVERIFY(prepared);
    QCOMPARE(prepared->source.size(), QSize(2, 3));
    QCOMPARE(prepared->source.pixel(0, 0), QRgb(0xFF80007F));
    QCOMPARE(prepared->source.pixel(1, 0), QRgb(0xFF00FF00));
    QCOMPARE(prepared->source.pixel(0, 1), QRgb(0xFF112233));
    QCOMPARE(prepared->source.pixel(0, 2), QRgb(0xFF0000FF));
    QCOMPARE(prepared->source.pixel(1, 2), QRgb(0xFF0000FF));
    QCOMPARE(initial, fixture());
  }
  void failedFileAndCancelledPickerKeepThePriorInitialPixelsAndFiles() {
    QTemporaryDir dir; const QString path = dir.filePath("initial.png"), badPath = dir.filePath("bad.png");
    const QImage initial = fixture(); QVERIFY(initial.save(path));
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write("untouched sidecar"); sidecar.close();
    QFile bad(badPath); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("bad"); bad.close();
    NewImageDialog dialog; dialog.show(); QVERIFY(dialog.loadInitialFile(path));
    auto *label = dialog.findChild<QLabel *>("newImageInitial");
    auto *status = dialog.findChild<QLabel *>("newImageStatus");
    QTRY_VERIFY(label->text().startsWith("initial.png"));
    const QString priorLabel = label->text(); dimensions(dialog, 2, 3);
    QVERIFY(dialog.loadInitialFile(badPath)); QTRY_VERIFY(status->text().contains("not a supported image"));
    QCOMPARE(label->text(), priorLabel);
    QCOMPARE(dialog.findChild<QSpinBox *>("newImageWidth")->value(), 2);
    QCOMPARE(dialog.findChild<QSpinBox *>("newImageHeight")->value(), 3);
    bool cancelled = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) { cancelled = true; picker->reject(); timer.stop(); }
    });
    timer.start(10); dialog.findChild<QPushButton *>("chooseNewImageInitial")->click(); timer.stop(); QVERIFY(cancelled);
    QCOMPARE(label->text(), priorLabel);
    dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    const auto prepared = dialog.takeImage(); QVERIFY(prepared);
    QCOMPARE(prepared->source.pixel(0, 0), initial.pixel(0, 0));
    QCOMPARE(prepared->source.pixel(1, 1), initial.pixel(1, 1));
    QCOMPARE(prepared->source.pixel(0, 2), QRgb(0));
    QCOMPARE(QImage(path), initial);
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), QByteArray("untouched sidecar"));
  }
  void removingInitialImageKeepsChosenDimensionsAndCreatesABlankSource() {
    NewImageDialog dialog; dialog.show(); QVERIFY(dialog.setInitialImage(fixture(), "Fixture"));
    dimensions(dialog, 2, 3); dialog.findChild<QPushButton *>("clearNewImageInitial")->click();
    QVERIFY(!dialog.findChild<QPushButton *>("clearNewImageInitial")->isEnabled());
    dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    const auto prepared = dialog.takeImage(); QVERIFY(prepared); QCOMPARE(prepared->source.size(), QSize(2, 3));
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x) QCOMPARE(prepared->source.pixel(x, y), QRgb(0));
  }
  void cancellationAndClosingRetireTheWorkerWithoutPublishing() {
    NewImageDialog dialog; dialog.show(); QVERIFY(dialog.setInitialImage(fixture(), "Prior initial"));
    const QString prior = dialog.findChild<QLabel *>("newImageInitial")->text();
    dimensions(dialog, 4000, 4000); dialog.accept();
    auto *progress = dialog.findChild<QProgressBar *>("newImageProgress");
    auto *status = dialog.findChild<QLabel *>("newImageStatus");
    auto *cancel = dialog.findChild<QPushButton *>("cancelNewImageWork");
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible() && progress->value() >= 20 && progress->value() < 100, 5000);
    cancel->click(); QTRY_VERIFY(status->text().startsWith("Work cancelled"));
    QVERIFY(dialog.isVisible()); QVERIFY(!dialog.takeImage());
    QCOMPARE(dialog.findChild<QLabel *>("newImageInitial")->text(), prior);
    QSignalSpy finished(&dialog, &QDialog::finished);
    dialog.accept();
    QTRY_VERIFY_WITH_TIMEOUT(progress->isVisible() && progress->value() >= 20 && progress->value() < 100, 5000);
    dialog.reject(); QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(dialog.result(), int(QDialog::Rejected)); QVERIFY(!dialog.takeImage());
  }
  void cancelledWorkCannotReplaceANewerInputOrCreation_data() {
    QTest::addColumn<bool>("load"); QTest::addColumn<bool>("fail");
    for (bool load : {false, true}) for (bool fail : {false, true}) QTest::newRow(qPrintable(QString("%1-%2").arg(load ? "load" : "create", fail ? "late-exception" : "late-success"))) << load << fail;
  }
  void cancelledWorkCannotReplaceANewerInputOrCreation() {
    QFETCH(bool, load); QFETCH(bool, fail); QTemporaryDir dir; const auto path = dir.filePath("abandoned.png"); QVERIFY(fixture().save(path));
    const auto gate = std::make_shared<NewGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); });
    NewImageDialog dialog(nullptr, gatedNew(gate, fail)); dialog.show(); QVERIFY(dialog.setInitialImage(fixture(), "Prior")); dimensions(dialog, 5, 4);
    if (load) QVERIFY(dialog.loadInitialFile(path)); else dialog.accept(); QTRY_VERIFY(gate->entered.load()); QVERIFY(gate->workerThread.load());
    QVERIFY(!dialog.loadInitialFile(path)); QVERIFY(!dialog.setInitialImage(fixture(), "Forbidden"));
    QElapsedTimer elapsed; elapsed.start(); dialog.findChild<QPushButton *>("cancelNewImageWork")->click(); QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->finished.load()); QVERIFY(dialog.isVisible()); QVERIFY(!dialog.takeImage());
    QCOMPARE(dialog.findChild<QSpinBox *>("newImageWidth")->value(), 5); QCOMPARE(dialog.findChild<QSpinBox *>("newImageHeight")->value(), 4); QVERIFY(dialog.findChild<QLabel *>("newImageInitial")->text().startsWith("Prior"));
    QImage replacement(2, 3, QImage::Format_ARGB32); replacement.fill(Qt::blue); QVERIFY(dialog.setInitialImage(replacement, "Replacement")); dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(!gate->finished.load()); auto prepared = dialog.takeImage(); QVERIFY(prepared && prepared->document); QCOMPARE(prepared->source.size(), replacement.size()); QCOMPARE(prepared->source.colorSpace(), QColorSpace(QColorSpace::SRgb)); for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x) QCOMPARE(prepared->source.pixel(x, y), qRgb(0, 0, 255)); QVERIFY(!dialog.takeImage());
    gate->released.store(true); QVERIFY(QThreadPool::globalInstance()->waitForDone(5000)); QTest::qWait(30); QVERIFY(!dialog.takeImage()); QCOMPARE(dialog.result(), int(QDialog::Accepted)); QCOMPARE(QImage(path), fixture());
  }
  void blockedWorkNeverJoinsOnTeardown_data() {
    QTest::addColumn<bool>("load"); QTest::addColumn<QString>("mode");
    for (bool load : {false, true}) for (const auto *mode : {"reject", "escape", "child-destroy", "owner-destroy"}) QTest::newRow(qPrintable(QString("%1-%2").arg(load ? "load" : "create", mode))) << load << QString(mode);
  }
  void blockedWorkNeverJoinsOnTeardown() {
    QFETCH(bool, load); QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("input.png"); QVERIFY(fixture().save(path));
    const auto gate = std::make_shared<NewGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); }); std::unique_ptr<QWidget> parent(new QWidget);
    QPointer<NewImageDialog> dialog(new NewImageDialog(parent.get(), gatedNew(gate))); dialog->show(); dimensions(*dialog, 4, 3);
    if (load) QVERIFY(dialog->loadInitialFile(path)); else dialog->accept(); QTRY_VERIFY(gate->entered.load());
    const auto progress = dialog->findChild<QProgressBar *>("newImageProgress"); auto *accessible = QAccessible::queryAccessibleInterface(progress); QVERIFY(accessible); QCOMPARE(accessible->role(), QAccessible::ProgressBar); QCOMPARE(accessible->text(QAccessible::Name), QString("New image preparation progress"));
    QSignalSpy finished(dialog, &QDialog::finished); QElapsedTimer elapsed; elapsed.start();
    if (mode == "child-destroy") delete dialog.data(); else if (mode == "owner-destroy") parent.reset(); else if (mode == "escape") QTest::keyClick(dialog, Qt::Key_Escape); else dialog->reject();
    QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->finished.load()); if (dialog) { QVERIFY(!dialog->isVisible()); QCOMPARE(finished.size(), 1); QVERIFY(!dialog->takeImage()); }
    gate->released.store(true); QVERIFY(QThreadPool::globalInstance()->waitForDone(5000)); if (dialog) delete dialog.data(); QCOMPARE(QImage(path), fixture());
  }
  void frozenOptionsAndBorrowedInitialPixelsSurviveCallerChanges() {
    QByteArray buffer(24, '\0'); QImage initial(reinterpret_cast<uchar *>(buffer.data()), 3, 2, 12, QImage::Format_ARGB32); initial.fill(qRgba(128, 0, 0, 128)); initial.setColorSpace(QColorSpace::SRgbLinear);
    const auto gate = std::make_shared<NewGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); }); NewImageDialog dialog(nullptr, gatedNew(gate)); dialog.show(); QVERIFY(dialog.setInitialImage(initial, "Owned"));
    dimensions(dialog, 4, 3); dialog.findChild<QComboBox *>("newImageProfile")->setCurrentIndex(1); dialog.accept(); QTRY_VERIFY(gate->entered.load());
    initial = {}; buffer.fill(char(0)); buffer.clear(); dimensions(dialog, 9, 8); dialog.findChild<QComboBox *>("newImageProfile")->setCurrentIndex(0);
    gate->released.store(true); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted)); auto prepared = dialog.takeImage(); QVERIFY(prepared && prepared->document); QCOMPARE(prepared->source.size(), QSize(4, 3)); QCOMPARE(prepared->source.colorSpace(), QColorSpace(QColorSpace::SRgbLinear)); QCOMPARE(prepared->source.pixel(0, 0), QRgb(0x80800000)); QCOMPARE(prepared->source.pixel(3, 2), QRgb(0));
  }
  void failedPreparationIsRedactedAndRetryable() {
    const auto gate = std::make_shared<NewGate>(); const auto release = qScopeGuard([gate] { gate->released.store(true); }); NewImageDialog dialog(nullptr, gatedNew(gate, true)); dialog.show(); dimensions(dialog, 4, 3); dialog.accept(); QTRY_VERIFY(gate->entered.load()); gate->released.store(true);
    auto *status = dialog.findChild<QLabel *>("newImageStatus"); QTRY_VERIFY(status->text().contains("preparation failed")); QVERIFY(!status->text().contains("private")); QVERIFY(!dialog.takeImage()); dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted)); QVERIFY(dialog.takeImage());
  }
  void ownedPickersRecoverFromNestedDestruction_data() {
    QTest::addColumn<bool>("colour"); QTest::addColumn<QString>("mode");
    for (bool colour : {false, true}) for (const auto *mode : {"accept", "cancel", "child-destroy", "owner-destroy", "cleanup-owner-destroy", "close-wizard"}) QTest::newRow(qPrintable(QString("%1-%2").arg(colour ? "colour" : "file", mode))) << colour << QString(mode);
  }
  void ownedPickersRecoverFromNestedDestruction() {
    QFETCH(bool, colour); QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("selected.png"); QVERIFY(fixture().save(path));
    QPointer<QWidget> parent(new QWidget); const auto cleanup = qScopeGuard([&] { delete parent.data(); }); QPointer<NewImageDialog> dialog(new NewImageDialog(parent)); dialog->show(); dimensions(*dialog, 4, 3); dialog->findChild<QComboBox *>("newImageBackground")->setCurrentIndex(1);
    bool acted = false, timedOut = false; QElapsedTimer deadline; deadline.start(); QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!picker || picker == dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; picker->reject(); return; } if (acted) return; acted = true; QVERIFY(!dialog->loadInitialFile(path)); QVERIFY(!dialog->setInitialImage(fixture(), "reentry"));
      if (mode == "child-destroy") delete picker; else if (mode == "owner-destroy") delete parent.data(); else if (mode == "close-wizard") dialog->reject();
      else if (mode == "accept") {
        if (colour) qobject_cast<QColorDialog *>(picker)->setCurrentColor(QColor(20, 40, 60, 80));
        else {
          // A visible focused Qt 6.4 filename edit is not changed by selectFile.
          auto *file = qobject_cast<QFileDialog *>(picker); QVERIFY(file);
          file->setDirectory(dir.path());
          auto *name = file->findChild<QLineEdit *>("fileNameEdit"); QVERIFY(name); name->setFocus(); name->setText(path);
          QCOMPARE(file->selectedFiles(), QStringList{path});
        }
        picker->accept();
      }
      else { if (mode == "cleanup-owner-destroy") connect(picker, &QObject::destroyed, [&] { delete parent.data(); }); picker->reject(); }
    });
    timer.start(5); dialog->findChild<QPushButton *>(colour ? "chooseNewImageColour" : "chooseNewImageInitial")->click(); timer.stop(); QVERIFY(acted && !timedOut);
    if (mode == "owner-destroy" || mode == "cleanup-owner-destroy") QVERIFY(!dialog);
    else if (mode == "close-wizard") QVERIFY(dialog && !dialog->isVisible());
    else { QVERIFY(dialog); if (mode == "accept") { if (colour) QCOMPARE(dialog->findChild<QLineEdit *>("newImageColour")->text(), QString("#5014283C")); else QTRY_VERIFY(dialog->findChild<QLabel *>("newImageInitial")->text().startsWith("selected.png")); } else { QCOMPARE(dialog->findChild<QSpinBox *>("newImageWidth")->value(), 4); QVERIFY(dialog->setInitialImage(fixture(), "Retry")); } }
    QCOMPARE(QImage(path), fixture());
  }
  void initialPublicationPrecedesCreateAvailabilityAndRefusesReentry() {
    QTemporaryDir dir; const auto path = dir.filePath("input.png"); QVERIFY(fixture().save(path)); NewImageDialog dialog; dialog.show();
    auto *create = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok); bool observed = false; NewEnableObserver observer;
    observer.changed = [&] { if (!create->isEnabled()) return; observed = true; QCOMPARE(dialog.findChild<QSpinBox *>("newImageWidth")->value(), 3); QCOMPARE(dialog.findChild<QSpinBox *>("newImageHeight")->value(), 2); QVERIFY(dialog.findChild<QLabel *>("newImageInitial")->text().startsWith("input.png")); QVERIFY(dialog.findChild<QLabel *>("newImageStatus")->text().startsWith("Create prepares")); QVERIFY(!dialog.loadInitialFile(path)); dialog.accept(); QVERIFY(!dialog.takeImage()); };
    create->installEventFilter(&observer); QVERIFY(dialog.loadInitialFile(path)); QTRY_VERIFY(observed); create->removeEventFilter(&observer); dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted)); QVERIFY(dialog.takeImage());
  }

};
QTEST_MAIN(NewImageDialogTest)
#include "NewImageDialogTest.moc"
