#include "CapturePipeline.h"
#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "PipelineResultDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <sys/stat.h>
#include <unistd.h>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(80, 60, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
CaptureAnnotationChoice rectangle(const LoadedSource &source) {
  xerahs::editor::EditorSession session(*source.document); session.createRectangle({20, 20}, {40, 40}); return {session.document(), {}, true};
}
QJsonObject native(const CapturePipelineReceipt &result) { return result->canonicalResult().value("extensions").toObject().value("com.xerahs.app").toObject(); }
}
class CapturePairedSaveTest final : public QObject {
  Q_OBJECT
private slots:
  void failedRasterNeverWritesCompanionOrAcknowledgesAFilePrerequisite() {
    QTemporaryDir dir; CaptureActionPorts ports; int copies = 0, fileOutputs = 0;
    ports.annotation = [](const LoadedSource &source, const CancellationToken &) { return rectangle(source); };
    ports.save.encode = [](const QImage &, QIODevice *output) { output->write("partial"); return false; };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return ActionOutcome{}; };
    ports.fileOutput = [&](const QString &, const CaptureFile &, const CancellationToken &) { ++fileOutputs; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "failed", {"annotation", "save", "image-clipboard", "path-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done);
    QVERIFY(result->files.empty()); QVERIFY(result->annotationDocument); const auto &save = result->pipeline.orderedResults[1];
    QCOMPARE(save.state, ActionState::Failed); QCOMPARE(save.artifacts.size(), size_t(2)); QCOMPARE(save.artifacts[0].state, ActionState::Failed);
    QCOMPARE(save.artifacts[1].state, ActionState::Skipped); QCOMPARE(copies, 1); QCOMPARE(fileOutputs, 0);
    QCOMPARE(result->pipeline.orderedResults.back().diagnostic, std::optional<QString>{"dependency-unavailable"});
    QCOMPARE(result->retryPlan().selected, (QStringList{"save", "path-clipboard"})); QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
  }
  void actualCanvasExplicitIntentSavesBothCompatiblePairs() {
    QTemporaryDir dir; CaptureActionPorts ports;
    ports.chooseSaveAs = [&](const QString &, const CancellationToken &) { return CaptureSaveAsChoice{dir.filePath("chosen.png"), {}}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"annotation", "save", "save-as"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start();
    QTRY_VERIFY(qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget()));
    auto *dialog = qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget());
    auto *intent = dialog->findChild<QCheckBox *>("captureSaveAnnotationSidecar"); QVERIFY(intent); QVERIFY(!intent->isChecked());
    intent->setFocus(); QTRY_VERIFY(intent->hasFocus()); QTest::keyClick(intent, Qt::Key_Space); QVERIFY(intent->isChecked());
    auto *canvas = dialog->findChild<EditorCanvas *>(); QVERIFY(canvas);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({20, 20}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({40, 40}).toPoint()); dialog->accept();
    QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->sidecarIntent); QCOMPARE(result->files.size(), size_t(4));
    for (const auto &stem : {QString("capture"), QString("chosen")}) {
      const auto rasterPath = dir.filePath(stem + ".png"); const auto sidecarPath = rasterPath + ".xann";
      QCOMPARE(QImage(rasterPath), result->workingMedia); const auto parsed = xerahs::editor::readXann(bytes(sidecarPath)); QVERIFY(parsed.document);
      QCOMPARE(parsed.document->imagePath, rasterPath); QCOMPARE(parsed.document->annotations.size(), size_t(1));
      QCOMPARE(QImage::fromData(parsed.document->sourceImagePng, "PNG"), pixels());
      QCOMPARE(parsed.document->imageHash, QString("sha256:") + QString::fromLatin1(QCryptographicHash::hash(bytes(rasterPath), QCryptographicHash::Sha256).toHex()));
      const auto loaded = loadSource(rasterPath); QVERIFY(loaded.document); QCOMPARE(loaded.document->annotations.size(), size_t(1));
      QCOMPARE(loaded.image, pixels()); QVERIFY(!loaded.pendingChoice);
    }
    for (const auto &file : result->files) {
      QCOMPARE(file.creatorTaskId, result->pipeline.taskId); QVERIFY(validateCaptureFile(file).isEmpty());
      struct stat info{}; QVERIFY(::stat(QFile::encodeName(file.path).constData(), &info) == 0); QCOMPARE(info.st_mode & 0777, mode_t(0600));
    }
    const auto records = journal->list(); QCOMPARE(records.entries.size(), size_t(1)); QVERIFY(records.entries[0].parsed.record);
    QCOMPARE(native(result).value("artifact_outcomes").toArray().size(), 4);
    auto *review = makePipelineResultDialog(result->canonicalResult(), nullptr);
    auto *table = review->findChild<QTreeWidget *>("captureArtifactResults"); QVERIFY(table); QCOMPARE(table->topLevelItemCount(), 4); delete review;
  }
  void partialFailureRetriesOnlyTheCompanionWithoutRepeatingConsumers_data() {
    QTest::addColumn<QString>("action"); QTest::newRow("Save") << QString("save"); QTest::newRow("Save As") << QString("save-as");
  }
  void partialFailureRetriesOnlyTheCompanionWithoutRepeatingConsumers() {
    QFETCH(QString, action); QTemporaryDir dir; const auto rasterPath = dir.filePath("capture.png"), sidecarPath = rasterPath + ".xann";
    QVERIFY(QDir().mkdir(sidecarPath)); int encodes = 0, pickers = 0, annotations = 0, consumers = 0; bool consumerPathCorrect = true;
    CaptureActionPorts ports; ports.annotation = [&](const LoadedSource &source, const CancellationToken &) { ++annotations; return rectangle(source); };
    ports.chooseSaveAs = [&](const QString &, const CancellationToken &) { ++pickers; return CaptureSaveAsChoice{rasterPath, {}}; };
    ports.save.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); };
    ports.fileOutput = [&](const QString &, const CaptureFile &file, const CancellationToken &) {
      ++consumers; consumerPathCorrect &= file.path == rasterPath; return ActionOutcome{};
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline first(pixels(), dir.path(), "capture", {"annotation", action, "path-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&first, &CapturePipeline::finished); first.start(); QTRY_COMPARE(done.count(), 1); const auto source = receipt(done);
    QCOMPARE(source->pipeline.orderedResults[1].state, ActionState::Failed); QCOMPARE(source->pipeline.orderedResults[1].diagnostic, std::optional<QString>{"sidecar-save-failed"});
    QCOMPARE(source->pipeline.orderedResults[2].state, ActionState::Succeeded); QVERIFY(source->annotationDocument && source->sidecarIntent);
    QCOMPARE(source->files.size(), size_t(1)); const auto original = bytes(rasterPath); const auto identity = source->file->identity;
    QCOMPARE(source->retryPlan().selected, QStringList{action}); QCOMPARE(source->retryPlan().link->preservedRasterActions, QStringList{action});
    QVERIFY(!source->retryPlan().link->preservedSuccessfulActions.contains(action));
    CapturePipeline second(source, nullptr, ports, journal); QSignalSpy again(&second, &CapturePipeline::finished); second.start(); QTRY_COMPARE(again.count(), 1); const auto failedAgain = receipt(again);
    QCOMPARE(failedAgain->pipeline.orderedResults[0].state, ActionState::Failed); QVERIFY(failedAgain->retryPlan().available()); QCOMPARE(bytes(rasterPath), original);
    QVERIFY(QDir().rmdir(sidecarPath));
    CapturePipeline third(failedAgain, nullptr, ports, journal); QSignalSpy final(&third, &CapturePipeline::finished); third.start(); QTRY_COMPARE(final.count(), 1); const auto repaired = receipt(final);
    QCOMPARE(repaired->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(repaired->pipeline.orderedResults[0].state, ActionState::Succeeded);
    QCOMPARE(encodes, 1); QCOMPARE(annotations, 1); QCOMPARE(pickers, action == "save-as" ? 1 : 0); QCOMPARE(consumers, 1); QVERIFY(consumerPathCorrect);
    QCOMPARE(bytes(rasterPath), original); QVERIFY(repaired->file->identity && *repaired->file->identity == *identity); QCOMPARE(repaired->file->creatorTaskId, source->pipeline.taskId);
    QCOMPARE(repaired->files.size(), size_t(2)); QCOMPARE(repaired->files[1].creatorTaskId, repaired->pipeline.taskId);
    QVERIFY(xerahs::editor::readXann(bytes(sidecarPath)).document); QCOMPARE(repaired->pipeline.retry->originalTaskId, source->pipeline.taskId);
    QVERIFY(!repaired->retryPlan().available());
    for (const auto &record : journal->list().entries) { QVERIFY(record.parsed.record); QVERIFY(record.parsed.record->terminal); }
  }
  void changedRasterAndCollidingSymlinkNeverAuthorizeCompanionPublication() {
    QTemporaryDir dir; const auto rasterPath = dir.filePath("capture.png"), sidecarPath = rasterPath + ".xann", foreign = dir.filePath("foreign");
    QFile other(foreign); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("KEEP"); other.close(); QVERIFY(QFile::link(foreign, sidecarPath));
    CaptureActionPorts ports; ports.annotation = [](const LoadedSource &source, const CancellationToken &) { return rectangle(source); };
    CapturePipeline first(pixels(), dir.path(), "capture", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&first, &CapturePipeline::finished); first.start(); QTRY_COMPARE(done.count(), 1);
    const auto source = receipt(done); QVERIFY(source->retryPlan().available()); QCOMPARE(bytes(foreign), QByteArray("KEEP"));
    QVERIFY(QFile::remove(sidecarPath)); const auto original = bytes(rasterPath); QVERIFY(QFile::remove(rasterPath));
    QFile replaced(rasterPath); QVERIFY(replaced.open(QIODevice::WriteOnly)); QVERIFY(replaced.setPermissions(QFile::ReadOwner | QFile::WriteOwner)); replaced.write(original); replaced.close();
    CapturePipeline retry(source, nullptr, ports); QSignalSpy next(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(next.count(), 1);
    QCOMPARE(receipt(next)->pipeline.orderedResults[0].state, ActionState::Failed); QVERIFY(!QFile::exists(sidecarPath)); QCOMPARE(bytes(rasterPath), original); QCOMPARE(bytes(foreign), QByteArray("KEEP"));
  }
  void cancellationAfterRasterAndJournalFailureKeepKnownPngAndEditableDocument() {
    QTemporaryDir dir; CaptureActionPorts ports; ports.annotation = [](const LoadedSource &source, const CancellationToken &) { return rectangle(source); };
    CapturePipeline *active = nullptr;
    ports.rasterPublished = [&](const std::vector<CaptureFile> &) { QMetaObject::invokeMethod(active, [&] { active->cancel(); }, Qt::BlockingQueuedConnection); };
    CapturePipeline cancelled(pixels(), dir.path(), "cancelled", {"annotation", "save", "image-clipboard"}, nullptr, ports); active = &cancelled;
    QSignalSpy done(&cancelled, &CapturePipeline::finished); cancelled.start(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done);
    QCOMPARE(result->pipeline.state, PipelineState::Cancelled); QVERIFY(result->file && result->annotationDocument); QVERIFY(result->retryPlan().available());
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Failed); QCOMPARE(result->pipeline.orderedResults[2].state, ActionState::Cancelled); QVERIFY(!QFile::exists(result->file->path + ".xann"));
    ports.rasterPublished = {}; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    bool changed = false;
    ports.save.beforePublish = [&](const QString &path) {
      if (path.endsWith(".png")) changed = QFile::setPermissions(dir.filePath("state"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther);
    };
    CapturePipeline failed(pixels(), dir.path(), "blocked", {"annotation", "save"}, nullptr, ports, journal); QSignalSpy next(&failed, &CapturePipeline::finished); failed.start(); QTRY_COMPARE(next.count(), 1);
    const auto blocked = receipt(next); QVERIFY(blocked->file && blocked->annotationDocument); QCOMPARE(blocked->pipeline.orderedResults[1].state, ActionState::Failed);
    QVERIFY(changed); QVERIFY(blocked->retryPlan().available()); QVERIFY(!QFile::exists(blocked->file->path + ".xann")); QVERIFY(!blocked->diagnostic.isEmpty());
    QVERIFY(QFile::setPermissions(dir.filePath("state"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto records = journal->list(); QVERIFY(records.entries[0].parsed.record); QVERIFY(!records.entries[0].parsed.record->terminal);
    CapturePipeline repair(blocked, nullptr, ports, journal); QSignalSpy repaired(&repair, &CapturePipeline::finished); repair.start(); QTRY_COMPARE(repaired.count(), 1);
    QCOMPARE(receipt(repaired)->pipeline.orderedResults[0].state, ActionState::Succeeded); QCOMPARE(receipt(repaired)->file->creatorTaskId, blocked->pipeline.taskId);
    QVERIFY(xerahs::editor::readXann(bytes(blocked->file->path + ".xann")).document);
  }
  void emptyDocumentFulfilsAbsenceAndPreservesAnUnownedCollision() {
    QTemporaryDir dir; CaptureActionPorts ports;
    ports.annotation = [](const LoadedSource &source, const CancellationToken &) { return CaptureAnnotationChoice{source.document, {}, true}; };
    CapturePipeline first(pixels(), dir.path(), "empty", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&first, &CapturePipeline::finished); first.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded); QCOMPARE(result->files.size(), size_t(1));
    QVERIFY(result->pipeline.orderedResults[1].artifacts[1].absent); QVERIFY(!QFile::exists(result->file->path + ".xann"));
    QFile foreign(dir.filePath("collision.png.xann")); QVERIFY(foreign.open(QIODevice::WriteOnly)); foreign.write("KEEP"); foreign.close();
    CapturePipeline second(pixels(), dir.path(), "collision", {"annotation", "save"}, nullptr, ports); QSignalSpy next(&second, &CapturePipeline::finished); second.start(); QTRY_COMPARE(next.count(), 1);
    QCOMPARE(receipt(next)->pipeline.orderedResults[1].state, ActionState::Failed); QCOMPARE(bytes(foreign.fileName()), QByteArray("KEEP")); QVERIFY(QFile::exists(dir.filePath("collision.png")));
  }
};
QTEST_MAIN(CapturePairedSaveTest)
#include "CapturePairedSaveTest.moc"
