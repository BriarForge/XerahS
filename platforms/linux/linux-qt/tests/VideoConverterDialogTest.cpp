#include "VideoConverterDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::media;
using xerahs::app::VideoConverterDialog;

namespace {
class FakeRunner final : public ConversionRunner {
public:
  void start(const Request &r) override { last = r; ++starts; running = true; }
  void cancel() override { if (!running) return; ++cancels; running = false; ConversionResult res; res.outcome = Outcome::Cancelled; emit finished(res); }
  bool busy() const override { return running; }
  void complete(const ConversionResult &r) { running = false; emit finished(r); }
  Request last; int starts = 0, cancels = 0; bool running = false;
};
}

class VideoConverterDialogTest : public QObject {
  Q_OBJECT
  QTemporaryDir dir;
  QString makeInput() { const auto p = dir.filePath("clip.mkv"); QFile f(p); (void)f.open(QIODevice::WriteOnly); f.write("x"); return p; }
private slots:
  void listsDisabledUnavailableCodecs() {
    SetAvailability port({"x264enc", "mp4mux", "matroskamux"});
    auto *runner = new FakeRunner; VideoConverterDialog d(runner, &port);
    auto *container = d.findChild<QComboBox *>("vcContainer"); auto *codec = d.findChild<QComboBox *>("vcCodec");
    container->setCurrentIndex(container->findData("mp4"));
    QCOMPARE(codec->count(), 9);   // 8 H.264/H.265 codecs + AV1
    auto *model = qobject_cast<QStandardItemModel *>(codec->model());
    int enabled = 0; for (int i = 0; i < codec->count(); ++i) if (model->item(i)->isEnabled()) ++enabled;
    QCOMPARE(enabled, 1); QCOMPARE(codec->currentData().toString(), QString("X264"));
    const int nv = codec->findData("H264Nvenc"); QVERIFY(!model->item(nv)->isEnabled());
    QVERIFY(codec->itemData(nv, Qt::ToolTipRole).toString().contains("hardware"));
    container->setCurrentIndex(container->findData("webm"));
    QCOMPARE(codec->count(), 3);   // Vp8, Vp9, Av1; none usable
    QCOMPARE(codec->currentData().toString(), QString("Vp8"));   // nothing usable: first row shown, Convert reports the reason
  }
  void convertFlowSuccessAndFolder() {
    SetAvailability port({"x264enc", "mp4mux", "matroskamux", "opusenc"});
    auto *runner = new FakeRunner; VideoConverterDialog d(runner, &port);
    QString opened; d.setFolderOpener([&](const QString &f) { opened = f; return true; });
    d.setInput(makeInput());
    QVERIFY(d.output().endsWith("clip-converted.mp4"));
    auto *show = d.findChild<QPushButton *>("vcShow"); QVERIFY(!show->isVisibleTo(&d));
    d.findChild<QPushButton *>("vcConvert")->click();
    QCOMPARE(runner->starts, 1); QCOMPARE(runner->last.codecId, QString("X264")); QVERIFY(d.busy());
    QVERIFY(!d.findChild<QPushButton *>("vcConvert")->isEnabled()); QVERIFY(d.findChild<QPushButton *>("vcCancel")->isEnabled());
    ConversionResult ok; ok.outcome = Outcome::Success; ok.outputPath = d.output(); runner->complete(ok);
    QVERIFY(d.statusText().startsWith("Saved clip-converted.mp4")); QVERIFY(show->isVisibleTo(&d));
    show->click(); QCOMPARE(opened, dir.path());
  }
  void validationAndExistingOutput() {
    SetAvailability port({"x264enc", "mp4mux"});
    auto *runner = new FakeRunner; VideoConverterDialog d(runner, &port);
    d.findChild<QPushButton *>("vcConvert")->click();
    QVERIFY(d.statusText().contains("VC_EMPTY_INPUT")); QCOMPARE(runner->starts, 0);
    const auto in = makeInput(); d.setInput(in);
    d.findChild<QLineEdit *>("vcOutput")->setText(in);
    d.findChild<QPushButton *>("vcConvert")->click();
    QVERIFY(d.statusText().contains("VC_OUTPUT_IS_INPUT")); QCOMPARE(runner->starts, 0);
  }
  void cancelAndCloseWhileBusy() {
    SetAvailability port({"x264enc", "mp4mux"});
    auto *runner = new FakeRunner; VideoConverterDialog d(runner, &port);
    d.setInput(makeInput()); d.findChild<QPushButton *>("vcConvert")->click(); QVERIFY(d.busy());
    d.findChild<QPushButton *>("vcCancel")->click();
    QCOMPARE(runner->cancels, 1); QVERIFY(d.statusText().startsWith("Cancelled")); QVERIFY(!d.busy());
    d.findChild<QPushButton *>("vcConvert")->click(); QVERIFY(d.busy());
    d.show(); d.close();   // EU-017
    QCOMPARE(runner->cancels, 2); QVERIFY(!runner->busy());
  }
  void failureShowsStableCodeAndAccessibleNames() {
    SetAvailability port({"x264enc", "mp4mux"});
    auto *runner = new FakeRunner; VideoConverterDialog d(runner, &port);
    d.setInput(makeInput()); d.findChild<QPushButton *>("vcConvert")->click();
    ConversionResult bad; bad.outcome = Outcome::Failed; bad.error = ErrorCode::PipelineFailed; bad.message = "boom"; runner->complete(bad);
    QVERIFY(d.statusText().contains("VC_PIPELINE_FAILED")); QVERIFY(d.statusText().contains("boom"));
    for (const auto &w : d.findChildren<QWidget *>()) if (w->objectName().startsWith("vc") && !qobject_cast<QProgressBar *>(w) && !qobject_cast<QLabel *>(w) || qobject_cast<QPushButton *>(w))
      QVERIFY2(!w->accessibleName().isEmpty(), qPrintable(w->objectName()));
  }
};

QTEST_MAIN(VideoConverterDialogTest)
#include "VideoConverterDialogTest.moc"
