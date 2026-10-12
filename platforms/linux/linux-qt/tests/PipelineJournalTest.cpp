#include "CapturePipeline.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <sys/stat.h>
#include <unistd.h>
using namespace xerahs::actions;
using namespace xerahs::app;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
PipelineJournalSnapshot observation(const QString &task) {
  PipelineJournalSnapshot snapshot; snapshot.result.taskId = task; snapshot.selected = {"save", "upload"};
  snapshot.createdAt = QDateTime::fromString("2026-10-13T01:02:03.004Z", Qt::ISODateWithMs);
  ActionResult saved; saved.action = "save"; saved.state = ActionState::Succeeded;
  saved.startedAt = snapshot.createdAt; saved.finishedAt = snapshot.createdAt.addMSecs(20);
  snapshot.result.orderedResults = {saved}; snapshot.files = {{"/fixture/capture.png", "image/png", 123, {}}};
  snapshot.pendingAction = "upload"; return snapshot;
}
QImage pixels() { QImage image(9, 7, QImage::Format_ARGB32); image.fill(Qt::red); return image; }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
void write(const QString &path, const QByteArray &data) { QFile file(path); if (file.open(QIODevice::WriteOnly)) { file.setPermissions(QFile::ReadOwner | QFile::WriteOwner); file.write(data); } }
}
class PipelineJournalTest final : public QObject {
  Q_OBJECT
private slots:
  void interruptedNetworkOutcomeIsUnknownRatherThanInventedFailure() {
    const auto task = id(); const auto snapshot = observation(task); const auto encoded = serializePipelineJournal(snapshot);
    const auto parsed = parsePipelineJournal(encoded, task); QVERIFY(parsed.record);
    const auto &record = *parsed.record; QCOMPARE(record.report.value("pipeline_state"), QJsonValue("interrupted"));
    QCOMPARE(record.report.value("working_media_retained"), QJsonValue(false)); QCOMPARE(record.pendingAction.value(), QString("upload"));
    QCOMPARE(record.report.value("ordered_results").toArray().size(), 1);
    const auto native = record.report.value("extensions").toObject().value("com.xerahs.app").toObject();
    QCOMPARE(native.value("recovery").toObject().value("unknown_action"), QJsonValue("upload"));
    QCOMPARE(native.value("file_artifacts").toArray().first().toObject().value("byte_length").toInteger(), qint64(123));
    QVERIFY(!encoded.contains("base64")); QVERIFY(!encoded.contains("clipboard"));
  }
  void malformedOrFutureRecordsFailClosed() {
    const auto task = id(); const auto original = QJsonDocument::fromJson(serializePipelineJournal(observation(task))).object();
    auto value = original; value.insert("version", 2);
    QCOMPARE(parsePipelineJournal(QJsonDocument(value).toJson(), task).diagnostic, QString("pipeline-journal-version-unsupported"));
    value = original; value.insert("terminal", true); QVERIFY(!parsePipelineJournal(QJsonDocument(value).toJson(), task).record);
    value = original; value.insert("pending_action", "save"); QVERIFY(!parsePipelineJournal(QJsonDocument(value).toJson(), task).record);
    value = original; value.insert("selected_actions", QJsonArray{"upload", "save"}); QVERIFY(!parsePipelineJournal(QJsonDocument(value).toJson(), task).record);
    QVERIFY(!parsePipelineJournal(serializePipelineJournal(observation(task)), id()).record);
    QVERIFY(!parsePipelineJournal(QByteArray(maximumJournalBytes + 1, 'x'), task).record);
    QVERIFY(!validJournalTaskId("../other"));
    QVERIFY(!validJournalTaskId(task + '\n'));
    value = original; value.insert("pending_action", ""); QVERIFY(!parsePipelineJournal(QJsonDocument(value).toJson(), task).record);
  }
  void privateAtomicStorageKeepsMultipleRecordsAndChecksExactSnapshots() {
    QTemporaryDir dir; const auto path = dir.filePath("state/pipelines"); NativePipelineJournal storage(path);
    QVERIFY(storage.list().entries.empty()); QVERIFY(!QFileInfo::exists(path));
    const auto one = id(), two = id(); const auto first = serializePipelineJournal(observation(one));
    QVERIFY(storage.replace(one, {}, first).stored); QVERIFY(storage.replace(two, {}, serializePipelineJournal(observation(two))).stored);
    struct stat info{}; QVERIFY(::stat(QFile::encodeName(path).constData(), &info) == 0); QCOMPARE(info.st_mode & 0777, mode_t(0700));
    QVERIFY(::stat(QFile::encodeName(path + '/' + one + ".json").constData(), &info) == 0); QCOMPARE(info.st_mode & 0777, mode_t(0600));
    auto next = observation(one); next.pendingAction.reset(); next.result.orderedResults.push_back(ActionResult{"upload", ActionState::Failed}); next.terminal = true;
    const auto final = serializePipelineJournal(next); QVERIFY(storage.replace(one, first, final).stored);
    QCOMPARE(storage.replace(one, first, final).diagnostic, QString("pipeline-journal-changed"));
    QCOMPARE(storage.erase(one, first).diagnostic, QString("pipeline-journal-changed"));
    QCOMPARE(storage.list().entries.size(), size_t(2)); QVERIFY(storage.erase(one, final).stored);
    QCOMPARE(storage.list().entries.size(), size_t(1)); QCOMPARE(storage.list().entries.front().taskId, two);
  }
  void unsafeFilesAndUnrecognizedVersionsArePreservedWithoutFollowingArtifacts() {
    QTemporaryDir dir; const auto path = dir.filePath("pipelines"); NativePipelineJournal storage(path);
    const auto task = id(); auto snapshot = observation(task); const auto artifact = dir.filePath("private-source"); write(artifact, "KEEP");
    snapshot.files[0].path = artifact; const auto encoded = serializePipelineJournal(snapshot); QVERIFY(storage.replace(task, {}, encoded).stored);
    QVERIFY(storage.erase(task, encoded).stored); QCOMPARE(bytes(artifact), QByteArray("KEEP"));
    QVERIFY(QFile::link(artifact, path + '/' + task + ".json"));
    QVERIFY(!storage.list().entries.front().parsed.record); QVERIFY(!storage.replace(task, {}, encoded).stored); QCOMPARE(bytes(artifact), QByteArray("KEEP"));
    QVERIFY(QFile::remove(path + '/' + task + ".json"));
    auto value = QJsonDocument::fromJson(encoded).object(); value.insert("version", 2); const auto future = QJsonDocument(value).toJson();
    write(path + '/' + task + ".json", future);
    QCOMPARE(storage.list().entries.front().parsed.diagnostic, QString("pipeline-journal-version-unsupported"));
    QVERIFY(!storage.erase(task, future).stored); QCOMPARE(bytes(path + '/' + task + ".json"), future);
    const auto fifo = id(); QVERIFY(::mkfifo(QFile::encodeName(path + '/' + fifo + ".json").constData(), 0600) == 0);
    QElapsedTimer timer; timer.start(); QCOMPARE(storage.list().entries.size(), size_t(2)); QVERIFY(timer.elapsed() < 1000);
  }
  void symlinkedAncestorsAndSharedDirectoriesAreRejectedWithoutChangingPermissions() {
    QTemporaryDir dir; const auto real = dir.filePath("real"); QVERIFY(QDir().mkdir(real));
    QVERIFY(QFile::link(real, dir.filePath("alias"))); NativePipelineJournal linked(dir.filePath("alias/pipelines"));
    const auto task = id(); QVERIFY(!linked.replace(task, {}, serializePipelineJournal(observation(task))).stored);
    QVERIFY(!QFileInfo::exists(real + "/pipelines"));
    QVERIFY(QFile::setPermissions(real, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther));
    NativePipelineJournal shared(real); QVERIFY(!shared.replace(task, {}, serializePipelineJournal(observation(task))).stored);
    QVERIFY(QFileInfo(real).permissions() & QFile::ReadOther);
  }
  void successfulNativeSaveAndClipboardFailureSurviveAReopenedStore() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureActionPorts ports; ports.clipboard = [](const QImage &, const CancellationToken &) { return ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}}; };
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt->file); QVERIFY(receipt->diagnostic.isEmpty());
    NativePipelineJournal reopened(dir.filePath("state")); const auto listing = reopened.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto &record = listing.entries.front().parsed.record; QVERIFY(record); QVERIFY(record->terminal);
    QCOMPARE(record->taskId, receipt->pipeline.taskId); QCOMPARE(record->report.value("pipeline_state"), QJsonValue("completed"));
    QCOMPARE(record->report.value("ordered_results"), receipt->canonicalResult().value("ordered_results"));
    QCOMPARE(QImage(receipt->file->path), pixels());
  }
  void journalCapacityFailsWithoutPruningOrReplacingExistingResults() {
    QTemporaryDir dir; const auto state = dir.filePath("state"); NativePipelineJournal storage(state);
    const auto task = id(); const auto first = serializePipelineJournal(observation(task)); QVERIFY(storage.replace(task, {}, first).stored);
    for (int count = 1; count < 1024; ++count) write(state + '/' + id() + ".json", "preserve corrupt evidence");
    const auto other = id(); QCOMPARE(storage.replace(other, {}, serializePipelineJournal(observation(other))).diagnostic, QString("pipeline-journal-capacity"));
    QCOMPARE(bytes(state + '/' + task + ".json"), first); QCOMPARE(QDir(state).entryList(QDir::Files | QDir::Hidden).size(), 1024);
    QVERIFY(storage.replace(task, first, first).stored);
  }
  void cancellationKeepsTheCompletedSaveAndDurableRemainingActionStates() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); CapturePipeline *running = nullptr; int copied = 0;
    CaptureActionPorts ports; ports.save.beforePublish = [&](const QString &) {
      QMetaObject::invokeMethod(running, [running] { running->cancel(); }, Qt::BlockingQueuedConnection);
    };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal); running = &pipeline;
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt->file); QCOMPARE(copied, 0);
    const auto listing = journal->list(); const auto &record = listing.entries.front().parsed.record;
    QVERIFY(record); QVERIFY(record->terminal); QCOMPARE(record->report.value("pipeline_state"), QJsonValue("cancelled"));
    QCOMPARE(QImage(receipt->file->path), pixels());
  }
  void initialJournalFailureDoesNotLosePixelsOrPreventIndependentClipboard() {
    QTemporaryDir dir; write(dir.filePath("blocked"), "KEEP"); auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("blocked/pipelines")); int copied = 0;
    QImage copiedMedia;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &image, const CancellationToken &) { ++copied; copiedMedia = image; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = qvariant_cast<CapturePipelineReceipt>(done.first().first());
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Failed); QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded);
    QCOMPARE(copied, 1); QCOMPARE(copiedMedia, pixels()); QVERIFY(!result->file); QCOMPARE(result->workingMedia, pixels()); QVERIFY(!QFileInfo::exists(dir.filePath("output")));
    QCOMPARE(bytes(dir.filePath("blocked")), QByteArray("KEEP"));
  }
  void journalFailureBeforePublicationPreventsSaveButStillAllowsIndependentClipboard() {
    QTemporaryDir dir; const auto state = dir.filePath("state"); auto journal = std::make_shared<NativePipelineJournal>(state); int copied = 0;
    CaptureActionPorts ports;
    ports.save.encode = [state](const QImage &image, QIODevice *output) {
      const bool saved = image.save(output, "PNG");
      QFile::setPermissions(state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther); return saved;
    };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = qvariant_cast<CapturePipelineReceipt>(done.first().first());
    QVERIFY(!result->file); QCOMPARE(copied, 1); QCOMPARE(result->workingMedia, pixels());
    QCOMPARE(QDir(dir.filePath("output")).entryList(QDir::Files | QDir::Hidden).size(), 0);
    QVERIFY(QFile::setPermissions(state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto recovered = journal->list(); QVERIFY(recovered.entries.front().parsed.record); QVERIFY(!recovered.entries.front().parsed.record->terminal);
  }
  void lostJournalWriteAfterSavePreservesTrueReceiptAndUnknownRecoveryWithoutCopying() {
    QTemporaryDir dir; const auto state = dir.filePath("state"); auto journal = std::make_shared<NativePipelineJournal>(state); int copied = 0;
    CaptureActionPorts ports;
    ports.save.beforePublish = [state](const QString &) { QFile::setPermissions(state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther); };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(result->file);
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Succeeded); QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Failed);
    QCOMPARE(copied, 0); QCOMPARE(QImage(result->file->path), pixels()); QCOMPARE(result->workingMedia, pixels());
    QVERIFY(QFile::setPermissions(state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto recovered = journal->list(); const auto &record = recovered.entries.front().parsed.record; QVERIFY(record);
    QCOMPARE(record->pendingAction.value(), QString("save")); QCOMPARE(record->pendingSavePath.value(), result->file->path);
    QCOMPARE(record->report.value("pipeline_state"), QJsonValue("interrupted"));
    QVERIFY(journal->erase(recovered.entries.front().taskId, recovered.entries.front().bytes).stored);
    QCOMPARE(QImage(result->file->path), pixels());
  }
  void killedNativeWorkerRecoversUnknownPublicationOrClipboard_data() {
    QTest::addColumn<QString>("mode"); QTest::newRow("before-publication") << QString("before-publication"); QTest::newRow("after-save") << QString("after-save");
  }
  void killedNativeWorkerRecoversUnknownPublicationOrClipboard() {
    QFETCH(QString, mode); QTemporaryDir dir; QProcess process;
    process.start(QCoreApplication::applicationDirPath() + "/pipeline-journal-fixture", {dir.filePath("state"), dir.filePath("output"), mode});
    QVERIFY(process.waitForStarted(3000)); QTRY_VERIFY_WITH_TIMEOUT(process.canReadLine() || process.state() == QProcess::NotRunning, 5000);
    QCOMPARE(process.readLine().trimmed(), QByteArray("checkpoint")); process.kill(); QVERIFY(process.waitForFinished(3000));
    NativePipelineJournal reopened(dir.filePath("state")); const auto listing = reopened.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto &record = listing.entries.front().parsed.record; QVERIFY(record); QVERIFY(!record->terminal);
    QCOMPARE(record->report.value("pipeline_state"), QJsonValue("interrupted"));
    QCOMPARE(record->pendingAction.value(), mode == "before-publication" ? QString("save") : QString("image-clipboard"));
    const auto ordered = record->report.value("ordered_results").toArray();
    if (mode == "before-publication") {
      QCOMPARE(ordered.size(), 0); QCOMPARE(record->pendingSavePath.value(), dir.filePath("output/capture.png"));
      QVERIFY(!QFileInfo::exists(dir.filePath("output/capture.png")));
    } else {
      QCOMPARE(ordered.size(), 1); QCOMPARE(ordered.first().toObject().value("state"), QJsonValue("succeeded"));
      QCOMPARE(QImage(dir.filePath("output/capture.png")), pixels());
    }
    const auto journalBytes = listing.entries.front().bytes;
    QTest::qWait(20); QCOMPARE(reopened.list().entries.front().bytes, journalBytes);
    // Recovery leaves even a crash-owned encoding temporary for the explicit
    // cleanup policy; no arbitrary directory scan deletes possible user data.
  }
};
QTEST_MAIN(PipelineJournalTest)
#include "PipelineJournalTest.moc"
