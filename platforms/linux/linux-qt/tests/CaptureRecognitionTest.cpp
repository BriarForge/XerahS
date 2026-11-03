#include "CapturePipeline.h"
#include "ImageRecognitionDialog.h"
#include "QrRender.h"
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QJsonDocument>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage qr() { return renderQr(xerahs::tools::encodeQr(QStringLiteral("private QR fixture"), xerahs::tools::QrEcc::M).matrix, 300).image; }
QImage text() {
  QImage image(1200, 240, QImage::Format_ARGB32); image.fill(Qt::white);
  QPainter painter(&image); QFont font("DejaVu Sans"); font.setPixelSize(64); painter.setFont(font); painter.setPen(Qt::black);
  painter.drawText(QRect(40,40,1120,160), Qt::AlignLeft | Qt::AlignVCenter, "LOCAL OCR TEST 123"); return image;
}
CapturePipelineReceipt receipt(const QSignalSpy &done) { return qvariant_cast<CapturePipelineReceipt>(done.first().first()); }
const ActionResult &action(const CapturePipelineReceipt &r, const QString &id) {
  for (const auto &result : r->pipeline.orderedResults) if (result.action == id) return result;
  qFatal("Missing expected capture action");
}
}
class CaptureRecognitionTest final : public QObject {
  Q_OBJECT
private slots:
  void imageOnlyPayloadCannotPretendToContainRecognizedText() {
    QTemporaryDir dir; int copies = 0;
    CaptureActionPorts ports; ports.textClipboard = [&](const QString &, const auto &) { ++copies; return ActionOutcome{}; };
    CaptureActions executor(qr(), dir.path(), "fixture", ports); CancellationToken cancellation;
    QApplication::clipboard()->setText("keep private clipboard");
    const auto index = catalogueIndex("ocr-clipboard"); QVERIFY(index);
    const auto result = executor.run({catalogue()[*index], {}, cancellation});
    QCOMPARE(result.state, ActionState::Failed); QCOMPARE(result.diagnostic, std::optional<QString>{"unsupported-content"});
    QCOMPARE(copies, 0); QCOMPARE(QApplication::clipboard()->text(), QString("keep private clipboard"));
  }
  void cancelledClipboardActionsDoNotReachTheirPublicationPorts() {
    QTemporaryDir dir; int copies = 0; CancellationToken cancellation; cancellation.request();
    CaptureActionPorts ports;
    ports.clipboard = [&](const auto &, const auto &) { ++copies; return ActionOutcome{}; };
    ports.textClipboard = [&](const auto &, const auto &) { ++copies; return ActionOutcome{}; };
    CaptureActions executor(qr(), dir.path(), "fixture", ports);
    for (const auto &id : {"image-clipboard", "url-clipboard", "ocr-clipboard"}) {
      const auto index = catalogueIndex(id); QVERIFY(index);
      QCOMPARE(executor.run({catalogue()[*index], {}, cancellation}).state, ActionState::Cancelled);
    }
    QCOMPARE(copies, 0);
  }
  void cleanup() { for (auto *w : QApplication::topLevelWidgets()) if (qobject_cast<ImageRecognitionDialog *>(w)) delete w; }
  void qrShowsOwnedResultsAndKeepsPixelsAndJournalPrivate() {
    if (!qrScanningAvailable()) QSKIP("QR engine absent; unavailable path is tested separately.");
    QTemporaryDir dir; const auto image = qr(); CaptureActionPorts ports; bool gui = false; QStringList values;
    ports.recognitionResult = [&](auto kind, const QStringList &found, const auto &) {
      gui = QThread::currentThread() == qApp->thread(); values = found;
      return CaptureActions::showNativeRecognition(kind, found);
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    CapturePipeline pipeline(image, dir.filePath("captures"), "fixture", {"qr-scan", "save"}, nullptr, ports, journal);
    QApplication::clipboard()->setText("clipboard sentinel"); QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start();
    QTRY_COMPARE(done.count(), 1); const auto r = receipt(done); QVERIFY(gui); QCOMPARE(values, QStringList{"private QR fixture"});
    QCOMPARE(action(r,"qr-scan").state, ActionState::Succeeded); QVERIFY(r->file); QCOMPARE(QImage(r->file->path), image); QCOMPARE(r->workingMedia,image);
    QVERIFY(!QJsonDocument(r->canonicalResult()).toJson().contains("private QR fixture"));
    const auto records = journal->list(); QCOMPARE(records.entries.size(), size_t(1));
    QVERIFY(!QJsonDocument(records.entries.front().parsed.record->report).toJson().contains("private QR fixture"));
    ImageRecognitionDialog *dialog = nullptr;
    for (auto *w : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<ImageRecognitionDialog *>(w)) dialog = candidate;
    QVERIFY(dialog && dialog->isVisible()); QCOMPARE(dialog->values(), values); QCOMPARE(QApplication::clipboard()->text(), QString("clipboard sentinel"));
    dialog->findChild<QPushButton *>("copyRecognitionResults")->click(); QCOMPARE(QApplication::clipboard()->text(), values.first());
  }
  void ocrRunsBeforeItsClipboardAndKeepsTextOutOfReceipts() {
    if (!textRecognitionAvailable()) QSKIP("OCR engine absent; unavailable path is tested separately.");
    QTemporaryDir dir; CaptureActionPorts ports; QString copied; bool gui = true;
    ports.recognitionResult = [&](auto kind, const QStringList &values, const auto &) {
      gui &= QThread::currentThread() == qApp->thread(); if (kind != ImageRecognitionKind::Text || values.size()!=1) return ActionOutcome{ActionState::Failed,"wrong-result",{}}; return ActionOutcome{};
    };
    ports.textClipboard = [&](const QString &value, const auto &) { gui &= QThread::currentThread()==qApp->thread(); copied=value; QApplication::clipboard()->setText(value); return ActionOutcome{}; };
    CapturePipeline pipeline(text(), dir.path(), "fixture", {"ocr-clipboard","ocr"}, nullptr, ports);
    QSignalSpy done(&pipeline,&CapturePipeline::finished); pipeline.start(); QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,7000);
    const auto r=receipt(done); QVERIFY(gui); QCOMPARE(copied.simplified(),QString("LOCAL OCR TEST 123"));
    QCOMPARE(r->pipeline.orderedResults[0].action,QString("ocr")); QCOMPARE(action(r,"ocr-clipboard").state,ActionState::Succeeded);
    QVERIFY(!QJsonDocument(r->canonicalResult()).toJson().contains("LOCAL OCR TEST"));
  }
  void emptyOcrPreservesTheClipboard() {
    if (!textRecognitionAvailable()) QSKIP("OCR engine absent.");
    QImage blank(900,220,QImage::Format_ARGB32); blank.fill(Qt::white); QTemporaryDir dir; CaptureActionPorts ports; int copies=0;
    ports.recognitionResult=[](auto,const auto &,const auto &){return ActionOutcome{};};
    ports.textClipboard=[&](const auto &,const auto &){++copies;return ActionOutcome{};};
    QApplication::clipboard()->setText("keep existing clipboard");
    CapturePipeline pipeline(blank,dir.path(),"fixture",{"ocr","ocr-clipboard"},nullptr,ports); QSignalSpy done(&pipeline,&CapturePipeline::finished);pipeline.start();QTRY_COMPARE(done.count(),1);
    QCOMPARE(copies,0); QCOMPARE(QApplication::clipboard()->text(),QString("keep existing clipboard"));
    QCOMPARE(action(receipt(done),"ocr-clipboard").diagnostic,std::optional<QString>("ocr-no-text"));
    QCOMPARE(action(receipt(done),"ocr-clipboard").state,ActionState::Succeeded);
  }
  void failedOcrPresentationSkipsDependentClipboardAndKeepsIndependentPin() {
    if (!textRecognitionAvailable()) QSKIP("OCR engine absent.");
    QTemporaryDir dir; CaptureActionPorts ports; int copies=0,pins=0;
    ports.recognitionResult=[](auto,const auto &,const auto &){return ActionOutcome{ActionState::Failed,"recognition-result-unavailable",{}};};
    ports.textClipboard=[&](const auto &,const auto &){++copies;return ActionOutcome{};};
    ports.pin=[&](const QImage &,const auto &){++pins;return ActionOutcome{};};
    CapturePipeline pipeline(text(),dir.path(),"fixture",{"ocr","ocr-clipboard","pin"},nullptr,ports);QSignalSpy done(&pipeline,&CapturePipeline::finished);pipeline.start();QTRY_COMPARE(done.count(),1);
    const auto r=receipt(done);QCOMPARE(action(r,"ocr").state,ActionState::Failed);QCOMPARE(action(r,"ocr-clipboard").state,ActionState::Skipped);
    QCOMPARE(action(r,"ocr-clipboard").missingArtifact,std::optional<QString>("ocr.text"));QCOMPARE(copies,0);QCOMPARE(pins,1);QVERIFY(!r->workingMedia.isNull());
  }
  void cancellationAtResultPublicationDoesNotCopyText() {
    if (!textRecognitionAvailable()) QSKIP("OCR engine absent.");
    QTemporaryDir dir; CaptureActionPorts ports; CapturePipeline *active=nullptr; int copies=0;
    ports.recognitionResult=[&](auto,const auto &,const auto &){active->cancel();return ActionOutcome{ActionState::Cancelled,{},{}};};
    ports.textClipboard=[&](const auto &,const auto &){++copies;return ActionOutcome{};};
    CapturePipeline pipeline(text(),dir.path(),"fixture",{"ocr","ocr-clipboard"},nullptr,ports);active=&pipeline;QSignalSpy done(&pipeline,&CapturePipeline::finished);pipeline.start();QTRY_COMPARE(done.count(),1);
    QCOMPARE(copies,0);QCOMPARE(receipt(done)->pipeline.state,PipelineState::Cancelled);QCOMPARE(action(receipt(done),"ocr").state,ActionState::Cancelled);
  }
  void missingEngineHasAnExplicitFailure() {
    if (qrScanningAvailable() && textRecognitionAvailable()) QSKIP("Both engines available in this build.");
    QTemporaryDir dir; const bool qrMissing=!qrScanningAvailable();
    CapturePipeline pipeline(qr(),dir.path(),"fixture",{qrMissing?"qr-scan":"ocr"});QSignalSpy done(&pipeline,&CapturePipeline::finished);pipeline.start();QTRY_COMPARE(done.count(),1);
    const auto &result=receipt(done)->pipeline.orderedResults.front();QCOMPARE(result.state,ActionState::Failed);
    QCOMPARE(result.diagnostic,std::optional<QString>(qrMissing?"qr-scanner-unavailable":"ocr-unavailable"));
    QCOMPARE(result.failureClass,std::optional<FailureClass>{FailureClass::UnavailableDependency});
  }
};
QTEST_MAIN(CaptureRecognitionTest)
#include "CaptureRecognitionTest.moc"
