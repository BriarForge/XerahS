#include "CapturePipeline.h"
#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include <QApplication>
#include <QEvent>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(80, 60, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
CaptureAnnotationDialog *task() { return qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget()); }
CapturePipelineReceipt receipt(QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
}
class EditorTaskTest final : public QObject {
  Q_OBJECT
private slots:
  void cleanup() {
    // Deleting an editor also destroys its child popup windows, which can
    // appear later in topLevelWidgets(). Keep guarded pointers in the snapshot.
    QList<QPointer<CaptureAnnotationDialog>> editors;
    for (auto *widget : QApplication::topLevelWidgets())
      if (auto *dialog = qobject_cast<CaptureAnnotationDialog *>(widget)) editors.append(dialog);
    for (const auto &editor : editors) delete editor.data();
  }
  void continueOnceOwnsPixelsAndRunsLaterActions_data() {
    QTest::addColumn<int>("copyResult"); QTest::addColumn<bool>("autoClose");
    for (int result = 0; result < 4; ++result) for (bool close : {false, true})
      QTest::newRow(qPrintable(QString::number(result) + (close ? "-close" : "-keep"))) << result << close;
  }
  void continueOnceOwnsPixelsAndRunsLaterActions() {
    QFETCH(int, copyResult); QFETCH(bool, autoClose);
    QTemporaryDir dir; QImage input = pixels(), copied; int copies = 0; bool copyPixelsCorrect = true;
    QPointer<CaptureAnnotationDialog> editor; QStringList order;
    CaptureActionPorts ports;
    ports.editorPreferences = [=] { return QJsonObject{{"AutoCopyImageToClipboard", true}, {"AutoCloseEditorOnTask", autoClose}}; };
    ports.clipboard = [&](const QImage &image, const CancellationToken &) {
      ++copies; order.append("copy"); copyPixelsCorrect = copyPixelsCorrect && image == pixels();
      if (editor) editor->accept(); // recursive Continue cannot duplicate delivery
      if (copyResult == 3) throw 1;
      if (copyResult == 2) return ActionOutcome{ActionState::Skipped, {}, {}};
      if (copyResult == 1) return ActionOutcome{ActionState::Failed, "private callback content", {}};
      copied = image; return ActionOutcome{};
    };
    const auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    CapturePipeline pipeline(input, dir.path(), "task", {"annotation", "save"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(task()); editor = task();
    QSignalSpy finished(editor, &CaptureAnnotationDialog::taskFinished);
    connect(editor, &CaptureAnnotationDialog::taskFinished, &pipeline, [&](const QString &outcome, const QString &) { QCOMPARE(outcome, QString("Continue")); order.append("host"); if (editor) editor->accept(); });
    input.fill(Qt::green); editor->accept(); editor->accept();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(finished.count(), 1); QCOMPARE(copies, 1); QVERIFY(copyPixelsCorrect); QCOMPARE(order, QStringList({"copy", "host"}));
    const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(result->workingMedia, pixels()); QCOMPARE(QImage(result->file->path), pixels());
    QCOMPARE(result->pipeline.finalWorkingMedia, QString("annotation"));
    QVERIFY(!journal->list().entries.empty()); QVERIFY(journal->list().entries.front().parsed.record);
    QCOMPARE(finished.first().first().toString(), QString("Continue"));
    if (copyResult == 0) { QCOMPARE(copied, pixels()); QVERIFY(finished.first().at(1).toString().isEmpty()); }
    else {
      const auto code = finished.first().at(1).toString(); QCOMPARE(code, copyResult == 3 ? QString("editor-task-auto-copy-unknown") : QString("editor-task-auto-copy-failed"));
      QCOMPARE(result->pipeline.orderedResults.front().artifacts.size(), size_t(2));
      QVERIFY(editor); QVERIFY(editor->isVisible()); QVERIFY(editor->findChild<QLabel *>("captureAnnotationStatus")->text().contains(code));
    }
    if (copyResult == 0 && autoClose) QTRY_VERIFY(!editor);
    else { QVERIFY(editor && editor->isVisible()); editor->accept(); editor->reject(); QTRY_VERIFY(!editor); QCOMPARE(finished.count(), 1); }
    QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void cancelAndDirtyCloseLeaveClipboardFilesAndUploadUntouched_data() {
    QTest::addColumn<bool>("windowClose"); QTest::newRow("Cancel") << false; QTest::newRow("window close") << true;
  }
  void cancelAndDirtyCloseLeaveClipboardFilesAndUploadUntouched() {
    QFETCH(bool, windowClose); QTemporaryDir dir; int copies = 0, saves = 0, uploads = 0;
    QApplication::clipboard()->setText("KEEP");
    CaptureActionPorts ports;
    ports.editorPreferences = [] { return QJsonObject{{"AutoCopyImageToClipboard", true}, {"AutoCloseEditorOnTask", true}, {"ShowExitConfirmation", false}}; };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return ActionOutcome{}; };
    ports.save.encode = [&](const QImage &, QIODevice *) { ++saves; return false; };
    ports.beforeUpload = [&](const CaptureUploadReview &, const CancellationToken &) { ++uploads; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "never", {"annotation", "save", "image-clipboard", "before-upload-window", "upload"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(task()); QPointer<CaptureAnnotationDialog> editor(task()); QSignalSpy finished(editor, &CaptureAnnotationDialog::taskFinished);
    auto *canvas = editor->findChild<EditorCanvas *>(); canvas->zoomToFit();
    const auto a = canvas->viewState().toView({10, 10}).toPoint(), b = canvas->viewState().toView({30, 30}).toPoint();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, a); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, b);
    QTimer timer; int prompts = 0; bool discard = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      ++prompts; QVERIFY(box->standardButtons().testFlag(QMessageBox::Save)); QVERIFY(box->standardButtons().testFlag(QMessageBox::Discard));
      box->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click();
    });
    timer.start(5); if (windowClose) QVERIFY(!editor->close()); else editor->reject();
    QCOMPARE(prompts, 1); QVERIFY(editor && editor->isVisible()); QCOMPARE(finished.count(), 0); QCOMPARE(done.count(), 0);
    discard = true; if (windowClose) editor->close(); else editor->reject(); timer.stop();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toString(), QString("Cancel"));
    QTRY_VERIFY(!editor); const auto result = receipt(done); QCOMPARE(result->pipeline.state, PipelineState::Cancelled);
    QVERIFY(result->files.empty()); QVERIFY(!result->annotationDocument); QCOMPARE(copies, 0); QCOMPARE(saves, 0); QCOMPARE(uploads, 0);
    QCOMPARE(QApplication::clipboard()->text(), QString("KEEP")); QVERIFY(!QFile::exists(dir.filePath("never.png")));
  }
  void ownerDestroyedDuringAutoCopyStillReturnsOneReadyContinue() {
    QTemporaryDir dir; CaptureActionPorts ports; QPointer<CaptureAnnotationDialog> editor;
    ports.editorPreferences = [] { return QJsonObject{{"AutoCopyImageToClipboard", true}, {"AutoCloseEditorOnTask", true}}; };
    int copies = 0;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; delete editor.data(); return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "owned", {"annotation", "save"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(task()); editor = task();
    QSignalSpy finished(editor, &CaptureAnnotationDialog::taskFinished); editor->accept();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(copies, 1); QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().first().toString(), QString("Continue")); QCOMPARE(finished.first().at(1).toString(), QString("editor-task-auto-copy-unknown"));
    QVERIFY(!editor); QVERIFY(receipt(done)->file); QCOMPARE(QImage(receipt(done)->file->path), pixels());
  }
  void upstreamCancelDuringCopyDoesNotBecomeAnEmptyTaskCancel() {
    QTemporaryDir dir; CaptureActionPorts ports; QPointer<CaptureAnnotationDialog> editor; CapturePipeline *host = nullptr;
    ports.editorPreferences = [] { return QJsonObject{{"AutoCopyImageToClipboard", true}, {"AutoCloseEditorOnTask", true}}; };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { host->cancel(); return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "cancel-during", {"annotation", "save"}, nullptr, ports); host = &pipeline;
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(task()); editor = task();
    QSignalSpy finished(editor, &CaptureAnnotationDialog::taskFinished); editor->accept();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toString(), QString("Continue"));
    QCOMPARE(finished.first().at(1).toString(), QString("editor-task-auto-copy-cancelled-during"));
    QVERIFY(editor && editor->isVisible()); QCOMPARE(editor->renderedResult(), pixels()); QVERIFY(editor->document());
    QCOMPARE(receipt(done)->pipeline.state, PipelineState::Cancelled); QVERIFY(receipt(done)->files.empty());
  }
  void quitUsesTheDirtyTaskCancelPolicy() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "quit", {"annotation", "save"});
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(task()); QPointer<CaptureAnnotationDialog> editor(task());
    bool discard = false; int prompts = 0; QTimer driver;
    connect(&driver, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { ++prompts; box->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click(); } });
    driver.start(5); QEvent quit(QEvent::Quit); QApplication::sendEvent(qApp, &quit);
    QVERIFY(!quit.isAccepted()); QVERIFY(editor && editor->isVisible()); QCOMPARE(done.count(), 0);
    // Exercise the same application event filter without terminating the Qt
    // Test application after all editors accepted Close.
    discard = true; QObject sink; QEvent accepted(QEvent::Quit); QApplication::sendEvent(&sink, &accepted); driver.stop();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(prompts, 2); QTRY_VERIFY(!editor); QVERIFY(receipt(done)->files.empty());
  }
  void noAutoCopyDoesNotTouchClipboardAndContinueStillCompletes() {
    QTemporaryDir dir; int copies = 0; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "none", {"annotation"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(task()); task()->accept(); QTRY_COMPARE(done.count(), 1);
    QCOMPARE(copies, 0); QCOMPARE(receipt(done)->workingMedia, pixels());
  }
};
QTEST_MAIN(EditorTaskTest)
#include "EditorTaskTest.moc"
