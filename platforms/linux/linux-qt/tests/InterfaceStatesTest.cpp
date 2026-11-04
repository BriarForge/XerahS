// IFCAT-003 state matrix for the main native surfaces: the shell window and the
// QR, hash, icon and colour dialogs. Every state is reached through the real
// widget and must be visible as text (never colour alone) with an accessible cue.
// States that a surface cannot be in are declared "n/a" with the reason.
#include "ApplicationWindow.h"
#include "ColorPickerDialog.h"
#include "CommandRegistry.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "ImageRecognitionDialog.h"
#include "NewImageDialog.h"
#include "QrCodeDialog.h"
#include "UrlOpenDialog.h"
#include "features/ToolsFeature.h"
#include <QAccessible>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

using namespace xerahs::app;

namespace {
bool visibleText(const QLabel *label) { return label && !label->isHidden() && !label->text().trimmed().isEmpty(); }
QAccessible::Event lastAlert = QAccessible::InvalidEvent;
void record(QAccessibleEvent *event) { if (event->type() == QAccessible::Alert) lastAlert = QAccessible::Alert; }

// The preparation worker blocks here until the test releases it, so the
// loading and cancelled states are reached deterministically.
struct WorkGate { std::atomic<bool> entered{false}, released{false}; };
NewImageHooks gatedHooks(const std::shared_ptr<WorkGate> &gate, bool fail) {
  return {[gate, fail] {
    gate->entered.store(true);
    while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (fail) throw std::runtime_error("private preparation path");
  }};
}

// A network stub: the URL dialog is driven against it, so no request ever
// reaches a real server.
class StubReply final : public QNetworkReply {
public:
  enum class Outcome { Pending, Failed, Success };
  StubReply(const QNetworkRequest &request, Outcome outcome, const QByteArray &payload, QObject *parent)
      : QNetworkReply(parent), m_payload(payload) {
    setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, QStringLiteral("OK"));
    if (!isOpen()) open(ReadOnly | Unbuffered);
    if (outcome == Outcome::Failed) {
      setError(QNetworkReply::ConnectionRefusedError, QStringLiteral("refused"));
      setFinished(true);
      QTimer::singleShot(0, this, [this] { emit finished(); });
    } else if (outcome == Outcome::Success) {
      QTimer::singleShot(0, this, [this] {
        emit readyRead(); emit downloadProgress(m_payload.size(), m_payload.size());
        setFinished(true); emit finished();
      });
    }
  }
  void abort() override {
    if (m_finished) return;
    m_finished = true;
    setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override { return m_payload.size() + QIODevice::bytesAvailable(); }
  bool isSequential() const override { return true; }
  bool atEnd() const override { return m_payload.isEmpty(); }
protected:
  qint64 readData(char *data, qint64 maxSize) override {
    const qint64 count = qMin<qint64>(maxSize, m_payload.size());
    if (count <= 0) return 0;
    std::memcpy(data, m_payload.constData(), size_t(count));
    m_payload.remove(0, int(count));
    return count;
  }
private:
  QByteArray m_payload;
  bool m_finished = false;
};

class StubNetwork final : public QNetworkAccessManager {
public:
  StubReply::Outcome outcome = StubReply::Outcome::Pending;
  QByteArray payload;
  int requests = 0;
protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
    ++requests;
    return new StubReply(request, outcome, payload, this);
  }
};

QByteArray pngBytes() {
  QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::blue);
  QBuffer buffer; buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return buffer.data();
}
}

class InterfaceStatesTest final : public QObject {
  Q_OBJECT
  QTemporaryDir dir;
private slots:
  void initTestCase() { QVERIFY(dir.isValid()); QAccessible::installUpdateHandler(record); }

