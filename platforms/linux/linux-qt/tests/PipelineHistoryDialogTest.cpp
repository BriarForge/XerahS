#include "PipelineHistoryDialog.h"
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QUuid>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
PipelineJournalSnapshot snapshot(const QString &id, const QString &path) {
  PipelineJournalSnapshot value; value.result.taskId = id; value.selected = {"save", "image-clipboard"}; value.terminal = true;
  value.createdAt = QDateTime::fromString("2026-10-13T01:02:03.004Z", Qt::ISODateWithMs);
  ActionResult saved; saved.action = "save"; saved.state = ActionState::Succeeded;
  ActionResult copied; copied.action = "image-clipboard"; copied.state = ActionState::Failed; copied.diagnostic = "clipboard-unavailable";
  value.result.orderedResults = {saved, copied}; value.files = {{path, "image/png", 4, {}}}; return value;
}
QByteArray fileBytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QMessageBox *confirmation(QWidget *owner) {
  for (auto *box : owner->findChildren<QMessageBox *>("forgetCaptureHistoryDialog")) if (box->isVisible()) return box;
  return nullptr;
}
}
class PipelineHistoryDialogTest final : public QObject {
  Q_OBJECT
private slots:
  void pendingCompanionReviewSeparatesKnownRasterFromUnknownSave() {
    QTemporaryDir dir; const auto rasterPath = dir.filePath("capture.png"); QFile raster(rasterPath); QVERIFY(raster.open(QIODevice::WriteOnly)); raster.write("KEEP"); raster.close();
    auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state")); const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    PipelineJournalSnapshot record; record.result.taskId = id; record.createdAt = QDateTime::currentDateTimeUtc(); record.selected = {"save"};
    record.files = {{rasterPath, "image/png", 4, {}}}; record.pendingAction = "save"; record.pendingSavePath = rasterPath + ".xann"; record.pendingSaveArtifact = "save.sidecar";
    const auto encoded = serializePipelineJournal(record); QVERIFY(storage->replace(id, {}, encoded).stored);
    QWidget owner; auto *dialog = new PipelineHistoryDialog(storage, [] { return false; }, &owner); dialog->show();
    auto *review = dialog->findChild<QPushButton *>("reviewCaptureHistory"); QTRY_VERIFY(review->isEnabled()); review->click();
    auto *result = dialog->findChild<QDialog *>("retainedCaptureResultDialog"); QVERIFY(result);
    auto *actions = result->findChild<QTreeWidget *>("captureActionResults"); QCOMPARE(actions->topLevelItem(0)->text(1), QString("Outcome unknown"));
    auto *artifacts = result->findChild<QTreeWidget *>("captureArtifactResults"); QVERIFY(artifacts); QCOMPARE(artifacts->topLevelItemCount(), 2);
    QCOMPARE(artifacts->topLevelItem(0)->text(1), QString("PNG file")); QCOMPARE(artifacts->topLevelItem(0)->text(2), QString("publication acknowledged"));
    QCOMPARE(artifacts->topLevelItem(1)->text(1), QString("Editable annotations (.xann)")); QCOMPARE(artifacts->topLevelItem(1)->text(2), QString("Outcome unknown"));
    QTest::keyClick(result, Qt::Key_Escape); QCOMPARE(storage->list().entries.front().bytes, encoded); QCOMPARE(fileBytes(rasterPath), QByteArray("KEEP"));
  }
  void runningCaptureDefersReadsUntilItsLiveJournalCanBeInterpretedHonestly() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(snapshot(id, dir.filePath("saved.png")))).stored);
    bool busy = true; QWidget owner; auto *dialog = new PipelineHistoryDialog(storage, [&] { return busy; }, &owner); dialog->show();
    auto *table = dialog->findChild<QTreeWidget *>("captureHistoryRecords");
    auto *refresh = dialog->findChild<QPushButton *>("refreshCaptureHistory"); QTest::qWait(40);
    QCOMPARE(table->topLevelItemCount(), 0); QVERIFY(!refresh->isEnabled());
    busy = false; QTRY_COMPARE(table->topLevelItemCount(), 1); QTRY_VERIFY(refresh->isEnabled());
    QCOMPARE(table->topLevelItem(0)->text(1), QString("completed"));
  }
  void reviewIsKeyboardReachableAndImmutableWithPrivateDetailsHidden() {
    QTemporaryDir dir; const auto artifact = dir.filePath("<private source>.png"); QFile file(artifact); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("KEEP"); file.close();
    auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state")); const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto encoded = serializePipelineJournal(snapshot(id, artifact)); QVERIFY(storage->replace(id, {}, encoded).stored);
    QWidget owner; auto *dialog = new PipelineHistoryDialog(storage, [] { return false; }, &owner); dialog->show();
    auto *table = dialog->findChild<QTreeWidget *>("captureHistoryRecords"); QVERIFY(table); QTRY_COMPARE(table->topLevelItemCount(), 1);
    auto *review = dialog->findChild<QPushButton *>("reviewCaptureHistory"); QTRY_VERIFY(review->isEnabled());
    review->setFocus(); QTest::keyClick(review, Qt::Key_Space);
    QPointer<QDialog> result = dialog->findChild<QDialog *>("retainedCaptureResultDialog"); QVERIFY(result); QTRY_VERIFY(result->isVisible());
    auto *outcomes = result->findChild<QTreeWidget *>("captureActionResults"); QVERIFY(outcomes); QCOMPARE(outcomes->topLevelItemCount(), 2);
    QCOMPARE(outcomes->topLevelItem(1)->text(1), QString("failed"));
    auto *json = result->findChild<QPlainTextEdit *>("captureResultJson"); QVERIFY(json); QVERIFY(json->isReadOnly()); QVERIFY(!json->isVisible());
    QCOMPARE(QJsonDocument::fromJson(json->toPlainText().toUtf8()).object().value("ordered_results"),
        storage->list().entries.front().parsed.record->report.value("ordered_results"));
    QTest::keyClick(result, Qt::Key_Escape); QTRY_VERIFY(!result || !result->isVisible());
    QCOMPARE(storage->list().entries.front().bytes, encoded); QCOMPARE(fileBytes(artifact), QByteArray("KEEP"));
  }
  void forgetRequiresConfirmationAndExactUnchangedSnapshotWhileKeepingTheFile() {
    QTemporaryDir dir; const auto artifact = dir.filePath("source.png"); QFile file(artifact); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("KEEP"); file.close();
    auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state")); const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto record = snapshot(id, artifact); const auto first = serializePipelineJournal(record); QVERIFY(storage->replace(id, {}, first).stored);
    bool busy = false; QWidget owner; auto *dialog = new PipelineHistoryDialog(storage, [&] { return busy; }, &owner); dialog->show();
    auto *table = dialog->findChild<QTreeWidget *>("captureHistoryRecords"); QTRY_COMPARE(table->topLevelItemCount(), 1);
    auto *forget = dialog->findChild<QPushButton *>("forgetCaptureHistory"); QTRY_VERIFY(forget->isEnabled());
    forget->click(); QPointer<QMessageBox> box = confirmation(dialog); QVERIFY(box);
    QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); QTest::keyClick(box, Qt::Key_Escape); QTRY_VERIFY(forget->isEnabled());
    QCOMPARE(storage->list().entries.front().bytes, first);
    forget->click(); box = confirmation(dialog); QVERIFY(box);
    record.result.orderedResults[1].diagnostic = "action-failed"; const auto updated = serializePipelineJournal(record);
    QVERIFY(storage->replace(id, first, updated).stored); box->button(QMessageBox::Discard)->click();
    auto *refresh = dialog->findChild<QPushButton *>("refreshCaptureHistory"); QTRY_VERIFY(refresh->isEnabled());
    QCOMPARE(storage->list().entries.front().bytes, updated); QCOMPARE(fileBytes(artifact), QByteArray("KEEP"));
    refresh->click(); QTRY_VERIFY(refresh->isEnabled()); busy = true; QTRY_VERIFY(!forget->isEnabled());
    busy = false; QTRY_VERIFY(forget->isEnabled()); forget->click(); box = confirmation(dialog);
    QVERIFY(box); box->button(QMessageBox::Discard)->click(); QTRY_COMPARE(table->topLevelItemCount(), 0);
    QVERIFY(storage->list().entries.empty()); QCOMPARE(fileBytes(artifact), QByteArray("KEEP"));
  }
  void interruptedRecordShowsUnknownActionAndDoesNotInventFailedUpload() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces); PipelineJournalSnapshot record;
    record.createdAt = QDateTime::currentDateTimeUtc(); record.result.taskId = id; record.selected = {"upload", "reveal"}; record.pendingAction = "upload";
    const auto encoded = serializePipelineJournal(record); QVERIFY(storage->replace(id, {}, encoded).stored);
    QWidget owner; auto *dialog = new PipelineHistoryDialog(storage, [] { return false; }, &owner); dialog->show();
    auto *review = dialog->findChild<QPushButton *>("reviewCaptureHistory"); QTRY_VERIFY(review->isEnabled()); review->click();
    auto *result = dialog->findChild<QDialog *>("retainedCaptureResultDialog"); QVERIFY(result);
    auto *outcomes = result->findChild<QTreeWidget *>("captureActionResults"); QVERIFY(outcomes); QCOMPARE(outcomes->topLevelItemCount(), 2);
    QCOMPARE(outcomes->topLevelItem(0)->text(0), QString("upload")); QCOMPARE(outcomes->topLevelItem(0)->text(1), QString("Outcome unknown"));
    QCOMPARE(outcomes->topLevelItem(1)->text(1), QString("Not started")); QCOMPARE(storage->list().entries.front().bytes, encoded);
  }
};
QTEST_MAIN(PipelineHistoryDialogTest)
#include "PipelineHistoryDialogTest.moc"
