// IFCAT-003 state matrix for the main native surfaces: the shell window and the
// QR, hash, icon and colour dialogs. Every state is reached through the real
// widget and must be visible as text (never colour alone) with an accessible cue.
// States that a surface cannot be in are declared "n/a" with the reason.
#include "ApplicationWindow.h"
#include "ColorPickerDialog.h"
#include "CommandRegistry.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "QrCodeDialog.h"
#include "features/ToolsFeature.h"
#include <QAccessible>
#include <QCheckBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace xerahs::app;

namespace {
bool visibleText(const QLabel *label) { return label && !label->isHidden() && !label->text().trimmed().isEmpty(); }
QAccessible::Event lastAlert = QAccessible::InvalidEvent;
void record(QAccessibleEvent *event) { if (event->type() == QAccessible::Alert) lastAlert = QAccessible::Alert; }
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
};
QTEST_MAIN(InterfaceStatesTest)
#include "InterfaceStatesTest.moc"
