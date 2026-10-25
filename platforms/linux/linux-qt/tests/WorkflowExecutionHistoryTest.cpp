#include "WorkflowExecutionHistoryDialog.h"
#include "features/WorkflowExecutionHistoryFeature.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <atomic>
#include <stdexcept>
using namespace xerahs::app;
using namespace xerahs::workflows;
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
void write(const QString &path, const QByteArray &bytes) { QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) qFatal("Fixture write failed"); }
QString record(NativeWorkflowExecutions &store, const QString &directory, bool terminal = true) {
  auto w = defaultCaptureWorkflows().entries.front(); w.name = "sensitive-title-fixture"; w.output.directory = directory;
  const auto id = uuid(); WorkflowExecution execution(id, "native-shell", w, store.writer());
  if (!execution.admit() || !execution.start() || (terminal && !execution.finish("cancelled"))) qFatal("Fixture admission failed");
  return id;
}
QTreeWidget *table(QWidget *dialog) { return dialog->findChild<QTreeWidget *>("workflowExecutionRecords"); }
QPushButton *forget(QWidget *dialog) { return dialog->findChild<QPushButton *>("forgetWorkflowExecution"); }
QLabel *status(QWidget *dialog) { return dialog->findChild<QLabel *>("workflowExecutionHistoryStatus"); }
void choose(QWidget *dialog, const QString &id) {
  for (int i = 0; i < table(dialog)->topLevelItemCount(); ++i) if (table(dialog)->topLevelItem(i)->text(2) == id) { table(dialog)->setCurrentItem(table(dialog)->topLevelItem(i)); return; }
  qFatal("Missing fixture row");
}
}
class WorkflowExecutionHistoryTest final : public QObject {
  Q_OBJECT
private slots:
  void sharedRegistryCommandReviewsAndForgetsOnlyConfirmedMetadata() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records"));
    const auto id = record(store, dir.filePath("sensitive-folder-fixture")); const auto path = dir.filePath("records/" + id + ".json"); const auto before = read(path);
    const auto unknown = record(store, dir.path(), false);
    const auto output = dir.filePath("image.png"), pipeline = dir.filePath("pipeline.json"); write(output, "IMAGE"); write(pipeline, "PIPELINE");
    CommandRegistry commands; QWidget owner; auto *action = installWorkflowExecutionHistory(commands, workflowHistoryPorts(store), [] { return false; }, [&] { return &owner; }, &owner);
    QVERIFY(action); for (auto surface : {CommandSurface::Window, CommandSurface::Tray, CommandSurface::CaptureMenu}) QVERIFY(commands.actions(surface).contains(action));
    action->trigger(); QPointer<WorkflowExecutionHistoryDialog> dialog = owner.findChild<WorkflowExecutionHistoryDialog *>(); QVERIFY(dialog);
    action->trigger(); QCOMPARE(owner.findChildren<WorkflowExecutionHistoryDialog *>().size(), 1);
    QTRY_COMPARE(table(dialog)->topLevelItemCount(), 2); choose(dialog, unknown); QCOMPARE(table(dialog)->currentItem()->text(1), QString("outcome-unconfirmed")); choose(dialog, id);
    QVERIFY(!table(dialog)->accessibleName().isEmpty()); for (auto *label : dialog->findChildren<QLabel *>()) { QVERIFY(!label->text().contains("sensitive-")); QCOMPARE(label->textFormat(), Qt::PlainText); }
    QVERIFY(forget(dialog)->isEnabled()); forget(dialog)->click(); QPointer<QMessageBox> box = dialog->findChild<QMessageBox *>("forgetWorkflowExecutionDialog"); QVERIFY(box); QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
    box->button(QMessageBox::Cancel)->click(); QTRY_VERIFY(!box); QCOMPARE(read(path), before);
    forget(dialog)->click(); box = dialog->findChild<QMessageBox *>("forgetWorkflowExecutionDialog"); QVERIFY(box); box->button(QMessageBox::Discard)->click();
    QTRY_VERIFY(!QFile::exists(path)); QTRY_COMPARE(table(dialog)->topLevelItemCount(), 1); QCOMPARE(read(output), QByteArray("IMAGE")); QCOMPARE(read(pipeline), QByteArray("PIPELINE"));
    QVERIFY(store.result(unknown).value("ok").toBool()); QTest::keyClick(dialog, Qt::Key_Escape); QTRY_VERIFY(!dialog); action->trigger(); QVERIFY(owner.findChild<WorkflowExecutionHistoryDialog *>());
  }
  void changedCorruptAndFutureRecordsArePreserved() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); const auto id = record(store, dir.path());
    const auto path = dir.filePath("records/" + id + ".json"); const auto before = read(path);
    QWidget owner; auto *dialog = new WorkflowExecutionHistoryDialog(workflowHistoryPorts(store), [] { return false; }, &owner); dialog->show();
    QTRY_COMPARE(table(dialog)->topLevelItemCount(), 1); forget(dialog)->click(); auto *box = dialog->findChild<QMessageBox *>("forgetWorkflowExecutionDialog"); QVERIFY(box);
    write(path, before + " "); box->button(QMessageBox::Discard)->click(); QTRY_VERIFY(status(dialog)->text().contains("workflow-record-changed")); QCOMPARE(read(path), before + " ");
    const auto corrupt = record(store, dir.path()), future = record(store, dir.path()); const auto corruptPath = dir.filePath("records/" + corrupt + ".json"), futurePath = dir.filePath("records/" + future + ".json");
    write(corruptPath, "{damaged-fixture"); auto object = QJsonDocument::fromJson(read(futurePath)).object(); object.insert("version", 99); const auto newer = QJsonDocument(object).toJson(); write(futurePath, newer);
    dialog->findChild<QPushButton *>("refreshWorkflowExecutions")->click(); QTRY_COMPARE(table(dialog)->topLevelItemCount(), 3);
    for (const auto &invalid : {corrupt, future}) { choose(dialog, invalid); QVERIFY(!forget(dialog)->isEnabled()); }
    QVERIFY(!store.erase(corrupt, read(corruptPath)).stored); QVERIFY(!store.erase(future, newer).stored); QCOMPARE(read(corruptPath), QByteArray("{damaged-fixture")); QCOMPARE(read(futurePath), newer); QCOMPARE(read(path), before + " ");
  }
  void forgettingAnUnconfirmedObservationCannotResurrectItsWriter() {
    QTemporaryDir dir; const auto folder = dir.filePath("records"); NativeWorkflowExecutions store(folder);
    auto workflow = defaultCaptureWorkflows().entries.front(); workflow.output.directory = dir.path();
    const auto id = uuid(); WorkflowExecution running(id, "external-activation", workflow, store.writer());
    QVERIFY(running.admit()); QVERIFY(running.start());
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto entry = listing.entries.front(); QCOMPARE(entry.id, id);
    QCOMPARE(entry.result.value("state"), QJsonValue("outcome-unconfirmed"));
    QVERIFY(store.erase(id, entry.snapshot).stored);
    QVERIFY(!running.processing(uuid()));
    QVERIFY(!QFile::exists(folder + "/" + id + ".json"));
    QCOMPARE(store.result(id).value("error").toObject().value("code"), QJsonValue("workflow-execution-not-found"));
  }
  void captureDefersReadsAndRevokesPendingRemoval() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); const auto id = record(store, dir.path()); const auto before = store.list().entries[0].snapshot;
    bool busy = true; std::atomic<int> reads{0}, erases{0}; std::atomic<bool> worker{false}; auto ports = workflowHistoryPorts(store); const auto list = ports.list; const auto erase = ports.erase;
    ports.list = [&] { ++reads; worker = QThread::currentThread() != qApp->thread(); return list(); };
    ports.erase = [&](const QString &key, const QByteArray &snapshot) { ++erases; return erase(key, snapshot); };
    QWidget owner; auto *dialog = new WorkflowExecutionHistoryDialog(ports, [&] { return busy; }, &owner); dialog->show();
    QTRY_VERIFY(status(dialog)->text().contains("Capture is running")); QCOMPARE(reads.load(), 0); QVERIFY(!forget(dialog)->isEnabled()); busy = false;
    QTRY_COMPARE(table(dialog)->topLevelItemCount(), 1); QVERIFY(worker); forget(dialog)->click(); auto *box = dialog->findChild<QMessageBox *>("forgetWorkflowExecutionDialog"); QVERIFY(box); busy = true;
    box->button(QMessageBox::Discard)->click(); QCOMPARE(erases.load(), 0); QCOMPARE(store.list().entries[0].snapshot, before); QVERIFY(store.result(id).value("ok").toBool());
  }
  void storeErrorsAndExceptionsDoNotExposePayloadOrLoseMetadata() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); const auto id = record(store, dir.path()); const auto before = store.list().entries[0].snapshot;
    auto ports = workflowHistoryPorts(store); ports.erase = [](const QString &, const QByteArray &) -> RecordWrite { throw std::runtime_error("payload-fixture"); };
    QWidget owner; auto *dialog = new WorkflowExecutionHistoryDialog(ports, [] { return false; }, &owner); dialog->show(); QTRY_COMPARE(table(dialog)->topLevelItemCount(), 1);
    forget(dialog)->click(); auto *box = dialog->findChild<QMessageBox *>("forgetWorkflowExecutionDialog"); QVERIFY(box); box->button(QMessageBox::Discard)->click();
    QTRY_VERIFY(status(dialog)->text().contains("workflow-storage-unavailable")); QVERIFY(!status(dialog)->text().contains("payload-fixture")); QCOMPARE(store.list().entries[0].snapshot, before); QVERIFY(store.result(id).value("ok").toBool());
    WorkflowHistoryPorts failed; failed.list = []() -> WorkflowExecutionListing { throw std::runtime_error("payload-fixture"); };
    auto *other = new WorkflowExecutionHistoryDialog(failed, [] { return false; }, &owner); other->show(); QTRY_VERIFY(status(other)->text().contains("workflow-storage-unavailable")); QVERIFY(!forget(other)->isEnabled());
  }
  void destroyingAnOwnerNeverJoinsOrPublishesALateRead() {
    auto release = std::make_shared<std::atomic_bool>(false), entered = std::make_shared<std::atomic_bool>(false), finished = std::make_shared<std::atomic_bool>(false);
    WorkflowHistoryPorts ports; ports.list = [release, entered, finished] { *entered = true; while (!release->load()) QThread::msleep(1); *finished = true; return WorkflowExecutionListing{}; };
    auto *owner = new QWidget; auto *dialog = new WorkflowExecutionHistoryDialog(ports, [] { return false; }, owner); dialog->show(); QTRY_VERIFY(entered->load());
    QPointer<WorkflowExecutionHistoryDialog> weak(dialog); QElapsedTimer elapsed; elapsed.start(); delete owner; const auto milliseconds = elapsed.elapsed(); *release = true;
    QVERIFY(!weak); QVERIFY(milliseconds < 150); QTRY_VERIFY(finished->load());
  }
  void explicitRemovalFreesCapacityWithoutTouchingOtherRecords() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); const auto id = record(store, dir.path()); const auto fixture = store.list().entries[0].snapshot;
    const auto parsed = *parseWorkflowExecution(fixture, id);
    for (int i = 1; i < 1024; ++i) { auto next = parsed; next.id = uuid(); write(dir.filePath("records/" + next.id + ".json"), serializeWorkflowExecution(next)); }
    auto workflow = defaultCaptureWorkflows().entries[0]; WorkflowExecution refused(uuid(), "native-shell", workflow, store.writer()); QVERIFY(!refused.admit());
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1024)); QVERIFY(listing.diagnostic.isEmpty());
    QVERIFY(store.erase(id, fixture).stored); QCOMPARE(store.list().entries.size(), size_t(1023)); WorkflowExecution admitted(uuid(), "native-shell", workflow, store.writer()); QVERIFY(admitted.admit());
  }
};
QTEST_MAIN(WorkflowExecutionHistoryTest)
#include "WorkflowExecutionHistoryTest.moc"
