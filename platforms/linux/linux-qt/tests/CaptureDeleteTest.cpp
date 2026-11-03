#include "CapturePipeline.h"
#include "PipelineHistoryDialog.h"
#include "image-editor/EditorSession.h"
#include "uploads/DestinationCatalog.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
using namespace xerahs::app;
using namespace xerahs::actions;
using namespace xerahs::uploads;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::green); return image; }
bool write(const QString &path, const QByteArray &bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
const ActionResult &action(const CapturePipelineReceipt &receipt, const QString &id) {
  for (const auto &result : receipt->pipeline.orderedResults) if (result.action == id) return result;
  Q_UNREACHABLE();
}
class Http : public QObject {
public:
  QTcpServer server; bool received = false; int status = 200;
  Http() {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      auto *peer = server.nextPendingConnection();
      connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
      connect(peer, &QTcpSocket::readyRead, peer, [this, peer] {
        if (received) { peer->readAll(); return; }
        peer->readAll(); received = true;
        const QByteArray body = status == 200 ? QByteArray("{\"url\":\"https://example.test/image\"}") : QByteArray("failed");
        peer->write("HTTP/1.1 " + QByteArray::number(status) + " Reply\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        peer->disconnectFromHost();
      });
    });
  }
};
}
class CaptureDeleteTest : public QObject {
  Q_OBJECT
private slots:
  void pipeline_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"saved", "save-as", "saved-precedence", "upload", "failed-upload", "failed-consumer", "no-save", "cancel", "cancel-during", "no-journal"})
      QTest::newRow(mode) << QString(mode);
  }
  void pipeline() {
    QFETCH(QString, mode); QTemporaryDir dir; Http http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    http.status = mode == "failed-upload" ? 500 : 200;
    const auto output = dir.filePath("captures"); QDir().mkpath(output);
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    CaptureActionPorts ports; ports.uploadCatalogPath = dir.filePath("destinations.json"); ports.uploadCredentials = std::make_shared<MemoryCredentialStore>();
    UploadDestination destination; destination.id = QUuid::createUuid(); destination.name = "Fixture";
    destination.requestUrl = QString("http://127.0.0.1:%1/upload").arg(http.server.serverPort()); destination.resultUrl = "{json:url}";
    DestinationCatalog catalog(ports.uploadCatalogPath); QVERIFY(catalog.add(destination).isEmpty());
    ports.chooseSaveAs = [output](const QString &, const CancellationToken &) { return CaptureSaveAsChoice{output + "/chosen.png", {}}; };
    ports.fileOutput = [mode](const QString &, const CaptureFile &, const CancellationToken &) {
      return ActionOutcome{mode == "failed-consumer" ? ActionState::Failed : ActionState::Succeeded, {}, {}};
    };
    QStringList selected{"save", "delete"};
    if (mode == "save-as") selected = {"save-as", "delete"};
    if (mode == "saved-precedence") selected = {"save", "save-as", "delete"};
    if (mode == "upload" || mode == "failed-upload") selected = {"save", "upload", "delete"};
    if (mode == "failed-consumer") selected = {"save", "file-clipboard", "delete"};
    if (mode == "no-save") selected = {"delete"};
    CapturePipeline *live = nullptr;
    if (mode == "cancel") ports.actionStarted = [&live](const QString &id) {
      if (id == "delete") QMetaObject::invokeMethod(live, [&live] { live->cancel(); }, Qt::BlockingQueuedConnection);
    };
    if (mode == "cancel-during") ports.deletion.afterMove = [&live](const QString &) {
      QMetaObject::invokeMethod(live, [&live] { live->cancel(); }, Qt::BlockingQueuedConnection);
    };
    CapturePipeline pipeline(pixels(), output, "capture", selected, nullptr, ports, mode == "no-journal" ? PipelineJournalStorage{} : journal); live = &pipeline;
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt);
    const auto &removed = action(receipt, "delete");
    const bool success = mode == "saved" || mode == "save-as" || mode == "saved-precedence" || mode == "upload" || mode == "cancel-during";
    QCOMPARE(removed.state, success ? ActionState::Succeeded : mode == "no-journal" ? ActionState::Failed : mode == "cancel" ? ActionState::Cancelled : ActionState::Skipped);
    if (mode == "failed-upload" || mode == "failed-consumer") QCOMPARE(removed.diagnostic, std::optional<QString>("delete-guard"));
    if (mode == "no-save") QCOMPARE(removed.diagnostic, std::optional<QString>("dependency-unavailable"));
    if (receipt->file) QCOMPARE(QFileInfo::exists(receipt->file->path), !success);
    if (success) {
      QCOMPARE(removed.inputArtifact, std::optional<QString>(mode == "save-as" ? "save-as.file" : "save.file"));
      const auto records = journal->list(); QCOMPARE(records.entries.size(), size_t(1)); QVERIFY(records.entries.front().parsed.record);
      const auto recovery = records.entries.front().parsed.record->deletionRecovery; QVERIFY(recovery); QCOMPARE(recovery->state, QString("removed"));
      QVERIFY(!QFileInfo::exists(recovery->stagingPath));
      QVERIFY(!receipt->file->identity); QCOMPARE(validateCaptureFile(*receipt->file), QString("saved-file-unavailable"));
    }
    if (mode == "saved-precedence") QVERIFY(QFileInfo::exists(output + "/chosen.png"));
    if (mode == "upload" || mode == "failed-upload") QVERIFY(http.received);
    if (mode == "cancel-during") QVERIFY(removed.cancellationArrivedDuring);
    QCOMPARE(receipt->workingMedia, pixels());
  }
  void changedEntry_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"modified", "symlink", "hardlink", "replacement", "restore-conflict", "ancestor", "shared-ancestor", "unlink-failure", "lost-ack"}) QTest::newRow(mode) << QString(mode);
  }
  void changedEntry() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto output = dir.filePath("captures");
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    CaptureActionPorts ports; const auto original = output + "/capture.png"; const auto foreign = dir.filePath("foreign"); QVERIFY(write(foreign, "foreign-secret"));
    if (mode == "modified") ports.actionStarted = [original](const QString &id) { if (id == "delete") { QFile file(original); if (file.open(QIODevice::Append)) file.write("changed"); } };
    if (mode == "ancestor") ports.actionStarted = [output, dirPath = dir.path()](const QString &id) {
      if (id == "delete") { QDir().rename(output, dirPath + "/kept"); ::symlink(QFile::encodeName(dirPath).constData(), QFile::encodeName(output).constData()); }
    };
    if (mode == "shared-ancestor") ports.actionStarted = [ancestor = dir.path()](const QString &id) {
      if (id == "delete") ::chmod(QFile::encodeName(ancestor).constData(), 0777);
    };
    if (mode == "replacement" || mode == "restore-conflict" || mode == "symlink") ports.deletion.beforeMove = [original, foreign, mode](const QString &) {
      QFile::rename(original, original + ".kept");
      if (mode == "symlink") ::symlink(QFile::encodeName(foreign).constData(), QFile::encodeName(original).constData());
      else write(original, "displaced-foreign-secret");
    };
    if (mode == "hardlink") ports.deletion.beforeMove = [original](const QString &) { ::link(QFile::encodeName(original).constData(), QFile::encodeName(original + ".link").constData()); };
    if (mode == "restore-conflict") ports.deletion.afterMove = [original](const QString &) { write(original, "replacement-at-original-name"); };
    if (mode == "unlink-failure") {
      // Read/execute permit inspecting the staged inode, but the actual
      // unlinkat fails without directory write permission. Restore then uses
      // the real rename after restoring the private directory's permissions.
      ports.deletion.afterMove = [](const QString &staged) { ::chmod(QFile::encodeName(QFileInfo(staged).absolutePath()).constData(), 0500); };
      ports.deletion.beforeRestore = [](const QString &staged) { ::chmod(QFile::encodeName(QFileInfo(staged).absolutePath()).constData(), 0700); };
    }
    if (mode == "lost-ack") ports.deletion.observe = [](const DeletionRecovery &recovery) { return recovery.state != "removed"; };
    CapturePipeline pipeline(pixels(), output, "capture", {"save", "delete"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt);
    if (mode == "shared-ancestor") ::chmod(QFile::encodeName(dir.path()).constData(), 0700);
    QCOMPARE(read(foreign), QByteArray("foreign-secret"));
    if (mode == "lost-ack") { QCOMPARE(receipt->pipeline.state, PipelineState::Interrupted); QCOMPARE(receipt->pipeline.orderedResults.size(), size_t(1)); }
    else QCOMPARE(action(receipt, "delete").state, ActionState::Failed);
    const auto records = journal->list(); QCOMPARE(records.entries.size(), size_t(1)); QVERIFY(records.entries.front().parsed.record);
    if (mode == "restore-conflict") {
      const auto recovery = records.entries.front().parsed.record->deletionRecovery; QVERIFY(recovery); QCOMPARE(recovery->state, QString("retained"));
      struct stat privateDirectory{}; QVERIFY(::stat(QFile::encodeName(QFileInfo(recovery->stagingPath).absolutePath()).constData(), &privateDirectory) == 0);
      QCOMPARE(privateDirectory.st_uid, ::geteuid()); QCOMPARE(privateDirectory.st_mode & 0777, mode_t(0700));
      const auto canonical = QJsonDocument(receipt->canonicalResult()).toJson();
      QVERIFY(!canonical.contains(recovery->stagingPath.toUtf8())); QVERIFY(!canonical.contains("deletion_recovery")); QVERIFY(!canonical.contains("displaced-foreign-secret"));
      QCOMPARE(read(recovery->stagingPath), QByteArray("displaced-foreign-secret")); QCOMPARE(read(original), QByteArray("replacement-at-original-name"));
      QVERIFY(QFileInfo::exists(original + ".kept"));
      QPointer<PipelineHistoryDialog> history = new PipelineHistoryDialog(std::make_shared<NativePipelineJournal>(dir.filePath("journal")), [] { return false; }); history->show();
      auto *table = history->findChild<QTreeWidget *>("captureHistoryRecords"); QVERIFY(table); QTRY_COMPARE(table->topLevelItemCount(), 1);
      auto *forget = history->findChild<QPushButton *>("forgetCaptureHistory"); QVERIFY(forget); QVERIFY(!forget->isEnabled());
      history->findChild<QPushButton *>("reviewCaptureHistory")->click();
      auto *details = history->findChild<QLabel *>("captureDeletionRecovery"); QVERIFY(details); QVERIFY(details->text().contains(recovery->stagingPath));
      QCOMPARE(details->textFormat(), Qt::PlainText); QVERIFY(details->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
      history->close(); QTRY_VERIFY(history.isNull()); QCOMPARE(read(recovery->stagingPath), QByteArray("displaced-foreign-secret"));
    } else if (mode == "replacement") QCOMPARE(read(original), QByteArray("displaced-foreign-secret"));
    else if (mode == "symlink") QVERIFY(QFileInfo(original).isSymLink());
    else if (mode == "ancestor") QVERIFY(QFileInfo::exists(dir.filePath("kept/capture.png")));
    else if (mode != "lost-ack") QVERIFY(QFileInfo::exists(original));
    if (mode == "unlink-failure") {
      QCOMPARE(QImage(original), pixels()); QVERIFY(receipt->file); QVERIFY(receipt->file->identity);
      QVERIFY(validateCaptureFile(*receipt->file).isEmpty());
      const auto recovery = records.entries.front().parsed.record->deletionRecovery; QVERIFY(recovery); QCOMPARE(recovery->state, QString("restored"));
      QVERIFY(!QFileInfo::exists(recovery->stagingPath));
    }
  }
  void recoveryWireVersionPreservesOlderHistory() {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    PipelineJournalSnapshot snapshot; snapshot.result.taskId = id; snapshot.createdAt = QDateTime::currentDateTimeUtc();
    snapshot.selected = {"delete"}; snapshot.pendingAction = "delete";
    auto ordinary = serializePipelineJournal(snapshot); QCOMPARE(QJsonDocument::fromJson(ordinary).object().value("version"), QJsonValue(1));
    QVERIFY(parsePipelineJournal(ordinary, id).record);
    snapshot.deletionRecovery = DeletionRecovery{"/private/original.png", "/private/stage/entry", "pending"};
    const auto current = serializePipelineJournal(snapshot); auto object = QJsonDocument::fromJson(current).object();
    QCOMPARE(object.value("version"), QJsonValue(2)); QVERIFY(parsePipelineJournal(current, id).record);
    object.insert("version", 1); QVERIFY(!parsePipelineJournal(QJsonDocument(object).toJson(), id).record);
    object.insert("version", 4); QCOMPARE(parsePipelineJournal(QJsonDocument(object).toJson(), id).diagnostic, QString("pipeline-journal-version-unsupported"));
  }
  void ownerDestructionBeforeMovePreservesOriginal() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    std::atomic<bool> completed{false}; std::unique_ptr<CapturePipeline> owner; CaptureActionPorts ports;
    ports.deletion.beforeMove = [&](const QString &) {
      QMetaObject::invokeMethod(qApp, [&] { owner.reset(); }, Qt::BlockingQueuedConnection);
    };
    ports.deletion.observe = [&](const DeletionRecovery &recovery) { if (recovery.state == "restored") completed = true; return true; };
    owner = std::make_unique<CapturePipeline>(pixels(), dir.filePath("captures"), "capture", QStringList{"save", "delete"}, nullptr, ports, journal);
    owner->start(); QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 5000); QVERIFY(!owner);
    QCOMPARE(QImage(dir.filePath("captures/capture.png")), pixels());
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      const auto records = journal->list(); return records.entries.size() == 1 && records.entries.front().parsed.record && records.entries.front().parsed.record->terminal;
    })(), 5000);
  }
  void pairedSaveKeepsEditableCompanion() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal")); CaptureActionPorts ports;
    ports.annotation = [](const LoadedSource &source, const CancellationToken &) {
      xerahs::editor::EditorSession session(*source.document); session.createRectangle({1, 1}, {8, 6});
      return CaptureAnnotationChoice{session.document(), {}, true};
    };
    CapturePipeline pipeline(pixels(), dir.filePath("captures"), "capture", {"annotation", "save", "delete"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QCOMPARE(action(receipt, "delete").state, ActionState::Succeeded);
    bool companion = false;
    for (const auto &file : receipt->savedFiles()) {
      if (file.artifact == "save.file") QVERIFY(!QFileInfo::exists(file.path));
      if (file.artifact == "save.sidecar") { companion = true; QVERIFY(QFileInfo::exists(file.path)); QVERIFY(validateCaptureFile(file).isEmpty()); }
    }
    QVERIFY(companion); QVERIFY(receipt->annotationDocument);
  }
  void recoveryMetadataNeverAuthorizesAnArbitraryPath() {
    QTemporaryDir dir; const auto foreign = dir.filePath("not-a-capture"); QVERIFY(write(foreign, "foreign-secret"));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces); PipelineJournalSnapshot snapshot;
    snapshot.result.taskId = id; snapshot.createdAt = QDateTime::currentDateTimeUtc(); snapshot.selected = {"delete"}; snapshot.pendingAction = "delete";
    snapshot.deletionRecovery = DeletionRecovery{dir.filePath("unconfirmed-original"), foreign, "pending"};
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal")); QVERIFY(journal->replace(id, {}, serializePipelineJournal(snapshot)).stored);
    QPointer<PipelineHistoryDialog> history = new PipelineHistoryDialog(journal, [] { return false; }); history->show();
    auto *table = history->findChild<QTreeWidget *>("captureHistoryRecords"); QTRY_COMPARE(table->topLevelItemCount(), 1);
    QVERIFY(!history->findChild<QPushButton *>("forgetCaptureHistory")->isEnabled());
    history->findChild<QPushButton *>("reviewCaptureHistory")->click();
    auto *label = history->findChild<QLabel *>("captureDeletionRecovery"); QVERIFY(label); QVERIFY(label->text().contains(foreign));
    history->close(); QTRY_VERIFY(history.isNull()); QCOMPARE(read(foreign), QByteArray("foreign-secret"));
  }
  void inheritedFileNeverGrantsDelete() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal")); CaptureActionPorts ports;
    ports.fileOutput = [](const QString &, const CaptureFile &, const CancellationToken &) { return ActionOutcome{ActionState::Failed, "fixture-failed", {}}; };
    CapturePipeline original(pixels(), dir.filePath("captures"), "capture", {"save", "file-clipboard", "delete"}, nullptr, ports, journal);
    QSignalSpy first(&original, &CapturePipeline::finished); original.start(); QTRY_COMPARE(first.count(), 1);
    const auto source = qvariant_cast<CapturePipelineReceipt>(first.first().first()); QVERIFY(source->file); QVERIFY(QFileInfo::exists(source->file->path));
    ports.fileOutput = [](const QString &, const CaptureFile &, const CancellationToken &) { return ActionOutcome{}; };
    CapturePipeline retry(source, nullptr, ports, journal); QSignalSpy second(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(second.count(), 1);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(second.first().first());
    // A guarded deletion is not a dependency-skipped retry candidate at all.
    QVERIFY(std::none_of(receipt->pipeline.orderedResults.begin(), receipt->pipeline.orderedResults.end(), [](const ActionResult &entry) { return entry.action == "delete"; }));
    QCOMPARE(action(source, "delete").state, ActionState::Skipped); QCOMPARE(action(source, "delete").diagnostic, std::optional<QString>("delete-guard"));
    CaptureActions inherited(pixels(), dir.path(), "unused", {}, source->file);
    PipelineOptions options; options.inheritedSaveAvailable = true; CancellationToken token;
    const auto guarded = runPipeline(QUuid::createUuid().toString(QUuid::WithoutBraces), {"delete"}, options, inherited, token);
    QCOMPARE(guarded.orderedResults.front().state, ActionState::Skipped); QCOMPARE(guarded.orderedResults.front().diagnostic, std::optional<QString>("delete-guard"));
    QVERIFY(QFileInfo::exists(source->file->path));
  }
  void interruptedStaging_data() { QTest::addColumn<QString>("mode"); QTest::newRow("before") << QString("before-move"); QTest::newRow("after") << QString("after-move"); }
  void interruptedStaging() {
    QFETCH(QString, mode); QTemporaryDir dir; QProcess child;
    child.start(QStringLiteral(CAPTURE_DELETE_FIXTURE), {dir.filePath("journal"), dir.filePath("captures"), mode}); QVERIFY(child.waitForStarted());
    QByteArray output; QElapsedTimer timer; timer.start();
    while (!output.contains("checkpoint") && timer.elapsed() < 8000) { child.waitForReadyRead(100); output += child.readAllStandardOutput(); }
    QVERIFY2(output.contains("checkpoint"), child.readAllStandardError().constData()); child.kill(); QVERIFY(child.waitForFinished());
    NativePipelineJournal reopened(dir.filePath("journal")); const auto records = reopened.list(); QCOMPARE(records.entries.size(), size_t(1)); QVERIFY(records.entries.front().parsed.record);
    const auto &record = *records.entries.front().parsed.record; QCOMPARE(record.report.value("pipeline_state").toString(), QString("interrupted")); QCOMPARE(record.pendingAction, std::optional<QString>("delete"));
    QVERIFY(record.deletionRecovery); QCOMPARE(record.deletionRecovery->state, QString("pending"));
    const auto retained = mode == "before-move" ? record.deletionRecovery->originalPath : record.deletionRecovery->stagingPath;
    QCOMPARE(QImage(retained), pixels());
    QPointer<PipelineHistoryDialog> history = new PipelineHistoryDialog(std::make_shared<NativePipelineJournal>(dir.filePath("journal")), [] { return false; }); history->show();
    auto *table = history->findChild<QTreeWidget *>("captureHistoryRecords"); QTRY_COMPARE(table->topLevelItemCount(), 1);
    history->findChild<QPushButton *>("reviewCaptureHistory")->click(); auto *label = history->findChild<QLabel *>("captureDeletionRecovery"); QVERIFY(label); QVERIFY(label->text().contains(retained));
    history->close(); QTRY_VERIFY(history.isNull()); QCOMPARE(QImage(retained), pixels());
  }
};
QTEST_MAIN(CaptureDeleteTest)
#include "CaptureDeleteTest.moc"
