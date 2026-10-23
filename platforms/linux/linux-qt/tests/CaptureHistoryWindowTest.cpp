#include "CaptureHistoryWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
using namespace xerahs::app;
using namespace xerahs::actions;
using namespace xerahs::history;
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
PipelineJournalSnapshot record(const QString &id, const QDateTime &at, std::vector<SavedFileArtifact> files, const QString &capture = {}) {
  PipelineJournalSnapshot value; value.result.taskId = id; value.selected = {"save"}; value.terminal = true; value.createdAt = at;
  ActionResult saved; saved.action = "save"; saved.state = files.empty() ? ActionState::Failed : ActionState::Succeeded;
  if (files.empty()) saved.diagnostic = "save-failed";
  value.result.orderedResults = {saved}; value.files = std::move(files);
  if (!capture.isEmpty()) value.result.workflowSnapshot = QJsonObject{{"name", "Docs"}, {"capture", capture}};
  return value;
}
SavedFileArtifact png(const QString &path, const QString &creator, int width = 40, int height = 30) {
  SavedFileArtifact file{path, "image/png", QFileInfo(path).size(), QJsonObject{{"width", width}, {"height", height}}};
  file.creatorTaskId = creator; return file;
}
QString writePng(const QString &path, QColor colour) {
  QImage image(40, 30, QImage::Format_ARGB32); image.fill(colour); image.save(path, "PNG"); return path;
}
std::vector<RecoveredPipelineJournal> recovered(const std::vector<PipelineJournalSnapshot> &snapshots) {
  std::vector<RecoveredPipelineJournal> out;
  for (const auto &s : snapshots) {
    auto parsed = parsePipelineJournal(serializePipelineJournal(s), s.result.taskId);
    if (!parsed.record) qFatal("fixture did not parse: %s", qPrintable(parsed.diagnostic));
    out.push_back(*parsed.record);
  }
  return out;
}
QListWidgetItem *find(QListWidget *list, const QString &fileName) {
  for (int i = 0; i < list->count(); ++i) if (list->item(i)->text().startsWith(fileName)) return list->item(i);
  return nullptr;
}
}
class CaptureHistoryWindowTest final : public QObject {
  Q_OBJECT
private slots:
  void itemsKeepOnlyFilesTheirRunCreatedNewestFirst() {
    QTemporaryDir dir; const auto a = newId(), b = newId(), c = newId();
    const auto first = writePng(dir.filePath("first.png"), Qt::red), second = writePng(dir.filePath("second.png"), Qt::blue);
    const auto at = QDateTime::fromString("2026-10-20T08:00:00Z", Qt::ISODate);
    auto retry = record(b, at.addSecs(60), {png(first, a), png(second, b)}, "monitor");
    const auto items = captureHistoryItems(recovered({record(a, at, {png(first, a)}, "region"), retry, record(c, at.addSecs(120), {})}));
    QCOMPARE(items.size(), std::size_t(2));
    QCOMPARE(items[0].taskId, b); QCOMPARE(items[0].files.size(), std::size_t(1)); QCOMPARE(items[0].files[0].path, second);
    QCOMPARE(items[0].captureSource, QString("monitor")); QCOMPARE(items[0].workflowName, QString("Docs"));
    QCOMPARE(items[0].files[0].width, qint64(40));
    QCOMPARE(items[1].taskId, a); QCOMPARE(items[1].state, QString("completed"));
  }
  void searchAndPagingAreBounded() {
    std::vector<CaptureHistoryItem> items;
    for (int i = 0; i < kHistoryPageSize + 5; ++i) {
      CaptureHistoryItem item; item.taskId = QString::number(i);
      item.createdAt = QDateTime::fromString("2026-10-21T10:00:00Z", Qt::ISODate);
      item.files.push_back({QStringLiteral("/home/u/Pictures/shot-%1.png").arg(i)});
      items.push_back(item);
    }
    items[3].captureSource = "full-screen";
    int total = 0;
    QCOMPARE(int(historyPage(items, {}, 1, &total).size()), kHistoryPageSize); QCOMPARE(total, kHistoryPageSize + 5);
    QCOMPARE(int(historyPage(items, {}, 2).size()), kHistoryPageSize + 5);
    QCOMPARE(int(historyPage(items, "FULL-screen", 1, &total).size()), 1); QCOMPARE(total, 1);
    QCOMPARE(int(historyPage(items, "shot-20", 1).size()), 6);  // shot-20 and shot-200 to shot-204
    QVERIFY(matchesHistoryQuery(items[0], items[0].createdAt.toLocalTime().toString("yyyy-MM-dd")));
    QVERIFY(!matchesHistoryQuery(items[0], "nothing-like-this"));
  }
  void windowShowsThumbnailsMissingFilesAndFileActions() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto kept = writePng(dir.filePath("kept.png"), Qt::green), gone = writePng(dir.filePath("gone.png"), Qt::yellow);
    const auto a = newId(), b = newId(); const auto now = QDateTime::currentDateTimeUtc();
    QVERIFY(storage->replace(a, {}, serializePipelineJournal(record(a, now.addSecs(-60), {png(kept, a)}, "region"))).stored);
    QVERIFY(storage->replace(b, {}, serializePipelineJournal(record(b, now, {png(gone, b)}, "full-screen"))).stored);
    QVERIFY(QFile::remove(gone));
    QString edited;
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, [&](const QString &path, QWidget *) { edited = path; }, &owner);
    window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text().section('\n', 0, 0), QString("gone.png"));
    QTRY_VERIFY(list->item(0)->toolTip().contains("no longer at this path"));
    auto *open = window->findChild<QPushButton *>("openHistoryCapture"), *edit = window->findChild<QPushButton *>("editHistoryCapture");
    auto *copyPath = window->findChild<QPushButton *>("copyHistoryPath");
    list->setCurrentItem(list->item(0)); QVERIFY(!open->isEnabled()); QVERIFY(!edit->isEnabled()); QVERIFY(copyPath->isEnabled());
    list->setCurrentItem(find(list, "kept.png")); QTRY_VERIFY(edit->isEnabled());
    edit->click(); QCOMPARE(edited, kept);
    copyPath->click(); QCOMPARE(QApplication::clipboard()->text(), kept);
    auto *search = window->findChild<QLineEdit *>("captureHistorySearch"); search->setText("full-screen");
    QCOMPARE(list->count(), 1); QVERIFY(find(list, "gone.png"));
    search->clear(); QCOMPARE(list->count(), 2);
  }
  void forgetRemovesTheRecordAndKeepsTheFile() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("keep-me.png"), Qt::cyan); const auto id = newId();
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, QDateTime::currentDateTimeUtc(), {png(path, id)}))).stored);
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    auto *forget = window->findChild<QPushButton *>("forgetHistoryCapture"); QTRY_VERIFY(forget->isEnabled()); forget->click();
    QMessageBox *box = nullptr; QTRY_VERIFY((box = window->findChild<QMessageBox *>("forgetHistoryCaptureDialog")));
    box->button(QMessageBox::Discard)->click();
    QTRY_COMPARE(list->count(), 0); QVERIFY(storage->list().entries.empty()); QVERIFY(QFile::exists(path));
  }
  void activeWindowSourceShowsItsLabel() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("active.png"), Qt::darkGray); const auto id = newId();
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, QDateTime::currentDateTimeUtc(), {png(path, id)}, "active-window"))).stored);
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("Active window")));
    QVERIFY(!list->item(0)->text().contains(QStringLiteral("active-window")));
  }
  void unknownSourceStillShowsItsRawId() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("future.png"), Qt::lightGray); const auto id = newId();
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, QDateTime::currentDateTimeUtc(), {png(path, id)}, "future-source"))).stored);
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("future-source")));
  }
  void runningCaptureDefersTheRead() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("later.png"), Qt::magenta); const auto id = newId();
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, QDateTime::currentDateTimeUtc(), {png(path, id)}))).stored);
    bool busy = true; QWidget owner; auto *window = new CaptureHistoryWindow(storage, [&] { return busy; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTest::qWait(60); QCOMPARE(list->count(), 0);
    busy = false; QTRY_COMPARE(list->count(), 1);
  }
};
QTEST_MAIN(CaptureHistoryWindowTest)
#include "CaptureHistoryWindowTest.moc"
