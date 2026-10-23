// Real native modal selection, Core plan replacement, PNG/clipboard dispatch
// and private journal reads. No desktop permission, live prefs or upload I/O.
#include "ActionSelectionDialog.h"
#include "CapturePipeline.h"
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
ActionSelectionDialog *chooser() { return qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget()); }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
QStringList resultActions(const CapturePipelineReceipt &result) {
  QStringList ids; for (const auto &action : result->pipeline.orderedResults) ids.append(action.action); return ids;
}
const QStringList configured{"after-capture-window", "save", "image-clipboard"};
}
class ActionSelectionTest final : public QObject {
  Q_OBJECT
private slots:
  void replacementRunsOnlyChosenNativeActionsAndJournalsTheEffectivePlan_data() {
    QTest::addColumn<bool>("save"); QTest::addColumn<bool>("copy");
    QTest::newRow("save only") << true << false;
    QTest::newRow("copy only") << false << true;
    QTest::newRow("keep capture only") << false << false;
    QTest::newRow("both") << true << true;
  }
  void replacementRunsOnlyChosenNativeActionsAndJournalsTheEffectivePlan() {
    QFETCH(bool, save); QFETCH(bool, copy); QTemporaryDir dir;
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &image, const CancellationToken &) {
      ++copies; QApplication::clipboard()->setImage(image); return ActionOutcome{};
    };
    QApplication::clipboard()->setText("private fixture clipboard");
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "chosen", configured, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(chooser());
    QPointer<ActionSelectionDialog> dialog(chooser()); QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
    QVERIFY(!QFileInfo::exists(dir.filePath("output"))); QVERIFY(!QFileInfo::exists(dir.filePath("state"))); QCOMPARE(copies, 0);
    auto *saveBox = dialog->findChild<QCheckBox *>("capture-action-save"); auto *copyBox = dialog->findChild<QCheckBox *>("capture-action-image-clipboard");
    QVERIFY(saveBox && copyBox); QVERIFY(saveBox->isChecked() && copyBox->isChecked());
    saveBox->setChecked(save); copyBox->setChecked(copy);
    auto *run = dialog->findChild<QPushButton *>("runCaptureActions"); QVERIFY(run);
    run->setFocus(); QTest::keyClick(run, Qt::Key_Space); QTRY_COMPARE(done.count(), 1); QTRY_VERIFY(!dialog);
    const auto result = receipt(done); QStringList expected{"after-capture-window"};
    if (save) expected.append("save"); if (copy) expected.append("image-clipboard");
    QCOMPARE(resultActions(result), expected); QCOMPARE(result->pipeline.state, PipelineState::Completed);
    QCOMPARE(result->workingMedia, pixels()); QCOMPARE(bool(result->file), save); QCOMPARE(copies, copy ? 1 : 0);
    for (const auto &action : result->pipeline.orderedResults) QCOMPARE(action.state, ActionState::Succeeded);
    if (save) {
      QCOMPARE(QImage(result->file->path), pixels()); const auto listing = journal->list(); QCOMPARE(listing.entries.size(), size_t(1));
      QVERIFY(listing.entries[0].parsed.record); QVERIFY(listing.entries[0].parsed.record->terminal);
      QCOMPARE(listing.entries[0].parsed.record->selected, expected);
      QCOMPARE(listing.entries[0].parsed.record->report.value("ordered_results"), result->canonicalResult().value("ordered_results"));
    } else { QVERIFY(!QFileInfo::exists(dir.filePath("output"))); QVERIFY(!QFileInfo::exists(dir.filePath("state"))); }
    if (copy) QCOMPARE(QApplication::clipboard()->image(), pixels());
    else QCOMPARE(QApplication::clipboard()->text(), QString("private fixture clipboard"));
    QCOMPARE(configured, (QStringList{"after-capture-window", "save", "image-clipboard"}));
  }
  void keyboardCancellationAndClosePreservePixelsWithoutOutputs_data() {
    QTest::addColumn<QString>("method");
    for (const auto &method : {"escape", "enter", "close", "cancel operation", "destroy dialog"}) QTest::newRow(method) << QString(method);
  }
  void keyboardCancellationAndClosePreservePixelsWithoutOutputs() {
    QFETCH(QString, method); QTemporaryDir dir; int copies = 0;
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return ActionOutcome{}; };
    QApplication::clipboard()->setText("cancel fixture clipboard");
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "cancelled", configured, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(chooser()); QPointer<ActionSelectionDialog> dialog(chooser());
    bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    if (method == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
    else if (method == "enter") QTest::keyClick(dialog, Qt::Key_Return);
    else if (method == "close") dialog->close();
    else if (method == "cancel operation") pipeline.cancel();
    else delete dialog.data();
    QTRY_COMPARE(done.count(), 1); QTRY_VERIFY(!dialog); const auto result = receipt(done);
    QCOMPARE(result->pipeline.state, PipelineState::Cancelled); QCOMPARE(resultActions(result), configured);
    for (const auto &action : result->pipeline.orderedResults) QCOMPARE(action.state, ActionState::Cancelled);
    QCOMPARE(result->workingMedia, pixels()); QVERIFY(!result->file); QCOMPARE(copies, 0);
    QVERIFY(!QFileInfo::exists(dir.filePath("output"))); QVERIFY(!QFileInfo::exists(dir.filePath("state")));
    QCOMPARE(QApplication::clipboard()->text(), QString("cancel fixture clipboard"));
  }
  void unavailableCatalogueIsAccessibleAndCannotBeAccepted() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", configured);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(chooser()); auto *dialog = chooser();
    const auto boxes = dialog->findChildren<QCheckBox *>(); QCOMPARE(boxes.size(), 20);
    auto *implementedUpload = dialog->findChild<QCheckBox *>("capture-action-upload"); QVERIFY(implementedUpload); QVERIFY(implementedUpload->isEnabled());
    auto *upload = dialog->findChild<QCheckBox *>("capture-action-ocr"); QVERIFY(upload); QVERIFY(!upload->isEnabled());
    QVERIFY(upload->text().contains("unavailable")); auto *interface = QAccessible::queryAccessibleInterface(upload); QVERIFY(interface);
    QVERIFY(interface->state().disabled); QVERIFY(interface->text(QAccessible::Name).contains("OCR"));
    auto *save = dialog->findChild<QCheckBox *>("capture-action-save"); save->setFocus(); QTest::keyClick(save, Qt::Key_Space); QVERIFY(!save->isChecked());
    upload->setChecked(true); dialog->accept(); QVERIFY(dialog->isVisible()); QCOMPARE(done.count(), 0);
    QVERIFY(!QFileInfo::exists(dir.filePath("output"))); QTest::keyClick(dialog, Qt::Key_Escape); QTRY_COMPARE(done.count(), 1);
  }
  void cancellationAtSelectionAdmissionAndOwnerDestructionCannotExecuteLateChoices() {
    QTemporaryDir dir; int copies = 0; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return ActionOutcome{}; };
    CapturePipeline cancelled(pixels(), dir.filePath("output"), "capture", configured, nullptr, ports);
    connect(&cancelled, &CapturePipeline::choosingActions, &cancelled, &CapturePipeline::cancel);
    QSignalSpy done(&cancelled, &CapturePipeline::finished); cancelled.start(); QTRY_COMPARE(done.count(), 1);
    QVERIFY(!chooser()); QCOMPARE(receipt(done)->pipeline.state, PipelineState::Cancelled);
    auto pipeline = std::make_unique<CapturePipeline>(pixels(), dir.filePath("output"), "capture", configured, nullptr, ports);
    pipeline->start(); QTRY_VERIFY(chooser()); QPointer<ActionSelectionDialog> dialog(chooser()); pipeline.reset();
    QVERIFY(!dialog); QTest::qWait(30); QVERIFY(!chooser()); QCOMPARE(copies, 0); QVERIFY(!QFileInfo::exists(dir.filePath("output")));
  }
  void explicitRetryPreservesSelectorSuccessWithoutOpeningItAgain() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); int copies = 0, saves = 0;
    CaptureActionPorts ports; ports.save.encode = [&](const QImage &image, QIODevice *output) { ++saves; return image.save(output, "PNG"); };
    ports.clipboard = [&](const QImage &, const CancellationToken &) {
      return ++copies == 1 ? ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}} : ActionOutcome{};
    };
    CapturePipeline original(pixels(), dir.filePath("output"), "capture", configured, nullptr, ports, journal);
    QSignalSpy first(&original, &CapturePipeline::finished); original.start(); QTRY_VERIFY(chooser()); chooser()->accept(); QTRY_COMPARE(first.count(), 1);
    const auto source = receipt(first); QVERIFY(source->retryPlan().available());
    CapturePipeline retry(source, nullptr, ports, journal); QSignalSpy next(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(next.count(), 1);
    QVERIFY(!chooser()); QCOMPARE(saves, 1); QCOMPARE(copies, 2); const auto result = receipt(next);
    QCOMPARE(resultActions(result), QStringList{"image-clipboard"});
    QCOMPARE(result->pipeline.retry->preservedSuccessfulActions, (QStringList{"after-capture-window", "save"}));
    QCOMPARE(result->file->path, source->file->path); QCOMPARE(journal->list().entries.size(), size_t(2));
  }
  void destroyingTheTransientParentCancelsTheWorkerWithoutOutput() {
    QTemporaryDir dir; int copies = 0; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return ActionOutcome{}; };
    auto parent = std::make_unique<QWidget>(); parent->show();
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", configured, nullptr, ports, {}, parent.get());
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(chooser());
    QPointer<ActionSelectionDialog> dialog(chooser()); QCOMPARE(dialog->parentWidget(), parent.get()); parent.reset();
    QTRY_COMPARE(done.count(), 1); QVERIFY(!dialog); QCOMPARE(receipt(done)->pipeline.state, PipelineState::Cancelled);
    QCOMPARE(receipt(done)->workingMedia, pixels()); QCOMPARE(copies, 0); QVERIFY(!QFileInfo::exists(dir.filePath("output")));
  }
};
QTEST_MAIN(ActionSelectionTest)
#include "ActionSelectionTest.moc"