  // Shell window: initial, loading (determinate/indeterminate), populated, terminal failure,
  // permission denied, dependency unavailable. empty/validation/partial: n/a (it only reports results;
  // partial outcomes are shown by the pipeline result views, validation by each dialog).
  void shellWindowStatesAreVisibleTextWithAnAccessibleCue() {
    CommandRegistry commands; installToolDialogs(commands, {});
    ApplicationWindow window(commands); window.activate();
    auto *status = window.findChild<QLabel *>("operationStatus"); auto *progress = window.findChild<QProgressBar *>("operationProgress");
    QVERIFY(status); QVERIFY(progress);
    QCOMPARE(status->text(), QString("Ready")); QVERIFY(progress->isHidden());                                   // initial
    window.showProgress("Region capture", 40); QVERIFY(!progress->isHidden()); QCOMPARE(progress->maximum(), 100);    // loading, knowable
    QVERIFY2(status->text().contains("40%") && status->text().contains("Cancel"), qPrintable(status->text()));
    window.showActivity("Capture actions", "Saving capture"); QCOMPARE(progress->maximum(), 0); QVERIFY(status->text().contains("Saving"));  // loading, indeterminate
    lastAlert = QAccessible::InvalidEvent;
    window.showResult("Capture", "Saved to the captures folder", true); QVERIFY(progress->isHidden());            // populated / success
    QVERIFY(status->text().contains("Saved")); QCOMPARE(lastAlert, QAccessible::InvalidEvent);
    window.showResult("Capture", "Save failed: the folder is not writable", false);                               // terminal failure
    QVERIFY(status->text().contains("failed")); QCOMPARE(lastAlert, QAccessible::Alert);
    QCOMPARE(status->textFormat(), Qt::PlainText); QVERIFY(!status->accessibleDescription().isEmpty());
    window.showResult("Activation", "XerahS could not secure its runtime files. Check the ownership and permissions of XDG_RUNTIME_DIR.", false);  // permission denied
    QVERIFY(status->text().contains("permissions"));
    auto *video = commands.action("video-converter");                                                              // dependency unavailable
    QVERIFY(video);
    if (!video->isEnabled()) QVERIFY(video->toolTip().contains("GStreamer") && video->text().contains("requires"));
    QVERIFY2(window.findChild<QToolButton *>("video-converter"), "an unavailable command stays visible with its reason, or is explicitly absent");
  }
  void qrDialogStates() {
    QrCodeDialog qr;                                                                                              // initial + empty + validation
    QCOMPARE(qr.errorCode(), QString("qr-empty-content")); QVERIFY(qr.statusText().contains("qr-empty-content"));
    for (auto name : {"qr-copy", "qr-save", "qr-upload"}) QVERIFY(!qr.findChild<QPushButton *>(name)->isEnabled());
    qr.setText("hello");                                                                                          // populated
    QVERIFY(qr.errorCode().isEmpty()); QVERIFY(!qr.image().isNull());
    QVERIFY2(!qr.findChild<QPushButton *>("qr-upload")->isEnabled(), "no upload destination wired: dependency unavailable, not inert");  // dependency unavailable
    qr.setText(QString(3000, 'a')); QCOMPARE(qr.errorCode(), QString("qr-content-too-long")); QVERIFY(qr.image().isNull());  // validation error
    QVERIFY(!qr.statusText().isEmpty());
  }
  void hashCheckerStates() {
    HashCheckerDialog hash;
    auto *status = hash.findChild<QLabel *>("hashStatus"); auto *progress = hash.findChild<QProgressBar *>("hashProgress"); auto *cancel = hash.findChild<QPushButton *>("cancelHash");
    QVERIFY(hash.digest().isEmpty()); QVERIFY(!hash.busy());                                                      // initial / empty
    hash.setFile("/dev/zero"); hash.compute();                                                                   // loading: an endless read stays busy
    QTRY_VERIFY(hash.busy()); QVERIFY(cancel->isEnabled()); QVERIFY(!hash.statusText().isEmpty()); QVERIFY(status && progress);
    hash.cancel(); QTRY_VERIFY(!hash.busy()); QVERIFY(hash.digest().isEmpty()); QVERIFY(!hash.statusText().isEmpty());   // cancelled terminal state
    QFile file(dir.filePath("abc")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("abc"); file.close();
    hash.setFile(file.fileName()); hash.compute(); QTRY_VERIFY(!hash.digest().isEmpty());                          // populated
    hash.findChild<QLineEdit *>("hashCompare")->setText("deadbeef"); QCOMPARE(hash.comparison(), QString("No match"));    // validation, in words
    hash.setFile(dir.filePath("missing")); hash.compute(); QTRY_VERIFY(hash.digest().isEmpty() && !hash.busy());    // error
    QVERIFY2(hash.statusText().contains("not", Qt::CaseInsensitive) || hash.statusText().contains("fail", Qt::CaseInsensitive) || hash.statusText().contains("could", Qt::CaseInsensitive), qPrintable(hash.statusText()));
  }
  void iconConverterStates() {
    IconConverterDialog icon; auto *save = icon.findChild<QPushButton *>("saveIcon");
    QVERIFY(icon.imagePath().isEmpty()); QVERIFY(icon.errorText().isEmpty());                                       // initial / empty
    save->click(); QCOMPARE(icon.errorText(), QString("Choose an image before saving."));                           // validation: Save explains instead of doing nothing
    icon.setImage(dir.filePath("missing.png")); QVERIFY(!icon.errorText().isEmpty());                                // error
    QImage image(64, 64, QImage::Format_ARGB32); image.fill(Qt::blue); QVERIFY(image.save(dir.filePath("ok.png")));
    icon.setImage(dir.filePath("ok.png")); QVERIFY(icon.errorText().isEmpty());                                     // populated
    for (auto *box : icon.sizeCheckBoxes()) box->setChecked(false);
    QVERIFY2(!save->isEnabled(), "no size selected: validation keeps Save disabled");                               // validation
    icon.sizeCheckBoxes().first()->setChecked(true);
  }
  void colourPickerStates() {
    ColorPickerDialog picker; auto *status = picker.findChild<QLabel *>("colourPickerStatus"); auto *hex = picker.findChild<QLineEdit *>("hexColour");
    QVERIFY(!picker.findChild<QPushButton *>("copyAllColorFormats")->isEnabled());                                  // initial / empty
    picker.showColor(QColor("#336699")); QVERIFY(picker.findChild<QPushButton *>("copyAllColorFormats")->isEnabled());  // populated
    hex->setText("zzzzzz"); hex->editingFinished(); QVERIFY(!picker.findChild<QLabel *>("hexColourError")->text().isEmpty()); // validation
    picker.notePortalResult(PortalColorPicker::Outcome::Denied, {});                                                 // permission denied
    QVERIFY2(status->text().contains("refused"), qPrintable(status->text()));
    picker.notePortalResult(PortalColorPicker::Outcome::Failed, {});                                                 // dependency unavailable
    QVERIFY2(status->text().contains("unavailable"), qPrintable(status->text()));
    QVERIFY(picker.findChild<QPushButton *>("pickAgain")->isEnabled());
  }
  // New image: initial, empty, populated, validation, loading, cancelled, error, failure.
  // denied: n/a because the dialog asks for no permission; a refused input is reported as an error.
  // unavailable: n/a because the dialog has no optional dependency; its decoder is always built in.
  void newImageDialogStates() {
    NewImageDialog dialog; dialog.show();
    auto *status = dialog.findChild<QLabel *>("newImageStatus");
    auto *initial = dialog.findChild<QLabel *>("newImageInitial");
    auto *width = dialog.findChild<QSpinBox *>("newImageWidth");
    auto *height = dialog.findChild<QSpinBox *>("newImageHeight");
    auto *background = dialog.findChild<QComboBox *>("newImageBackground");
    auto *colour = dialog.findChild<QLineEdit *>("newImageColour");
    auto *progress = dialog.findChild<QProgressBar *>("newImageProgress");
    auto *cancel = dialog.findChild<QPushButton *>("cancelNewImageWork");
    auto *create = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(status && initial && width && height && background && colour && progress && cancel && create);
    QVERIFY2(status->text().contains("Create prepares"), qPrintable(status->text()));                                     // initial
    QVERIFY(create->isEnabled());
    QCOMPARE(initial->text(), QString("No initial image"));                                                              // empty: nothing is selected
    QImage source(3, 2, QImage::Format_ARGB32); source.fill(Qt::blue);
    QVERIFY(dialog.setInitialImage(source, "Fixture"));
    QVERIFY2(initial->text().contains("Fixture") && initial->text().contains("pixels"), qPrintable(initial->text()));  // populated
    width->setValue(100000); height->setValue(100000);
    QVERIFY(!create->isEnabled());
    QVERIFY2(status->text().contains("limits"), qPrintable(status->text()));                                             // validation: the size is over the limit
    width->setValue(6); height->setValue(4);
    background->setCurrentIndex(1); colour->setText("invalid");
    QVERIFY(!create->isEnabled());
    QVERIFY2(status->text().contains("#AARRGGBB"), qPrintable(status->text()));                                         // validation: the colour is not #AARRGGBB
    colour->setText("#FF0000FF");
    QVERIFY(create->isEnabled());                                                                                        // populated: a valid input enables Create
    const auto gate = std::make_shared<WorkGate>();
    NewImageDialog working(nullptr, gatedHooks(gate, false)); working.show();
    auto *workStatus = working.findChild<QLabel *>("newImageStatus");
    auto *workProgress = working.findChild<QProgressBar *>("newImageProgress");
    auto *workCancel = working.findChild<QPushButton *>("cancelNewImageWork");
    QVERIFY(working.loadInitialFile(dir.filePath("initial.png")));
    QVERIFY2(workStatus->text().contains("Loading"), qPrintable(workStatus->text()));                                    // loading
    QVERIFY(workProgress->isVisible());
    QTRY_VERIFY(gate->entered.load());
    workCancel->click();
    QVERIFY2(workStatus->text().contains("cancelled"), qPrintable(workStatus->text()));                                 // cancelled
    QVERIFY(!workProgress->isVisible());
    gate->released.store(true);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    NewImageDialog failing; failing.show();
    auto *failStatus = failing.findChild<QLabel *>("newImageStatus");
    QFile bad(dir.filePath("bad.png")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
    QVERIFY(failing.loadInitialFile(dir.filePath("bad.png")));
    QTRY_VERIFY_WITH_TIMEOUT(failStatus->text().contains("not a supported image"), 5000);                                // error: the file is not an image
    QVERIFY(failing.loadInitialFile(dir.filePath("missing.png")));
    QTRY_VERIFY_WITH_TIMEOUT(failStatus->text().contains("not a supported image"), 5000);                                // error: the file is missing
    const auto failingGate = std::make_shared<WorkGate>();
    NewImageDialog broken(nullptr, gatedHooks(failingGate, true)); broken.show();
    broken.findChild<QSpinBox *>("newImageWidth")->setValue(4);
    broken.findChild<QSpinBox *>("newImageHeight")->setValue(3);
    broken.accept();
    QTRY_VERIFY(failingGate->entered.load());
    QVERIFY2(broken.findChild<QLabel *>("newImageStatus")->text().contains("Preparing"), "the preparation announces itself"); // loading
    failingGate->released.store(true);
    QTRY_VERIFY_WITH_TIMEOUT(broken.findChild<QLabel *>("newImageStatus")->text().contains("preparation failed"), 5000);  // failure
    QVERIFY(!broken.findChild<QLabel *>("newImageStatus")->text().contains("private"));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
  }
  // URL: initial, empty, validation, denied, populated, loading, cancelled, failure.
  // unavailable: n/a because the dialog has no optional dependency; every refusal is a validation or a failure.
  // The download is driven against a stub network, so no request reaches a real server.
  void urlOpenDialogStates() {
    StubNetwork network;
    UrlOpenOptions options; options.manager = &network;
    UrlOpenDialog dialog(nullptr, options); dialog.show();
    auto *input = dialog.findChild<QLineEdit *>("openImageUrlInput");
    auto *allowHttp = dialog.findChild<QCheckBox *>("openImageUrlAllowHttp");
    auto *privacy = dialog.findChild<QLabel *>("openImageUrlPrivacy");
    auto *status = dialog.findChild<QLabel *>("openImageUrlStatus");
    auto *progress = dialog.findChild<QProgressBar *>("openImageUrlProgress");
    auto *download = dialog.findChild<QPushButton *>("openImageUrlDownload");
    auto *cancel = dialog.findChild<QPushButton *>("openImageUrlCancel");
    QVERIFY(input && allowHttp && privacy && status && progress && download && cancel);
    QVERIFY2(privacy->text().contains("Nothing is downloaded until you press Download"), qPrintable(privacy->text()));   // initial
    QVERIFY(input->text().isEmpty());
    QVERIFY(download->isEnabled());
    QVERIFY(status->text().isEmpty());                                                                                     // initial: nothing has been requested yet
    download->click();
    QVERIFY2(status->text().contains("https://"), qPrintable(status->text()));                                          // empty
    QCOMPARE(network.requests, 0);
    input->setText("ftp://example.com/image.png");
    download->click();
    QVERIFY2(status->text().contains("Only https://"), qPrintable(status->text()));                                      // validation: the scheme is refused
    QCOMPARE(network.requests, 0);
    input->setText("http://example.com/image.png");
    download->click();
    QVERIFY2(status->text().contains("Unencrypted http://"), qPrintable(status->text()));                                // denied: refused without the opt-in
    QCOMPARE(network.requests, 0);
    allowHttp->setChecked(true);
    input->setText("https://example.com/image.png");
    download->click();
    QCOMPARE(network.requests, 1);                                                                                      // populated: the valid address is requested
    QVERIFY2(status->text().contains("Contacting"), qPrintable(status->text()));                                         // loading
    QVERIFY(progress->isVisible());
    cancel->click();
    QVERIFY2(status->text().contains("cancelled"), qPrintable(status->text()));                                         // cancelled
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    StubNetwork served;
    served.outcome = StubReply::Outcome::Success;
    served.payload = pngBytes();
    UrlOpenOptions servedOptions; servedOptions.manager = &served;
    UrlOpenDialog done(nullptr, servedOptions); done.show();
    done.findChild<QLineEdit *>("openImageUrlInput")->setText("https://example.com/image.png");
    done.findChild<QPushButton *>("openImageUrlDownload")->click();
    QTRY_COMPARE(done.result(), int(QDialog::Accepted));
    const auto result = done.takeResult();
    QVERIFY(result);
    QCOMPARE(result->bytes, served.payload);                                                                            // populated: the image is handed over
    QVERIFY2(!done.findChild<QLabel *>("openImageUrlStatus")->text().isEmpty(), "the download reports itself in words");
    StubNetwork refused;
    refused.outcome = StubReply::Outcome::Failed;
    UrlOpenOptions refusedOptions; refusedOptions.manager = &refused;
    UrlOpenDialog broken(nullptr, refusedOptions); broken.show();
    broken.findChild<QLineEdit *>("openImageUrlInput")->setText("https://example.com/image.png");
    broken.findChild<QPushButton *>("openImageUrlDownload")->click();
    QTRY_VERIFY_WITH_TIMEOUT(broken.findChild<QLabel *>("openImageUrlStatus")->text().contains("failed"), 5000);          // failure
  }
  // Recognition: initial, empty, populated, loading, cancelled, failure, denied, unavailable.
  // validation: n/a because the dialog takes no typed input; the values it shows are produced locally.
  // The capture and recognition ports are injected, so no portal or file dialog is ever opened.
  void imageRecognitionDialogStates() {
    ImageRecognitionPorts ports;
    std::function<void(RecognitionImageChoice)> pending;
    ports.capture = [&](auto complete) { pending = std::move(complete); };
    ports.recognize = [](auto, const QImage &, const auto &) { return ImageRecognitionResult{{"fixture QR"}}; };
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog.show();
    auto *status = dialog.findChild<QLabel *>("recognitionStatus");
    auto *results = dialog.findChild<QPlainTextEdit *>("recognitionResults");
    auto *progress = dialog.findChild<QProgressBar *>("recognitionProgress");
    auto *copy = dialog.findChild<QPushButton *>("copyRecognitionResults");
    auto *cancel = dialog.findChild<QPushButton *>("cancelRecognition");
    QVERIFY(status && results && progress && copy && cancel);
    QVERIFY2(status->text().contains("Choose an image file"), qPrintable(status->text()));                                // initial
    QVERIFY(results->toPlainText().isEmpty());                                                                            // empty
    QVERIFY(!copy->isEnabled()); QVERIFY(!cancel->isEnabled());
    dialog.scanScreen();
    QTRY_VERIFY(dialog.busy());
    QVERIFY2(status->text().contains("permission"), qPrintable(status->text()));                                         // loading: it asks the desktop for permission
    QVERIFY(cancel->isEnabled());
    dialog.cancel();
    QTRY_VERIFY(!dialog.busy());
    QVERIFY2(status->text().contains("Cancelled"), qPrintable(status->text()));                                          // cancelled
    dialog.showRecognizedValues({"fixture QR"});
    QCOMPARE(dialog.values(), QStringList{"fixture QR"});
    QVERIFY2(results->toPlainText().contains("fixture QR"), qPrintable(results->toPlainText()));                         // populated
    QVERIFY2(status->text().contains("QR code"), qPrintable(status->text()));
    QVERIFY(copy->isEnabled());
    ImageRecognitionPorts emptyPorts;
    emptyPorts.capture = [](auto complete) { complete({QImage(2, 2, QImage::Format_ARGB32)}); };
    emptyPorts.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{}; };
    ImageRecognitionDialog emptyDialog(ImageRecognitionKind::QrCode, nullptr, emptyPorts);
    emptyDialog.scanScreen();
    QTRY_VERIFY_WITH_TIMEOUT(emptyDialog.findChild<QLabel *>("recognitionStatus")->text().contains("No QR codes found"), 5000); // empty: the scan found nothing
    ImageRecognitionPorts deniedPorts;
    deniedPorts.capture = [](auto complete) { complete({{}, "permission-denied"}); };
    deniedPorts.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{}; };
    ImageRecognitionDialog denied(ImageRecognitionKind::QrCode, nullptr, deniedPorts);
    denied.scanScreen();
    QTRY_VERIFY_WITH_TIMEOUT(denied.findChild<QLabel *>("recognitionStatus")->text().contains("Recognition failed"), 5000);   // denied / failure
    QVERIFY2(denied.findChild<QLabel *>("recognitionStatus")->text().contains("permission-denied"), "the refusal is named");
    ImageRecognitionPorts missingPorts;
    missingPorts.capture = [](auto complete) { complete({QImage(2, 2, QImage::Format_ARGB32)}); };
    missingPorts.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{{}, "qr-scanner-unavailable"}; };
    ImageRecognitionDialog missing(ImageRecognitionKind::QrCode, nullptr, missingPorts);
    missing.scanScreen();
    QTRY_VERIFY_WITH_TIMEOUT(missing.findChild<QLabel *>("recognitionStatus")->text().contains("unavailable"), 5000);       // dependency unavailable
    QVERIFY2(missing.findChild<QLabel *>("recognitionStatus")->text().contains("zxing"), "the missing dependency is named");
    if (!qrScanningAvailable()) {
      ImageRecognitionDialog plain(ImageRecognitionKind::QrCode);                                                        // dependency unavailable
      QVERIFY2(plain.findChild<QLabel *>("recognitionStatus")->text().contains("unavailable"), "the missing scanner is named");
      QVERIFY(!plain.findChild<QPushButton *>("recognitionFile")->isEnabled());
    }
  }
};
QTEST_MAIN(InterfaceStatesTest)
#include "InterfaceStatesTest.moc"
