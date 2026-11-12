#include "CaptureHistoryWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
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
  void initTestCase() {
    static QTemporaryDir state;
    qputenv("XDG_STATE_HOME", state.path().toUtf8());
  }
  void legacyHistoryPreviewIsReachableAndClosesWithHistory() {
    QTemporaryDir dir;
    QPointer<CaptureHistoryWindow> window = new CaptureHistoryWindow(
        std::make_shared<NativePipelineJournal>(dir.filePath("state")), [] { return false; }, {});
    window->show();
    auto *open = window->findChild<QPushButton *>("previewLegacyHistory"); QVERIFY(open && open->isEnabled());
    QTest::mouseClick(open, Qt::LeftButton);
    QPointer<QWidget> preview = window->findChild<QWidget *>("legacyHistoryWindow");
    QVERIFY(preview && preview->isVisible());
    QTRY_VERIFY(preview->findChild<QPushButton *>("chooseLegacyHistory")->isEnabled());
    window->close(); QTRY_VERIFY(window.isNull()); QVERIFY(preview.isNull());
  }
  void uploadOnlyRowsRetainUrlsAndProtectSourceFiles_data() {
    QTest::addColumn<bool>("image");
    QTest::newRow("existing-image") << true;
    QTest::newRow("existing-file") << false;
  }
  void uploadOnlyRowsRetainUrlsAndProtectSourceFiles() {
    QFETCH(bool, image);
    QTemporaryDir dir; const auto directory = dir.filePath("state");
    auto storage = std::make_shared<NativePipelineJournal>(directory);
    const auto path = image ? writePng(dir.filePath("external.png"), Qt::blue) : dir.filePath("external.txt");
    if (!image) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("source file"); }
    FileUploadHistoryRecord upload;
    upload.taskId = newId(); upload.path = path; upload.origin = "file-upload";
    upload.destinationName = "Destination fixture"; upload.createdAt = QDateTime::currentDateTimeUtc();
    upload.result = {"https://example.test/upload", "https://example.test/thumb", "https://example.test/delete", {}};
    QVERIFY(storage->recordFileUpload(upload).stored);
    QWidget owner; int edits = 0;
    auto *window = new CaptureHistoryWindow(std::make_shared<NativePipelineJournal>(directory), [] { return false; },
        [&](const QString &, QWidget *) { ++edits; }, &owner);
    QList<QUrl> opened; window->setUrlOpener([&](const QUrl &url) { opened << url; return true; }); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    auto *search = window->findChild<QLineEdit *>("captureHistorySearch");
    search->setText("EXTERNAL"); QCOMPARE(list->count(), 1);
    search->setText("DESTINATION fixture"); QCOMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    auto button = [&](const char *name) { return window->findChild<QPushButton *>(name); };
    QTRY_VERIFY(button("openHistoryCapture")->isEnabled());
    QTRY_VERIFY(button("forgetHistoryCapture")->isEnabled());
    QVERIFY(button("revealHistoryCapture")->isEnabled());
    QVERIFY(!button("editHistoryCapture")->isEnabled()); QVERIFY(!button("copyHistoryImage")->isEnabled());
    QVERIFY(!button("trashHistoryCapture")->isEnabled());
    QVERIFY(button("copyHistoryPath")->isEnabled());
    button("copyHistoryUrl")->click(); QCOMPARE(QApplication::clipboard()->text(), upload.result.url);
    button("copyHistoryDeletionUrl")->click(); QCOMPARE(QApplication::clipboard()->text(), upload.result.deletionUrl);
    button("openHistoryUrl")->click(); QCOMPARE(opened, QList<QUrl>{QUrl(upload.result.url)});
    QVERIFY(list->item(0)->text().contains("file-upload")); QVERIFY(list->item(0)->text().contains(upload.destinationName));
    QVERIFY(QFile::remove(path)); window->refresh();
    QTRY_VERIFY(list->count() == 1 && list->item(0)->text().contains("File missing"));
    list->setCurrentItem(list->item(0));
    QVERIFY(!button("openHistoryCapture")->isEnabled()); QVERIFY(!button("revealHistoryCapture")->isEnabled());
    QVERIFY(button("copyHistoryUrl")->isEnabled()); QVERIFY(button("openHistoryUrl")->isEnabled());
    QCOMPARE(edits, 0);
  }
  void failedUploadMessageIsVisible() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    FileUploadHistoryRecord upload;
    upload.taskId = newId(); upload.path = dir.filePath("failed.txt"); upload.origin = "watch-folder";
    upload.destinationName = "Offline fixture"; upload.createdAt = QDateTime::currentDateTimeUtc();
    upload.failureCode = "network-error"; upload.result.errorMessage = "network-error: The server could not be reached.";
    QVERIFY(storage->recordFileUpload(upload).stored);
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(upload.result.errorMessage));
    QVERIFY(list->item(0)->text().contains("watch-folder"));
    list->setCurrentItem(list->item(0));
    QVERIFY(!window->findChild<QPushButton *>("copyHistoryUrl")->isEnabled());
  }

  void resultsWithoutFilesRemainVisibleInspectableAndForgettable_data() {
    QTest::addColumn<QString>("state");
    for (const auto *state : {"failed", "cancelled", "interrupted", "uploaded"}) QTest::newRow(state) << QString(state);
  }
  void resultsWithoutFilesRemainVisibleInspectableAndForgettable() {
    QFETCH(QString, state);
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto id = newId();
    auto snapshot = record(id, QDateTime::currentDateTimeUtc(), {}, "region");
    QString outcome = "transient-failure";
    snapshot.result.orderedResults.front().failureClass = FailureClass::TransientFailure;
    if (state == "cancelled") {
      snapshot.result.state = PipelineState::Cancelled;
      snapshot.result.orderedResults.front().state = ActionState::Cancelled;
      snapshot.result.orderedResults.front().failureClass.reset();
      snapshot.result.orderedResults.front().diagnostic.reset(); outcome = "cancelled";
    } else if (state == "uploaded") {
      snapshot.selected = {"upload"};
      snapshot.result.orderedResults.front().action = "upload";
      snapshot.result.orderedResults.front().state = ActionState::Succeeded;
      snapshot.result.orderedResults.front().failureClass.reset();
      snapshot.result.orderedResults.front().diagnostic.reset();
      snapshot.result.upload = UploadResult{"https://example.test/history-result", {}, {}, {}};
      outcome = "succeeded";
    } else if (state == "interrupted") {
      snapshot.terminal = false; snapshot.pendingAction = "save";
      snapshot.result.orderedResults.clear(); outcome = "interrupted";
    }
    const auto bytes = serializePipelineJournal(snapshot);
    QVERIFY(storage->replace(id, {}, bytes).stored);
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), id);
    QVERIFY(list->item(0)->text().contains("No confirmed file"));
    QVERIFY(list->item(0)->text().contains(outcome));
    auto *search = window->findChild<QLineEdit *>("captureHistorySearch");
    search->setText(id); QCOMPARE(list->count(), 1);
    search->setText(outcome); QCOMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    for (const auto *name : {"openHistoryCapture", "editHistoryCapture", "copyHistoryImage", "copyHistoryPath", "revealHistoryCapture", "trashHistoryCapture"})
      QVERIFY2(!window->findChild<QPushButton *>(name)->isEnabled(), name);
    auto *copyUrl = window->findChild<QPushButton *>("copyHistoryUrl");
    QCOMPARE(copyUrl->isEnabled(), state == "uploaded");
    if (state == "uploaded") { copyUrl->click(); QCOMPARE(QApplication::clipboard()->text(), QString("https://example.test/history-result")); }
    auto *details = window->findChild<QPushButton *>("historyCaptureDetails"); QVERIFY(details->isEnabled()); details->click();
    auto *review = window->findChild<QDialog *>("retainedCaptureResultDialog"); QVERIFY(review); QVERIFY(review->isVisible()); review->reject();
    auto *forget = window->findChild<QPushButton *>("forgetHistoryCapture"); QTRY_VERIFY(forget->isEnabled()); forget->click();
    QPointer<QMessageBox> confirmation = window->findChild<QMessageBox *>("forgetHistoryCaptureDialog"); QVERIFY(confirmation);
    QCOMPARE(confirmation->defaultButton(), confirmation->button(QMessageBox::Cancel));
    confirmation->button(QMessageBox::Cancel)->click(); QTRY_VERIFY(!confirmation);
    QCOMPARE(list->count(), 1); QCOMPARE(storage->list().entries.front().bytes, bytes);
    forget->click(); confirmation = window->findChild<QMessageBox *>("forgetHistoryCaptureDialog"); QVERIFY(confirmation);
    confirmation->button(QMessageBox::Discard)->click(); QTRY_COMPARE(list->count(), 0);
    QVERIFY(storage->list().entries.empty());
  }

  void unreadableRecordsStayPreservedAndVisibleAcrossSearchAndPreviewUpdates() {
    QTemporaryDir dir; const auto directory = dir.filePath("state");
    auto storage = std::make_shared<NativePipelineJournal>(directory);
    const auto id = newId(), brokenId = newId();
    const auto path = writePng(dir.filePath("kept.png"), Qt::blue);
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, QDateTime::currentDateTimeUtc(), {png(path, id)}))).stored);
    QVERIFY(storage->replace(brokenId, {}, serializePipelineJournal(record(brokenId, QDateTime::currentDateTimeUtc(), {}))).stored);
    const auto corruptPath = directory + '/' + brokenId + ".json";
    const QByteArray corrupt("{broken history fixture");
    { QFile file(corruptPath); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(corrupt), qint64(corrupt.size())); }
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner);
    int reviews = 0; window->setRecordsOpener([&] { ++reviews; }); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    QTRY_VERIFY(window->findChild<QPushButton *>("openHistoryCapture")->isEnabled());
    auto *status = window->findChild<QLabel *>("captureHistoryWindowStatus");
    QVERIFY(status->text().contains("unreadable"));
    window->findChild<QLineEdit *>("captureHistorySearch")->setText("kept");
    QCOMPARE(list->count(), 1); QVERIFY(status->text().contains("unreadable"));
    window->findChild<QPushButton *>("openCaptureResultRecords")->click(); QCOMPARE(reviews, 1);
    // Corrupt records are set aside byte for byte instead of staying in the journal.
    QVERIFY(!QFile::exists(corruptPath));
    QFile file(corruptPath + ".corrupt"); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), corrupt);
    QVERIFY(QFile::exists(path)); QCOMPARE(storage->list().entries.size(), size_t(1));
  }

  void itemsKeepOnlyFilesTheirRunCreatedNewestFirst() {
    QTemporaryDir dir; const auto a = newId(), b = newId(), c = newId();
    const auto first = writePng(dir.filePath("first.png"), Qt::red), second = writePng(dir.filePath("second.png"), Qt::blue);
    const auto at = QDateTime::fromString("2026-10-20T08:00:00Z", Qt::ISODate);
    auto retry = record(b, at.addSecs(60), {png(first, a), png(second, b)}, "monitor");
    const auto items = captureHistoryItems(recovered({record(a, at, {png(first, a)}, "region"), retry, record(c, at.addSecs(120), {})}));
    QCOMPARE(items.size(), std::size_t(3));
    QCOMPARE(items[0].taskId, c); QVERIFY(items[0].files.empty());
    QCOMPARE(items[1].taskId, b); QCOMPARE(items[1].files.size(), std::size_t(1)); QCOMPARE(items[1].files[0].path, second);
    QCOMPARE(items[1].captureSource, QString("monitor")); QCOMPARE(items[1].workflowName, QString("Docs"));
    QCOMPARE(items[1].files[0].width, qint64(40));
    QCOMPARE(items[2].taskId, a); QCOMPARE(items[2].state, QString("completed"));
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
    QCOMPARE(int(historyPage(items, "Full screen", 1, &total).size()), 1); QCOMPARE(total, 1);
    QVERIFY(!matchesHistoryQuery(items[3], "Active window"));
    QCOMPARE(int(historyPage(items, "shot-20", 1).size()), 6);  // shot-20 and shot-200 to shot-204
    QVERIFY(matchesHistoryQuery(items[0], items[0].createdAt.toLocalTime().toString("yyyy-MM-dd")));
    QVERIFY(!matchesHistoryQuery(items[0], "nothing-like-this"));
  }
  void keysetPaginationIsStableWhileRecordsAreAdded() {
    std::vector<CaptureHistoryItem> items;
    const auto base = QDateTime::fromString("2026-10-21T10:00:00Z", Qt::ISODate);
    for (int i = 0; i < 25; ++i) {
      CaptureHistoryItem item; item.taskId = QStringLiteral("t%1").arg(i, 2, 10, QLatin1Char('0'));
      item.createdAt = base.addSecs(-(i / 2) * 10);  // pairs share a timestamp: tie-break by task ID
      item.files.push_back({QStringLiteral("/p/shot-%1.png").arg(i)});
      items.push_back(item);
    }
    auto first = historyPageAfter(items, {}, {}, 10);
    QCOMPARE(int(first.items.size()), 10); QCOMPARE(first.totalMatches, 25); QVERIFY(first.next.valid());
    // A newer capture arrives between page loads.
    CaptureHistoryItem fresh; fresh.taskId = "zz-new"; fresh.createdAt = base.addSecs(100); fresh.files.push_back({"/p/new.png"});
    items.insert(items.begin(), fresh);
    QStringList seen; for (const auto &i : first.items) seen << i.taskId;
    auto second = historyPageAfter(items, {}, first.next, 10);
    auto third = historyPageAfter(items, {}, second.next, 10);
    for (const auto &i : second.items) seen << i.taskId;
    for (const auto &i : third.items) seen << i.taskId;
    QVERIFY(!third.next.valid()); QCOMPARE(seen.size(), 25); QCOMPARE(QSet<QString>(seen.begin(), seen.end()).size(), 25);  // no repeat, none skipped
    QVERIFY(!seen.contains("zz-new"));
    QCOMPARE(first.items[0].taskId, QString("t00")); QCOMPARE(first.items[1].taskId, QString("t01"));
    // Bounded: an oversized limit is clamped; a filter applies before paging.
    std::vector<CaptureHistoryItem> many(kHistoryPageSize + 50, fresh);
    QCOMPARE(int(historyPageAfter(many, {}, {}, 100000).items.size()), kHistoryPageSize);
    QCOMPARE(historyPageAfter(items, "shot-2", {}, 3).totalMatches, 6);  // shot-2, shot-20 to shot-24
  }
  void overlongQueriesAreTruncatedNotUnbounded() {
    CaptureHistoryItem item; item.workflowName = "Docs"; item.files.push_back({"/p/a.png"});
    QVERIFY(matchesHistoryQuery(item, QString("docs") + QString(5000, ' ')));
    QVERIFY(!matchesHistoryQuery(item, QString(kHistoryMaxQueryLength, 'x') + "docs"));
    QVERIFY(matchesHistoryQuery(item, QString("docs") + QString(kHistoryMaxQueryLength, ' ') + "tail") == false);
  }
  void corruptAndTruncatedRecordsAreSetAsideWithBackupAndGoodRecordsSurvive() {
    QTemporaryDir dir; const auto state = dir.filePath("state"); auto storage = std::make_shared<NativePipelineJournal>(state);
    const auto good = newId(), bad1 = newId(), bad2 = newId(), future = newId();
    const auto file = writePng(dir.filePath("good.png"), Qt::green);
    const auto encoded = serializePipelineJournal(record(good, QDateTime::currentDateTimeUtc(), {png(file, good)}, "region"));
    QVERIFY(storage->replace(good, {}, encoded).stored);
    const auto put = [&](const QString &id, const QByteArray &bytes) {
      QFile f(state + "/" + id + ".json"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(bytes); f.close();
      QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)); };
    const auto truncated = encoded.left(encoded.size() / 2);
    put(bad1, truncated); put(bad2, "not json at all \x01");
    auto value = QJsonDocument::fromJson(encoded).object(); value.insert("version", 999); const auto futureBytes = QJsonDocument(value).toJson();
    put(future, futureBytes);
    const auto listing = storage->listRecovering();
    QCOMPARE(listing.quarantined, 2); QCOMPARE(listing.entries.size(), size_t(2));  // good + future-version kept
    QVERIFY(QFile::exists(state + "/" + bad1 + ".json.corrupt")); QVERIFY(QFile::exists(state + "/" + bad2 + ".json.corrupt"));
    QVERIFY(!QFile::exists(state + "/" + bad1 + ".json"));
    QFile backup(state + "/" + bad1 + ".json.corrupt"); QVERIFY(backup.open(QIODevice::ReadOnly)); QCOMPARE(backup.readAll(), truncated);
    QVERIFY(QFile::exists(state + "/" + future + ".json"));  // newer-version record untouched
    QCOMPARE(storage->list().entries.size(), size_t(2)); QCOMPARE(storage->listRecovering().quarantined, 0);
    // The same ID can be written again; an earlier backup is never overwritten.
    QVERIFY(storage->replace(bad1, {}, serializePipelineJournal(record(bad1, QDateTime::currentDateTimeUtc(), {png(file, bad1)}))).stored); put(bad1, QByteArray());
    const auto again = storage->listRecovering(); QCOMPARE(again.quarantined, 1);
    QVERIFY(QFile::exists(state + "/" + bad1 + ".json.corrupt.1"));
  }
  void windowRecoversCorruptStoreAndReportsAValueFreeCount() {
    QTemporaryDir dir; const auto state = dir.filePath("state"); auto storage = std::make_shared<NativePipelineJournal>(state);
    const auto good = newId(), bad = newId(); const auto file = writePng(dir.filePath("ok.png"), Qt::green);
    QVERIFY(storage->replace(good, {}, serializePipelineJournal(record(good, QDateTime::currentDateTimeUtc(), {png(file, good)}))).stored);
    { QFile f(state + "/" + bad + ".json"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("{\"secret-value\": "); f.close(); QFile::setPermissions(f.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner); }
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    QVERIFY(QFile::exists(state + "/" + bad + ".json.corrupt"));
    bool reported = false;
    for (auto *label : window->findChildren<QLabel *>()) if (label->text().contains("1 unreadable history record")) { reported = true; QVERIFY(!label->text().contains("secret-value")); }
    QVERIFY(reported);
  }
  void windowSearchFieldIsBoundedAndLoadMorePages() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto file = writePng(dir.filePath("p.png"), Qt::green); const auto now = QDateTime::currentDateTimeUtc();
    for (int i = 0; i < kHistoryPageSize + 3; ++i) { const auto id = newId();
      QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, now.addSecs(-i), {png(file, id)}))).stored); }
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), kHistoryPageSize);
    auto *search = window->findChild<QLineEdit *>("captureHistorySearch");
    QCOMPARE(search->maxLength(), kHistoryMaxQueryLength); QVERIFY(!search->accessibleName().isEmpty());
    auto *more = window->findChild<QPushButton *>("moreCaptureHistory"); QVERIFY(more->isVisible()); QVERIFY(!more->accessibleName().isEmpty() || !more->text().isEmpty());
    more->click(); QCOMPARE(list->count(), kHistoryPageSize + 3); QVERIFY(!more->isVisible());
    search->setText("no-such-thing"); QCOMPARE(list->count(), 0);
  }
  void nativePagingAndSearchBoundResultOnlyRowsAndKeepSelection() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto at = QDateTime::currentDateTimeUtc(); QString newest;
    for (int i = 0; i < kHistoryPageSize + 1; ++i) {
      newest = newId();
      QVERIFY(storage->replace(newest, {}, serializePipelineJournal(record(newest, at.addSecs(i), {}, "region"))).stored);
    }
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), kHistoryPageSize);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), newest); list->setCurrentItem(list->item(0));
    auto *more = window->findChild<QPushButton *>("moreCaptureHistory"); QVERIFY(more->isVisible()); more->click();
    QCOMPARE(list->count(), kHistoryPageSize + 1); QVERIFY(!more->isVisible());
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), newest);
    auto *search = window->findChild<QLineEdit *>("captureHistorySearch"); search->setText(newest);
    QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), newest);
    search->clear(); QCOMPARE(list->count(), kHistoryPageSize); QVERIFY(more->isVisible());
    QCOMPARE(storage->list().entries.size(), size_t(kHistoryPageSize + 1));
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
  void uploadUrlIsVisibleAndCanBeCopiedFromHistory() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("uploaded.png"), Qt::green); const auto id = newId();
    auto snapshot = record(id, QDateTime::currentDateTimeUtc(), {png(path, id)});
    snapshot.result.upload = UploadResult{"https://example.test/image", "https://example.test/thumb", "https://example.test/delete", {}};
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(snapshot)).stored);
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner); window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    QVERIFY(list->item(0)->text().contains("https://example.test/image"));
    auto *copy = window->findChild<QPushButton *>("copyHistoryUrl"); QVERIFY(copy); QVERIFY(copy->isEnabled());
    QApplication::clipboard()->setText("before"); copy->click(); QCOMPARE(QApplication::clipboard()->text(), QString("https://example.test/image"));
  }
  void uploadUrlActionsOpenTheUrlAndCopyTheDeletionUrl() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("receipt.png"), Qt::green); const auto id = newId();
    auto snapshot = record(id, QDateTime::currentDateTimeUtc(), {png(path, id)});
    snapshot.result.upload = UploadResult{"https://example.test/image", "https://example.test/thumb", "https://example.test/delete", {}};
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(snapshot)).stored);
    QList<QUrl> opened; QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner);
    window->setUrlOpener([&opened](const QUrl &url) { opened << url; return true; });
    window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    auto *openUrl = window->findChild<QPushButton *>("openHistoryUrl");
    auto *copyDeletion = window->findChild<QPushButton *>("copyHistoryDeletionUrl");
    QVERIFY(openUrl); QVERIFY(copyDeletion); QTRY_VERIFY(openUrl->isEnabled()); QTRY_VERIFY(copyDeletion->isEnabled());
    QApplication::clipboard()->setText("before"); copyDeletion->click();
    QCOMPARE(QApplication::clipboard()->text(), QString("https://example.test/delete"));
    openUrl->click(); QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first().toString(), QString("https://example.test/image"));
  }
  void uploadUrlActionsAreDisabledWithoutAnUpload() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("local.png"), Qt::green); const auto id = newId();
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(record(id, QDateTime::currentDateTimeUtc(), {png(path, id)}))).stored);
    QList<QUrl> opened; QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner);
    window->setUrlOpener([&opened](const QUrl &url) { opened << url; return true; });
    window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    auto *openUrl = window->findChild<QPushButton *>("openHistoryUrl");
    auto *copyDeletion = window->findChild<QPushButton *>("copyHistoryDeletionUrl");
    QVERIFY(openUrl); QVERIFY(copyDeletion); QTRY_VERIFY(!openUrl->isEnabled()); QTRY_VERIFY(!copyDeletion->isEnabled());
    QVERIFY(!window->findChild<QPushButton *>("copyHistoryUrl")->isEnabled());
  }
  void openUrlRefusesANonWebUploadUrl() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto path = writePng(dir.filePath("script.png"), Qt::green); const auto id = newId();
    auto snapshot = record(id, QDateTime::currentDateTimeUtc(), {png(path, id)});
    snapshot.result.upload = UploadResult{"javascript:alert(1)", {}, {}, {}};
    QVERIFY(storage->replace(id, {}, serializePipelineJournal(snapshot)).stored);
    QList<QUrl> opened; QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, {}, &owner);
    window->setUrlOpener([&opened](const QUrl &url) { opened << url; return true; });
    window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 1);
    list->setCurrentItem(list->item(0));
    auto *openUrl = window->findChild<QPushButton *>("openHistoryUrl"); QVERIFY(openUrl); QTRY_VERIFY(openUrl->isEnabled());
    openUrl->click(); QVERIFY(opened.isEmpty());
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
  void visibleSourceLabelUrlAndFailureAreSearchableWithoutTheDeletionUrl() {
    QTemporaryDir dir; auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    const auto fullPath = writePng(dir.filePath("full.png"), Qt::green);
    const auto activePath = writePng(dir.filePath("active.png"), Qt::blue);
    const auto lastPath = writePng(dir.filePath("last.png"), Qt::red);
    const auto fullId = newId(), activeId = newId(), lastId = newId();
    const auto at = QDateTime::fromString("2026-10-21T10:30:00Z", Qt::ISODate);
    auto full = record(fullId, at, {png(fullPath, fullId)}, "full-screen");
    full.result.upload = UploadResult{"https://example.test/public-image", "https://example.test/thumb-only",
                                      "https://example.test/delete-secret-token-9f3a", "the relay refused the transfer"};
    QVERIFY(storage->replace(fullId, {}, serializePipelineJournal(full)).stored);
    QVERIFY(storage->replace(activeId, {}, serializePipelineJournal(record(activeId, at.addSecs(120), {png(activePath, activeId)}, "active-window"))).stored);
    QVERIFY(storage->replace(lastId, {}, serializePipelineJournal(record(lastId, at.addSecs(240), {png(lastPath, lastId)}, "last-region"))).stored);
    const auto indexed = captureHistoryItems(recovered({full}));
    QCOMPARE(indexed.size(), std::size_t(1));
    QCOMPARE(indexed[0].resultUrl, QString("https://example.test/public-image"));
    QCOMPARE(indexed[0].failureMessage, QString("the relay refused the transfer"));
    QVERIFY(indexed[0].resultUrl.contains("public-image"));
    QVERIFY(!indexed[0].resultUrl.contains("delete-secret-token-9f3a"));
    QVERIFY(matchesHistoryQuery(indexed[0], "Full screen"));
    QVERIFY(matchesHistoryQuery(indexed[0], "public-image"));
    QVERIFY(matchesHistoryQuery(indexed[0], "relay refused"));
    QVERIFY(matchesHistoryQuery(indexed[0], indexed[0].createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm")));
    QVERIFY(!matchesHistoryQuery(indexed[0], "delete-secret-token-9f3a"));
    QVERIFY(!matchesHistoryQuery(indexed[0], "thumb-only"));
    CaptureHistoryItem translated; translated.captureSource = "active-window"; translated.sourceLabel = "Fenêtre active";
    QVERIFY(matchesHistoryQuery(translated, "Fenêtre active"));
    QVERIFY(matchesHistoryQuery(translated, "Active window"));
    QString edited;
    QWidget owner; auto *window = new CaptureHistoryWindow(storage, [] { return false; }, [&](const QString &path, QWidget *) { edited = path; }, &owner);
    window->show();
    auto *list = window->findChild<QListWidget *>("captureHistoryItems"); QTRY_COMPARE(list->count(), 3);
    auto *fullRow = find(list, "full.png"); QVERIFY(fullRow);
    QVERIFY(fullRow->text().contains("Full screen"));
    QVERIFY(fullRow->text().contains("https://example.test/public-image"));
    QVERIFY(fullRow->text().contains("the relay refused the transfer"));
    QVERIFY(!fullRow->text().contains("delete-secret-token-9f3a"));
    QVERIFY(!fullRow->text().contains("thumb-only"));
    auto *lastRow = find(list, "last.png"); QVERIFY(lastRow);
    QVERIFY(lastRow->text().contains("Last region"));
    QVERIFY(!lastRow->text().contains("last-region"));
    auto *activeRow = find(list, "active.png"); QVERIFY(activeRow);
    QVERIFY(activeRow->text().contains("Active window"));
    auto *search = window->findChild<QLineEdit *>("captureHistorySearch");
    search->setText("Full screen"); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), fullId);
    list->setCurrentItem(list->item(0));
    auto *edit = window->findChild<QPushButton *>("editHistoryCapture"); QTRY_VERIFY(edit->isEnabled());
    edit->click(); QCOMPARE(edited, fullPath);
    const auto shownTime = list->item(0)->text().section('\n', 1, 1);
    search->setText(shownTime); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), fullId);
    search->setText("Active window"); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), activeId);
    search->setText("Last region"); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), lastId);
    search->setText("public-image"); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), fullId);
    search->setText("relay refused"); QCOMPARE(list->count(), 1);
    search->setText("delete-secret-token-9f3a"); QCOMPARE(list->count(), 0);
    search->setText("thumb-only"); QCOMPARE(list->count(), 0);
    search->clear(); QCOMPARE(list->count(), 3);
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
