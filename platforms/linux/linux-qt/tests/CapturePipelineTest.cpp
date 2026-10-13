#include "CapturePipeline.h"
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <stdexcept>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(QColor(23, 45, 67)); return image; }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
struct SaveGate { std::atomic<bool> entered{false}, released{false}, onWorker{false}; };
struct ReleaseGate { std::shared_ptr<SaveGate> gate; ~ReleaseGate() { gate->released.store(true); } };
void waitAtSave(const std::shared_ptr<SaveGate> &gate) {
  gate->onWorker.store(QThread::currentThread() != qGuiApp->thread()); gate->entered.store(true);
  QElapsedTimer deadline; deadline.start();
  while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
  if (!gate->released.load()) throw std::runtime_error("fixture deadline");
}
}
class CapturePipelineTest final : public QObject {
  Q_OBJECT
private slots:
  void retriesFailedSaveWithOriginalSettingsAndDoesNotRepeatSuccessfulClipboard() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); int encoded = 0, copied = 0;
    CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *output) { ++encoded; return encoded > 1 && image.save(output, "PNG"); };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline initial(pixels(), dir.filePath("output"), "original-name", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy originalDone(&initial, &CapturePipeline::finished); initial.start(); QTRY_COMPARE(originalDone.count(), 1);
    const auto original = receipt(originalDone); const auto originalReport = original->canonicalResult(); QVERIFY(!original->file); QCOMPARE(copied, 1);
    CapturePipeline retry(original, nullptr, ports, journal); QSignalSpy done(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(encoded, 2); QCOMPARE(copied, 1);
    QCOMPARE(result->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(result->pipeline.orderedResults[0].action, QString("save"));
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Succeeded); QCOMPARE(result->file->path, dir.filePath("output/original-name.png"));
    QCOMPARE(QImage(result->file->path), pixels()); QCOMPARE(result->file->creatorTaskId, result->pipeline.taskId);
    QVERIFY(result->pipeline.retry); QCOMPARE(result->pipeline.retry->originalTaskId, original->pipeline.taskId);
    QCOMPARE(result->pipeline.retry->preservedSuccessfulActions, QStringList{"image-clipboard"});
    QCOMPARE(original->canonicalResult(), originalReport); QVERIFY(!result->retryPlan().available());
    const auto stored = journal->list(); QCOMPARE(stored.entries.size(), size_t(2));
    for (const auto &entry : stored.entries) { QVERIFY(entry.parsed.record); QVERIFY(entry.parsed.record->terminal); }
  }
  void retriesOnlyFailedClipboardAndKeepsTheExistingFileAndAppendOnlyLineage() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); int encoded = 0, copied = 0; bool onGui = true;
    CaptureActionPorts ports; ports.save.encode = [&](const QImage &image, QIODevice *output) { ++encoded; return image.save(output, "PNG"); };
    ports.clipboard = [&](const QImage &image, const CancellationToken &) {
      onGui = onGui && QThread::currentThread() == qGuiApp->thread(); ++copied;
      return copied < 3 || image != pixels() ? ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}} : ActionOutcome{};
    };
    CapturePipeline initial(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy originalDone(&initial, &CapturePipeline::finished); initial.start(); QTRY_COMPARE(originalDone.count(), 1);
    const auto original = receipt(originalDone); QVERIFY(original->file); const auto originalReport = original->canonicalResult();
    QFile existing(original->file->path); QVERIFY(existing.open(QIODevice::ReadOnly)); const auto before = existing.readAll(); existing.close();
    CapturePipeline retry(original, nullptr, ports, journal); QSignalSpy done(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(done.count(), 1);
    const auto first = receipt(done); QCOMPARE(first->pipeline.retry->attempt, 1); QVERIFY(first->retryPlan().available());
    CapturePipeline again(first, nullptr, ports, journal); QSignalSpy next(&again, &CapturePipeline::finished); again.start(); QTRY_COMPARE(next.count(), 1);
    const auto second = receipt(next); QCOMPARE(encoded, 1); QCOMPARE(copied, 3); QVERIFY(onGui);
    QCOMPARE(second->pipeline.retry->originalTaskId, original->pipeline.taskId); QCOMPARE(second->pipeline.retry->previousTaskId, first->pipeline.taskId);
    QCOMPARE(second->pipeline.retry->attempt, 2); QCOMPARE(second->pipeline.retry->preservedSuccessfulActions, QStringList{"save"});
    QCOMPARE(second->file->path, original->file->path); QCOMPARE(second->file->creatorTaskId, original->pipeline.taskId);
    QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), before); existing.close();
    QCOMPARE(original->canonicalResult(), originalReport); QCOMPARE(first->pipeline.orderedResults[0].state, ActionState::Failed);
    QCOMPARE(second->pipeline.orderedResults[0].state, ActionState::Succeeded); QCOMPARE(journal->list().entries.size(), size_t(3));
  }
  void cancellingRetryBeforeStartKeepsTheOriginalArtifactPixelsAndDurableLink() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); CaptureActionPorts originalPorts;
    originalPorts.clipboard = [](const QImage &, const CancellationToken &) { return ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}}; };
    CapturePipeline initial(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, originalPorts, journal);
    QSignalSpy originalDone(&initial, &CapturePipeline::finished); initial.start(); QTRY_COMPARE(originalDone.count(), 1);
    const auto original = receipt(originalDone); int started = 0; CaptureActionPorts ports; ports.actionStarted = [&](const QString &) { ++started; };
    CapturePipeline retry(original, nullptr, ports, journal); connect(&retry, &CapturePipeline::activityChanged, &retry,
        [&](const QString &text) { if (text == "Preparing capture actions.") retry.cancel(); });
    QSignalSpy done(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QCOMPARE(started, 0); QCOMPARE(result->pipeline.state, PipelineState::Cancelled);
    QCOMPARE(result->workingMedia, original->workingMedia); QCOMPARE(result->file->path, original->file->path); QCOMPARE(QImage(result->file->path), pixels());
    const auto listing = journal->list(); QCOMPARE(listing.entries.size(), size_t(2));
    for (const auto &entry : listing.entries) if (entry.taskId == result->pipeline.taskId) {
      QVERIFY(entry.parsed.record); QVERIFY(entry.parsed.record->terminal);
      QCOMPARE(entry.parsed.record->report.value("pipeline_state"), QJsonValue("cancelled"));
    }
  }
  void unknownOutcomeCannotRetryAndStorageFailureCannotLoseTheSuccessfulSave() {
    QTemporaryDir dir; const auto state = dir.filePath("state"); auto journal = std::make_shared<NativePipelineJournal>(state); int copied = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}}; };
    CapturePipeline initial(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy originalDone(&initial, &CapturePipeline::finished); initial.start(); QTRY_COMPARE(originalDone.count(), 1);
    const auto original = receipt(originalDone); auto unknown = std::make_shared<CapturePipelineResult>(*original); unknown->pipeline.state = PipelineState::Interrupted;
    CapturePipeline rejected(unknown, nullptr, ports, journal); QSignalSpy rejection(&rejected, &CapturePipeline::finished); rejected.start(); QTRY_COMPARE(rejection.count(), 1);
    QCOMPARE(receipt(rejection)->diagnostic, QString("retry-outcome-unknown")); QCOMPARE(receipt(rejection)->pipeline.state, PipelineState::Completed);
    QCOMPARE(unknown->pipeline.state, PipelineState::Interrupted); QCOMPARE(copied, 1); QCOMPARE(journal->list().entries.size(), size_t(1));
    CapturePipeline empty(CapturePipelineReceipt{}, nullptr, ports, journal); QSignalSpy emptyDone(&empty, &CapturePipeline::finished);
    empty.start(); QTRY_COMPARE(emptyDone.count(), 1); QVERIFY(!receipt(emptyDone)->pipeline.workingMediaRetained);
    QCOMPARE(receipt(emptyDone)->diagnostic, QString("retry-invalid-source")); QCOMPARE(copied, 1);
    QVERIFY(QFile::setPermissions(state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther));
    CapturePipeline retry(original, nullptr, ports, journal); QSignalSpy done(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QCOMPARE(copied, 1); QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Failed);
    QCOMPARE(*result->pipeline.orderedResults[0].diagnostic, QString("pipeline-journal-unavailable"));
    QCOMPARE(result->workingMedia, pixels()); QCOMPARE(result->file->creatorTaskId, original->pipeline.taskId); QCOMPARE(QImage(result->file->path), pixels());
    QVERIFY(QFile::setPermissions(state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  }
  void actualReceiptEmitsCanonicalResultsAndPrivateArtifactFactsWithoutPayloads() {
    QTemporaryDir dir; CaptureActionPorts ports;
    ports.clipboard = [](const QImage &, const CancellationToken &) -> ActionOutcome { throw std::runtime_error("private clipboard contents and secret"); };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"save", "image-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto retained = receipt(done); QVERIFY(retained->file);
    const auto report = retained->canonicalResult();
    QCOMPARE(report.value("pipeline_state").toString(), QString("completed"));
    QCOMPARE(report.value("ordered_results").toArray(), (QJsonArray{
        QJsonObject{{"action", "save"}, {"state", "succeeded"}},
        QJsonObject{{"action", "image-clipboard"}, {"state", "failed"}, {"diagnostic", "action-failed"}}}));
    const auto metadata = report.value("extensions").toObject().value("com.xerahs.app").toObject();
    QCOMPARE(metadata.value("task_id").toString(), retained->pipeline.taskId);
    const auto file = metadata.value("file_artifacts").toArray().first().toObject();
    QCOMPARE(file.value("artifact").toString(), QString("save.file"));
    QCOMPARE(file.value("path").toString(), retained->file->path);
    QCOMPARE(file.value("byte_length").toInteger(), QFileInfo(retained->file->path).size());
    QCOMPARE(file.value("filename_context").toObject(), retained->file->filenameContext);
    const auto encoded = QJsonDocument(report).toJson();
    QVERIFY(!encoded.contains("private clipboard contents")); QVERIFY(!encoded.contains("secret"));
    QVERIFY(!encoded.contains("base64")); QVERIFY(!encoded.contains("iVBOR"));
    QCOMPARE(retained->workingMedia, pixels()); QCOMPARE(QImage(retained->file->path), pixels());
  }
  void cancelDuringUninterruptibleNativeSavePreservesItsFileAndStopsClipboard() {
    QTemporaryDir dir; const auto media = pixels(); auto gate = std::make_shared<SaveGate>(); ReleaseGate release{gate}; int copied = 0;
    QGuiApplication::clipboard()->setText("fixture before save");
    CaptureActionPorts ports; ports.save.beforePublish = [gate](const QString &) { waitAtSave(gate); };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(media, dir.path(), "capture", {"save", "image-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(gate->entered.load()); QVERIFY(gate->onWorker.load());
    bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    pipeline.cancel(); QTest::qWait(20); QCOMPARE(done.count(), 0); QCOMPARE(copied, 0); gate->released.store(true);
    QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result); QVERIFY(result->file);
    QCOMPARE(result->pipeline.state, PipelineState::Cancelled); QCOMPARE(result->pipeline.orderedResults.size(), size_t(2));
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Succeeded); QVERIFY(result->pipeline.orderedResults[0].cancellationArrivedDuring);
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Cancelled); QCOMPARE(result->workingMedia, media);
    QCOMPARE(QImage(result->file->path), media); QCOMPARE(QGuiApplication::clipboard()->text(), QString("fixture before save"));
    pipeline.start(); pipeline.cancel(); QTest::qWait(20); QCOMPARE(done.count(), 1); QCOMPARE(copied, 0);
  }
  void saveFailureStillDispatchesTheIndependentClipboardOnTheGuiThread() {
    QTemporaryDir dir; const auto media = pixels(); int copied = 0; bool onGui = false;
    CaptureActionPorts ports; ports.save.encode = [](const QImage &, QIODevice *file) { file->write("partial"); return false; };
    ports.clipboard = [&](const QImage &image, const CancellationToken &) {
      ++copied; onGui = QThread::currentThread() == qGuiApp->thread(); QGuiApplication::clipboard()->setImage(image); return ActionOutcome{};
    };
    CapturePipeline pipeline(media, dir.path(), "capture", {"save", "image-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(!result->file); QCOMPARE(result->pipeline.state, PipelineState::Completed);
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Failed); QCOMPARE(*result->pipeline.orderedResults[0].diagnostic, QString("save-write-failed"));
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded); QCOMPARE(copied, 1); QVERIFY(onGui);
    QCOMPARE(QGuiApplication::clipboard()->image(), media); QCOMPARE(result->workingMedia, media);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
  }
  void clipboardExceptionKeepsCompletedSaveAndWorkingMediaWithRedactedFailure() {
    QTemporaryDir dir; CaptureActionPorts ports;
    ports.clipboard = [](const QImage &, const CancellationToken &) -> ActionOutcome { throw std::runtime_error("private clipboard content"); };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"save", "image-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(QImage(result->file->path), pixels()); QCOMPARE(result->workingMedia, pixels());
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Failed); QCOMPARE(*result->pipeline.orderedResults[1].diagnostic, QString("action-failed"));
  }
  void cancellingAtInitialAvailabilityStartsNoNativeAction() {
    QTemporaryDir dir; int started = 0; CaptureActionPorts ports; ports.actionStarted = [&](const QString &) { ++started; };
    CapturePipeline pipeline(pixels(), dir.filePath("outputs"), "capture", {"save", "image-clipboard"}, nullptr, ports);
    connect(&pipeline, &CapturePipeline::activityChanged, &pipeline, [&](const QString &text) { if (text == "Preparing capture actions.") pipeline.cancel(); });
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QCOMPARE(started, 0); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
    QCOMPARE(result->pipeline.state, PipelineState::Cancelled); QVERIFY(!result->file); QCOMPARE(result->workingMedia, pixels());
  }
  void cancelledUnclaimedClipboardRequestCannotMutateItsFixture() {
    QTemporaryDir dir; int copied = 0; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"save", "image-clipboard"}, nullptr, ports);
    connect(&pipeline, &CapturePipeline::activityChanged, &pipeline, [&](const QString &text) { if (text.startsWith("Copying")) pipeline.cancel(); });
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(copied, 0);
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Cancelled); QCOMPARE(QImage(result->file->path), pixels());
  }
  void destructionDoesNotJoinSavingAndCannotDispatchLateClipboard() {
    QTemporaryDir dir; auto gate = std::make_shared<SaveGate>(); ReleaseGate release{gate}; int copied = 0;
    auto lifetime = std::make_shared<int>(0); std::weak_ptr<int> workerLifetime(lifetime);
    CaptureActionPorts ports; ports.save.beforePublish = [gate, lifetime](const QString &) { waitAtSave(gate); };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    auto pipeline = std::make_unique<CapturePipeline>(pixels(), dir.path(), "capture", QStringList{"save", "image-clipboard"}, nullptr, ports);
    ports = {}; lifetime.reset(); pipeline->start(); QTRY_VERIFY(gate->entered.load());
    QElapsedTimer elapsed; elapsed.start(); pipeline.reset(); QVERIFY(elapsed.elapsed() < 500); QVERIFY(!workerLifetime.expired());
    gate->released.store(true); QTRY_VERIFY(workerLifetime.expired()); QCOMPARE(copied, 0); QCOMPARE(QImage(dir.filePath("capture.png")), pixels());
  }
  void cancellationInsideClaimedClipboardPreservesItsRealOutcome() {
    QTemporaryDir dir; CapturePipeline *running = nullptr; int copied = 0;
    CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &image, const CancellationToken &) {
      running->cancel(); QThread::msleep(30); // allow the worker to observe cancellation during the native call
      QGuiApplication::clipboard()->setImage(image); ++copied; return ActionOutcome{};
    };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"save", "image-clipboard"}, nullptr, ports); running = &pipeline;
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QCOMPARE(copied, 1); QCOMPARE(result->pipeline.state, PipelineState::Completed);
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded); QVERIFY(result->pipeline.orderedResults[1].cancellationArrivedDuring);
    QCOMPARE(QGuiApplication::clipboard()->image(), pixels());
  }
  void concurrentPipelinesKeepCancellationFilesMediaAndReceiptsIsolated() {
    QTemporaryDir dir; auto gate = std::make_shared<SaveGate>(); ReleaseGate release{gate};
    auto red = pixels(), blue = pixels(); red.fill(Qt::red); blue.fill(Qt::blue); int redCopies = 0, blueCopies = 0;
    CaptureActionPorts a, b; a.save.beforePublish = [gate](const QString &) { waitAtSave(gate); };
    a.clipboard = [&](const QImage &, const CancellationToken &) { ++redCopies; return ActionOutcome{}; };
    b.clipboard = [&](const QImage &image, const CancellationToken &) { ++blueCopies; QGuiApplication::clipboard()->setImage(image); return ActionOutcome{}; };
    QStringList selected{"save", "image-clipboard"};
    CapturePipeline first(red, dir.filePath("first"), "capture", selected, nullptr, a);
    CapturePipeline second(blue, dir.filePath("second"), "capture", selected, nullptr, b); selected.clear();
    QSignalSpy one(&first, &CapturePipeline::finished), two(&second, &CapturePipeline::finished);
    first.start(); QTRY_VERIFY(gate->entered.load()); second.start(); QTRY_COMPARE(two.count(), 1); QCOMPARE(one.count(), 0);
    first.cancel(); gate->released.store(true); QTRY_COMPARE(one.count(), 1);
    const auto stopped = receipt(one), completed = receipt(two);
    QCOMPARE(stopped->pipeline.state, PipelineState::Cancelled); QCOMPARE(completed->pipeline.state, PipelineState::Completed);
    QVERIFY(stopped->pipeline.taskId != completed->pipeline.taskId); QVERIFY(stopped->file->path != completed->file->path);
    QCOMPARE(QImage(stopped->file->path), red); QCOMPARE(QImage(completed->file->path), blue);
    QCOMPARE(stopped->workingMedia, red); QCOMPARE(completed->workingMedia, blue); QCOMPARE(redCopies, 0); QCOMPARE(blueCopies, 1);
  }
  void actualNativeClipboardPublishesPixelsAndReportsOwnershipHonestly() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "capture", {"image-clipboard"});
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    QCOMPARE(QGuiApplication::clipboard()->image(), pixels());
    const auto &action = receipt(done)->pipeline.orderedResults.front();
    if (QGuiApplication::clipboard()->ownsClipboard()) QCOMPARE(action.state, ActionState::Succeeded);
    else { QCOMPARE(action.state, ActionState::Failed); QCOMPARE(*action.diagnostic, QString("clipboard-unavailable")); }
  }
  void deletionAtInitialAvailabilityCannotLaunchAWorkerOrPublishOutput() {
    QTemporaryDir dir;
    auto pipeline = std::make_unique<CapturePipeline>(pixels(), dir.filePath("outputs"), "capture", QStringList{"save", "image-clipboard"});
    connect(pipeline.get(), &CapturePipeline::activityChanged, [&](const QString &) { pipeline.reset(); });
    pipeline->start(); QVERIFY(!pipeline); QTest::qWait(20); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
};
QTEST_MAIN(CapturePipelineTest)
#include "CapturePipelineTest.moc"
