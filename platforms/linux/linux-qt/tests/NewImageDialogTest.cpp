// EC-001/EC-017: native validation, real file/clipboard inputs and worker lifetime.
#include "NewImageDialog.h"
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
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
}
class NewImageDialogTest : public QObject {
  Q_OBJECT
private slots:
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
  void cancellationAndClosingWaitForTheSameLiveWorkerWithoutPublishing() {
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
};
QTEST_MAIN(NewImageDialogTest)
#include "NewImageDialogTest.moc"
