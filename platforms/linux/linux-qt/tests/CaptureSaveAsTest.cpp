#include "CaptureSaveAsDialog.h"
#include "CapturePipeline.h"
#include "actions/SaveAsTarget.h"
#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
CaptureSaveAsDialog *picker() { return qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget()); }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
bool choosePath(CaptureSaveAsDialog *dialog, const QString &path) {
  auto *buttons = dialog->findChild<QDialogButtonBox *>();
  if (!buttons || !buttons->button(QDialogButtonBox::Save)) return false;
  // Qt 6.4 selectFile() intentionally leaves a focused filename field alone.
  // Select through Qt's public API with Save focused, verify the actual path
  // before the click, then exercise the real Save control.
  buttons->button(QDialogButtonBox::Save)->setFocus(); dialog->selectFile(path);
  if (!QTest::qWaitFor([&] { return dialog->selectedFiles().value(0) == path && buttons->button(QDialogButtonBox::Save)->isEnabled(); }, 3000)) return false;
  QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton); return true;
}
}
class CaptureSaveAsTest final : public QObject {
  Q_OBJECT
  QTemporaryDir preferences;
private slots:
  void initTestCase() {
    QVERIFY(preferences.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, preferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
  }
  void targetValidationIsLiteralBoundedAndPngOnly() {
    QCOMPARE(captureSaveAsTarget("/fixture/100% literal ü").path, QString("/fixture/100% literal ü.png"));
    QCOMPARE(captureSaveAsTarget("/fixture/Case.PNG").path, QString("/fixture/Case.PNG"));
    for (const auto &bad : {QString("relative.png"), QString("/fixture/image.jpg"), QString("/fixture/../image.png"),
                            QString("/fixture/"), QString("/fixture/a\nb.png"), QString("/fixture/") + QChar::Null + "a.png"})
      QVERIFY(!captureSaveAsTarget(bad).valid());
    QVERIFY(!captureSaveAsTarget("/fixture/" + QString(QChar(0xd800)) + ".png").valid());
    QVERIFY(!captureSaveAsTarget("/" + QString(32767, 'a')).valid());
  }
  void realPickerRejectsExistingFilesThenPublishesChosenLiteralPngAndJournal() {
    QTemporaryDir dir; QVERIFY(write(dir.filePath("keep.png"), "KEEP")); QVERIFY(QDir().mkdir(dir.filePath("folder"))); int outputs = 0; CaptureActionPorts ports;
    ports.fileOutput = [&](const QString &, const CaptureFile &file, const CancellationToken &) {
      ++outputs; return validateCaptureFile(file).isEmpty() ? ActionOutcome{} : ActionOutcome{ActionState::Failed, "fixture-invalid-file", {}};
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(pixels(), dir.path(), "default", {"save-as", "path-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(picker()); QPointer<CaptureSaveAsDialog> dialog(picker());
    QCOMPARE(dialog->windowModality(), Qt::ApplicationModal); QVERIFY(!QFile::exists(dir.filePath("state")));
    QVERIFY(choosePath(dialog, dir.filePath("keep.png"))); QVERIFY(dialog->isVisible()); QCOMPARE(done.count(), 0);
    QCOMPARE(bytes(dir.filePath("keep.png")), QByteArray("KEEP")); QVERIFY(dialog->findChild<QLabel *>("captureSaveAsStatus")->text().contains("save-as-destination-exists"));
    QVERIFY(choosePath(dialog, dir.filePath("folder")));
    QVERIFY(dialog->isVisible()); QCOMPARE(dialog->directory().absolutePath(), dir.filePath("folder"));
    QVERIFY(choosePath(dialog, dir.filePath("100% chosen ü"))); QTRY_COMPARE(done.count(), 1); QTRY_VERIFY(!dialog);
    const auto result = receipt(done); QCOMPARE(result->files.size(), size_t(1)); QCOMPARE(result->file->artifact, QString("save-as.file"));
    QCOMPARE(result->file->path, dir.filePath("100% chosen ü.png")); QCOMPARE(QImage(result->file->path), pixels()); QCOMPARE(outputs, 1);
    QCOMPARE(result->pipeline.orderedResults[1].inputArtifact, std::optional<QString>{"save-as.file"});
    QCOMPARE(result->file->filenameContext.value("mode"), QJsonValue("explicit-save-as"));
    const auto listing = journal->list(); QCOMPARE(listing.entries.size(), size_t(1)); QVERIFY(listing.entries[0].parsed.record);
    QVERIFY(listing.entries[0].parsed.record->terminal); QCOMPARE(listing.entries[0].parsed.record->report.value("ordered_results"), result->canonicalResult().value("ordered_results"));
  }
  void cancelPickerSkipsOnlySaveAsAndKeepsIndependentImageCopy() {
    QTemporaryDir dir; int copies = 0; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &image, const CancellationToken &) { if (image == pixels()) ++copies; return ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(pixels(), dir.path(), "default", {"save-as", "image-clipboard", "path-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(picker()); QTest::keyClick(picker(), Qt::Key_Escape);
    QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QCOMPARE(copies, 1); QVERIFY(result->files.empty());
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Cancelled); QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded);
    QCOMPARE(result->pipeline.orderedResults[2].state, ActionState::Skipped); QCOMPARE(result->pipeline.orderedResults[2].missingArtifact, std::optional<QString>{"save.file"});
    QVERIFY(!QFile::exists(dir.filePath("state"))); QCOMPARE(result->workingMedia, pixels()); QVERIFY(!result->retryPlan().available());
  }
  void globalCancellationAndOwnerDestructionCloseThePickerWithoutOutputs() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "default", {"save-as", "image-clipboard"});
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(picker()); QPointer<CaptureSaveAsDialog> dialog(picker()); pipeline.cancel();
    QTRY_COMPARE(done.count(), 1); QTRY_VERIFY(!dialog); QCOMPARE(receipt(done)->pipeline.state, PipelineState::Cancelled); QVERIFY(receipt(done)->files.empty());
    auto owned = std::make_unique<CapturePipeline>(pixels(), dir.path(), "default", QStringList{"save-as"}); owned->start(); QTRY_VERIFY(picker()); dialog = picker(); owned.reset();
    QVERIFY(!dialog); QTest::qWait(20); QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
  }
  void twoArtifactsKeepSavePriorityAndBothActualCreatorFacts() {
    QTemporaryDir dir; QString consumed; CaptureActionPorts ports;
    ports.chooseSaveAs = [&](const QString &, const CancellationToken &) { return CaptureSaveAsChoice{dir.filePath("chosen.png"), {}}; };
    ports.fileOutput = [&](const QString &, const CaptureFile &file, const CancellationToken &) { consumed = file.path; return ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(pixels(), dir.path(), "default", {"save", "save-as", "path-clipboard"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done);
    QCOMPARE(result->files.size(), size_t(2)); QCOMPARE(consumed, dir.filePath("default.png")); QCOMPARE(result->file->artifact, QString("save.file"));
    QCOMPARE(result->files[1].artifact, QString("save-as.file")); for (const auto &file : result->files) { QCOMPARE(QImage(file.path), pixels()); QCOMPARE(file.creatorTaskId, result->pipeline.taskId); QVERIFY(validateCaptureFile(file).isEmpty()); }
    QCOMPARE(result->canonicalResult().value("extensions").toObject().value("com.xerahs.app").toObject().value("file_artifacts").toArray().size(), 2);
    QVERIFY(journal->list().entries[0].parsed.record); QVERIFY(journal->list().entries[0].parsed.record->terminal);
  }
  void failedDefaultSaveFallsBackToSaveAsAndLinkedConsumerRetryKeepsBothSources() {
    QTemporaryDir dir; int encoded = 0, choices = 0, outputs = 0; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *out) { return ++encoded > 1 && image.save(out, "PNG"); };
    ports.chooseSaveAs = [&](const QString &, const CancellationToken &) { ++choices; return CaptureSaveAsChoice{dir.filePath("chosen.png"), {}}; };
    ports.fileOutput = [&](const QString &, const CaptureFile &, const CancellationToken &) { return ++outputs == 1 ? ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}} : ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "default", {"save", "save-as", "path-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); const auto source = receipt(done); QCOMPARE(source->file->artifact, QString("save-as.file"));
    QCOMPARE(source->pipeline.orderedResults[2].inputArtifact, std::optional<QString>{"save-as.file"}); QVERIFY(source->retryPlan().available());
    const auto before = bytes(source->file->path); CapturePipeline retry(source, nullptr, ports); QSignalSpy next(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(next.count(), 1);
    const auto result = receipt(next); QCOMPARE(choices, 1); QCOMPARE(result->files.size(), size_t(2)); QCOMPARE(result->files[1].creatorTaskId, source->pipeline.taskId);
    QCOMPARE(result->pipeline.orderedResults[1].inputArtifact, std::optional<QString>{"save.file"}); QCOMPARE(bytes(source->file->path), before); QCOMPARE(outputs, 2);
  }
  void explicitTargetCollisionAndCancellationBeforePublicationPreserveFilesAndPixels() {
    QTemporaryDir dir; const auto target = dir.filePath("chosen.png"); CancellationToken token;
    CaptureSaveHooks hooks; hooks.beforePublish = [&](const QString &path) { if (!write(path, "RACE WINNER")) throw 1; };
    const auto raced = saveCaptureAs(pixels(), target, token, hooks); QVERIFY(!raced.file); QCOMPARE(raced.diagnostic, QString("save-as-destination-exists")); QCOMPARE(bytes(target), QByteArray("RACE WINNER"));
    QVERIFY(QFile::remove(target)); hooks.beforePublish = [&](const QString &) { token.request(); };
    const auto cancelled = saveCaptureAs(pixels(), target, token, hooks); QVERIFY(cancelled.cancelled); QVERIFY(!cancelled.file); QVERIFY(!QFile::exists(target));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
  }
  void retryFailedSaveAsKeepsSuccessfulDefaultSaveAndDoesNotRepeatItsConsumers() {
    QTemporaryDir dir; QVERIFY(write(dir.filePath("blocked.png"), "KEEP")); int encoded = 0, choices = 0, outputs = 0; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *out) { ++encoded; return image.save(out, "PNG"); };
    ports.chooseSaveAs = [&](const QString &, const CancellationToken &) { return CaptureSaveAsChoice{dir.filePath(++choices == 1 ? "blocked.png" : "retry.png"), {}}; };
    ports.fileOutput = [&](const QString &, const CaptureFile &, const CancellationToken &) { ++outputs; return ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline original(pixels(), dir.path(), "default", {"save", "save-as", "path-clipboard"}, nullptr, ports, journal);
    QSignalSpy one(&original, &CapturePipeline::finished); original.start(); QTRY_COMPARE(one.count(), 1); const auto source = receipt(one);
    const auto before = bytes(source->file->path); QCOMPARE(source->retryPlan().selected, QStringList{"save-as"});
    CapturePipeline retry(source, nullptr, ports, journal); QSignalSpy two(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(two.count(), 1);
    const auto result = receipt(two); QCOMPARE(result->files.size(), size_t(2)); QCOMPARE(encoded, 3); QCOMPARE(outputs, 1);
    QCOMPARE(bytes(source->file->path), before); QCOMPARE(bytes(dir.filePath("blocked.png")), QByteArray("KEEP"));
    QCOMPARE(result->files[0].creatorTaskId, source->pipeline.taskId); QCOMPARE(result->files[1].creatorTaskId, result->pipeline.taskId);
    QCOMPARE(result->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(result->pipeline.orderedResults[0].action, QString("save-as"));
    for (const auto &entry : journal->list().entries) { QVERIFY(entry.parsed.record); QVERIFY(entry.parsed.record->terminal); }
  }
};
QTEST_MAIN(CaptureSaveAsTest)
#include "CaptureSaveAsTest.moc"
