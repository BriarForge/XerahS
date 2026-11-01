#include "ImageRecognitionDialog.h"
#include "QrRender.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QPainter>
#include <QTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <thread>
using namespace xerahs::app;
namespace {
QImage code() { return renderQr(xerahs::tools::encodeQr(QStringLiteral("fixture local QR"), xerahs::tools::QrEcc::M).matrix, 300).image; }
QByteArray bytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}
class ImageRecognitionDialogTest final : public QObject {
  Q_OBJECT
private slots:
  void fileAndExplicitCopyLeaveSourceAlone() {
    QTemporaryDir directory; const auto path = directory.filePath("code.png"); QVERIFY(code().save(path)); const auto before = bytes(path);
    QApplication::clipboard()->setText("clipboard sentinel");
    ImageRecognitionPorts ports; ports.chooseFile = [path](QWidget *) { return path; };
#ifndef XERAHS_ZXING
    ports.recognize = [](auto, const QImage &, const auto &) { return ImageRecognitionResult{{"fixture local QR"}}; };
#endif
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog.show();
    QSignalSpy done(&dialog, &ImageRecognitionDialog::recognitionFinished); dialog.findChild<QPushButton *>("recognitionFile")->click();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(dialog.values(), QStringList{"fixture local QR"}); QCOMPARE(bytes(path), before);
    QCOMPARE(QApplication::clipboard()->text(), QString("clipboard sentinel"));
    dialog.findChild<QPushButton *>("copyRecognitionResults")->click(); QCOMPARE(QApplication::clipboard()->text(), QString("fixture local QR"));
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{"code.png"});
  }
  void screenRegionAndCancellationDispatchOnlySelectedPixels() {
    std::function<void(RecognitionImageChoice)> finish; int scans = 0, selections = 0, cancellations = 0;
    ImageRecognitionPorts ports;
    ports.capture = [&](auto complete) { finish = std::move(complete); };
    ports.cancelCapture = [&] { ++cancellations; };
    ports.selectRegion = [&](const QImage &image, QWidget *) { ++selections; return RecognitionImageChoice{image.copy(0, 0, 12, 9)}; };
    ports.recognize = [&](auto, const QImage &image, const auto &) { ++scans; return ImageRecognitionResult{{QString("%1x%2").arg(image.width()).arg(image.height())}}; };
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog.show();
    QSignalSpy done(&dialog, &ImageRecognitionDialog::recognitionFinished);
    dialog.scanScreen(); QTRY_VERIFY(bool(finish)); finish({code()}); QTRY_COMPARE(done.count(), 1); QCOMPARE(selections, 0); QCOMPARE(dialog.values(), QStringList{QString("%1x%2").arg(code().width()).arg(code().height())});
    finish = {}; dialog.scanRegion(); QTRY_VERIFY(bool(finish)); finish({code()}); QTRY_COMPARE(done.count(), 2); QCOMPARE(selections, 1); QCOMPARE(dialog.values(), QStringList{"12x9"});
    finish = {}; dialog.scanRegion(); QTRY_VERIFY(bool(finish)); const auto late = finish; dialog.cancel(); QCOMPARE(cancellations, 1); late({code()});
    QTest::qWait(20); QCOMPARE(scans, 2); QCOMPARE(selections, 1); QVERIFY(!dialog.busy());
  }
  void regionSelectionCancelNeverScans() {
    ImageRecognitionPorts ports; int scans = 0;
    ports.capture = [](auto complete) { complete({code()}); };
    ports.selectRegion = [](const QImage &, QWidget *) { return RecognitionImageChoice{{}, {}, true}; };
    ports.recognize = [&](auto, const auto &, const auto &) { ++scans; return ImageRecognitionResult{}; };
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); QSignalSpy done(&dialog, &ImageRecognitionDialog::recognitionFinished);
    dialog.scanRegion(); QTRY_COMPARE(done.count(), 1); QVERIFY(done[0][2].toBool()); QCOMPARE(scans, 0); QVERIFY(dialog.values().isEmpty());
  }
  void nativeRegionChooserScansOnlyTheChosenRectangle() {
    const auto qr = code(); QImage screen(qr.width() + 80, qr.height() + 80, QImage::Format_RGB32); screen.fill(Qt::white);
    { QPainter painter(&screen); painter.drawImage(40, 40, qr); }
    ImageRecognitionPorts ports; ports.capture = [screen](auto complete) { complete({screen}); };
#ifndef XERAHS_ZXING
    ports.recognize = [](auto, const QImage &image, const auto &) { return ImageRecognitionResult{{QString("%1x%2").arg(image.width()).arg(image.height())}}; };
#endif
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog.show();
    QSignalSpy done(&dialog, &ImageRecognitionDialog::recognitionFinished); bool selected = false, accepted = false;
    QTimer timer; connect(&timer, &QTimer::timeout, this, [&] {
      auto *crop = dialog.findChild<QDialog *>("recognitionRegionDialog"); if (!crop) return;
      if (!selected) {
        selected = true; crop->findChild<QSpinBox *>("cropLeft")->setValue(40); crop->findChild<QSpinBox *>("cropTop")->setValue(40);
        crop->findChild<QSpinBox *>("cropWidth")->setValue(qr.width()); crop->findChild<QSpinBox *>("cropHeight")->setValue(qr.height());
      }
      auto *apply = crop->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
      if (apply->isEnabled() && !accepted) { accepted = true; QCOMPARE(apply->text(), QString("&Use Region")); apply->click(); }
    });
    timer.start(2); dialog.scanRegion(); QTRY_COMPARE(done.count(), 1); timer.stop(); QVERIFY(accepted);
#ifdef XERAHS_ZXING
    QCOMPARE(dialog.values(), QStringList{"fixture local QR"});
#else
    QCOMPARE(dialog.values(), QStringList{QString("%1x%2").arg(qr.width()).arg(qr.height())});
#endif
  }
  void failureAndNoResultsAreDifferent() {
    ImageRecognitionPorts ports; ports.capture = [](auto complete) { complete({code()}); };
    ports.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{}; };
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); QSignalSpy done(&dialog, &ImageRecognitionDialog::recognitionFinished);
    dialog.scanScreen(); QTRY_COMPARE(done.count(), 1); QVERIFY(dialog.findChild<QLabel *>("recognitionStatus")->text().contains("No QR codes"));
    ports.capture = [](auto complete) { complete({{}, "permission-denied"}); };
    ImageRecognitionDialog denied(ImageRecognitionKind::QrCode, nullptr, ports); QSignalSpy failed(&denied, &ImageRecognitionDialog::recognitionFinished);
    denied.scanScreen(); QTRY_COMPARE(failed.count(), 1); QCOMPARE(failed[0][1].toString(), QString("permission-denied"));
  }
  void closeAbandonsWorkerAndLateCapture() {
    auto entered = std::make_shared<std::atomic<bool>>(false), release = std::make_shared<std::atomic<bool>>(false), finished = std::make_shared<std::atomic<bool>>(false);
    ImageRecognitionPorts ports; ports.capture = [](auto complete) { complete({code()}); };
    ports.recognize = [entered, release, finished](auto, const auto &, const auto &control) { entered->store(true); while (!release->load() && !control.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); finished->store(true); return ImageRecognitionResult{{"late"}}; };
    auto *dialog = new ImageRecognitionDialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog->scanScreen(); QTRY_VERIFY(entered->load());
    QPointer<ImageRecognitionDialog> lifetime(dialog); QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose)); dialog->close(); QTRY_VERIFY(!lifetime);
    release->store(true); QTRY_VERIFY(finished->load());
    std::function<void(RecognitionImageChoice)> complete;
    ports.capture = [&](auto result) { complete = std::move(result); };
    dialog = new ImageRecognitionDialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog->scanScreen(); QTRY_VERIFY(bool(complete)); delete dialog; complete({code()});
  }
};
QTEST_MAIN(ImageRecognitionDialogTest)
#include "ImageRecognitionDialogTest.moc"
