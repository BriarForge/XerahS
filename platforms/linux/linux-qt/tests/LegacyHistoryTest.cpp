#include "history/LegacyHistory.h"
#include "LegacyHistoryWindow.h"
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <sys/stat.h>
#include <sqlite3.h>
using namespace xerahs;
namespace {
QJsonObject record(int index = 0) {
  return {{"FileName", QString("image-%1.png").arg(index)}, {"FilePath", "/not-read/missing.png"},
      {"URL", "javascript:must-remain-text"}, {"DateTime", "2026-08-30T12:34:56+10:00"},
      {"Tags", QJsonObject{{"Errors", "private failure"}}}};
}
QByteArray fragment(const QJsonArray &records) {
  auto bytes = QJsonDocument(records).toJson(QJsonDocument::Compact);
  return bytes.mid(1, bytes.size() - 2);
}
void save(const QString &path, const QByteArray &bytes) {
  QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size());
}
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
void confirm(QPushButton *action, QMessageBox::StandardButton answer) {
  bool shown = false; QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (!box) return;
    shown = true; QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); box->button(answer)->click();
  });
  timer.start(2); action->click(); timer.stop(); QVERIFY(shown);
}
}
class LegacyHistoryTest final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    static QTemporaryDir state;
    qputenv("XDG_STATE_HOME", state.path().toUtf8());
  }
  void pinnedSourceFixtureKeepsAllFieldsAndMissingReferences() {
    QFile fixture(QStringLiteral(LEGACY_HISTORY_FIXTURE)); QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto bytes = fixture.readAll(); const auto result = history::previewLegacyHistory(bytes);
    QVERIFY(result.ok()); QCOMPARE(result.records.size(), size_t(2)); QVERIFY(result.quarantine.isEmpty());
    const auto originals = QJsonDocument::fromJson('[' + bytes + ']').array();
    QCOMPARE(result.records[0].fields, originals[0].toObject()); QCOMPARE(result.records[1].fields, originals[1].toObject());
    QCOMPARE(result.records[0].fields.value("DateTime").toString(), QString("2026-08-30T12:34:56+10:00"));
    QCOMPARE(result.records[1].fields.value("DateTime").toString(), QString("2026-08-29T01:02:03"));
    QVERIFY(!result.records[0].fields.contains("Id")); QVERIFY(!result.records[0].fields.contains("state"));
  }
  void corruptAndAuthorityRecordsStayQuarantinedWithoutLosingNeighbours() {
    auto first = record(1), last = record(2), corrupt = record(3), authority = record(4);
    first.insert("FutureMetadata", QJsonObject{{"label", "preserved"}});
    corrupt.insert("DateTime", "invalid-private-date"); authority.insert("$type", "private-runtime-type");
    const auto result = history::previewLegacyHistory(fragment({first, corrupt, authority, 5, last}));
    QVERIFY(result.ok()); QCOMPARE(result.records.size(), size_t(2)); QCOMPARE(result.quarantine.size(), 3);
    QCOMPARE(result.records[0].index, qsizetype(0)); QCOMPARE(result.records[0].fields, first);
    QCOMPARE(result.records[1].index, qsizetype(4)); QCOMPARE(result.records[1].fields, last);
    QCOMPARE(result.quarantine[0].toObject().value("record").toObject(), corrupt);
    QCOMPARE(result.quarantine[1].toObject().value("record").toObject(), authority);
    const auto summary = result.report.detailedSummary();
    QVERIFY(summary.contains("record 1")); QVERIFY(summary.contains("record 2"));
    QVERIFY(!summary.contains("private")); QVERIFY(!summary.contains("image-")); QVERIFY(!summary.contains("not-read"));
  }
  void malformedAndBoundedInputsFailWithoutPreviewingRecords_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<QString>("code");
    QTest::newRow("duplicate-key") << QByteArray("{\"FileName\":\"one\",\"FileName\":\"two\"}") << QString("legacy-history-duplicate-key");
    QTest::newRow("utf8") << QByteArray("\xff") << QString("legacy-history-invalid-encoding");
    QTest::newRow("syntax") << QByteArray("{broken}") << QString("legacy-history-invalid-json");
    QTest::newRow("size") << QByteArray(history::kLegacyHistoryMaxBytes + 1, ' ') << QString("legacy-history-too-large");
    QTest::newRow("depth") << QByteArray(65, '[') + "0" + QByteArray(65, ']') << QString("legacy-history-too-deep");
  }
  void malformedAndBoundedInputsFailWithoutPreviewingRecords() {
    QFETCH(QByteArray, bytes); QFETCH(QString, code);
    const auto result = history::previewLegacyHistory(bytes);
    QVERIFY(!result.ok()); QCOMPARE(result.code, code); QVERIFY(result.records.empty()); QVERIFY(result.quarantine.isEmpty());
  }
  void nativeChoosePagesRealRecordsAndNeverChangesFilesOrOpensReferences() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); QJsonArray records;
    for (int i = 0; i < 201; ++i) records.append(record(i));
    const auto bytes = fragment(records);
    const auto source = dir.filePath("History.json");
    const auto live = dir.filePath("live-history.json"); save(source, bytes); save(live, "live history untouched");
    bool cancel = false;
    QPointer<app::LegacyHistoryWindow> window = new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return cancel ? QString{} : source; });
    window->show(); auto *choose = window->findChild<QPushButton *>("chooseLegacyHistory");
    auto *table = window->findChild<QTableWidget *>("legacyHistoryRecords");
    auto *status = window->findChild<QLabel *>("legacyHistoryStatus"); QVERIFY(choose && table && status);
    QTRY_VERIFY(choose->isEnabled());
    QTest::mouseClick(choose, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(choose->isEnabled(), 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(table->rowCount() == 200, qPrintable(status->text()), 5000);
    QCOMPARE(table->item(0, 0)->text(), QString("1"));
    QCOMPARE(table->item(0, 6)->text(), QString("javascript:must-remain-text"));
    QCOMPARE(status->textFormat(), Qt::PlainText); QVERIFY(status->text().contains("no task IDs or operation states"));
    QTest::mouseClick(window->findChild<QPushButton *>("nextLegacyHistory"), Qt::LeftButton);
    QCOMPARE(table->rowCount(), 1); QCOMPARE(table->item(0, 0)->text(), QString("201"));
    QTest::mouseClick(window->findChild<QPushButton *>("previousLegacyHistory"), Qt::LeftButton); QCOMPARE(table->rowCount(), 200);
    cancel = true; QTest::mouseClick(choose, Qt::LeftButton); QCOMPARE(table->rowCount(), 200);
    QCOMPARE(read(source), bytes); QCOMPARE(read(live), QByteArray("live history untouched"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 2);
    window->close(); QTRY_VERIFY(window.isNull());
  }
  void chooserCanDestroyItsOwnerAndCloseAbandonsPendingRead() {
    QPointer<app::LegacyHistoryWindow> destroyed;
    destroyed = new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { delete destroyed.data(); return QString("ignored"); });
    auto *choose = destroyed->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled()); choose->click(); QVERIFY(destroyed.isNull());
    QTemporaryDir dir; const auto source = dir.filePath("History.json"); save(source, fragment({record()}));
    QPointer<app::LegacyHistoryWindow> closed = new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return source; });
    QTRY_VERIFY(closed->findChild<QPushButton *>("chooseLegacyHistory")->isEnabled());
    closed->findChild<QPushButton *>("chooseLegacyHistory")->click(); closed->close();
    QTRY_VERIFY(closed.isNull()); QTest::qWait(30); QCOMPARE(read(source), fragment({record()}));
  }
  void nativeXmlAndSqliteImportRestartUndoAndRecovery_data() {
    QTest::addColumn<bool>("sqlite"); QTest::newRow("xml") << false; QTest::newRow("sqlite") << true;
  }
  void nativeXmlAndSqliteImportRestartUndoAndRecovery() {
    QFETCH(bool, sqlite); QTemporaryDir dir; const auto source = dir.filePath(sqlite ? "History.db" : "History.xml");
    const auto archive = dir.filePath("archive.json");
    if (sqlite) {
      sqlite3 *db = nullptr; QCOMPARE(sqlite3_open(QFile::encodeName(source).constData(), &db), SQLITE_OK);
      const int status = sqlite3_exec(db,
          "CREATE TABLE History(Id INTEGER PRIMARY KEY AUTOINCREMENT,FileName TEXT,FilePath TEXT,DateTime TEXT,Type TEXT,Host TEXT,URL TEXT,ThumbnailURL TEXT,DeletionURL TEXT,ShortenedURL TEXT,Tags TEXT);"
          "INSERT INTO History VALUES(9007199254740993,'reviewed.png','/missing/reviewed.png','2026-08-30T12:34:56.1234567Z','Image',NULL,'javascript:inert',NULL,NULL,NULL,'{}')", nullptr, nullptr, nullptr);
      sqlite3_close(db); QCOMPARE(status, SQLITE_OK);
    } else save(source, "<HistoryItem><Filename>reviewed.png</Filename><Filepath>/missing/reviewed.png</Filepath><DateTimeUtc>2026-08-30T12:34:56.1234567Z</DateTimeUtc><URL>javascript:inert</URL></HistoryItem>");
    const auto original = read(source);
    const auto open = [&] { return new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return source; }, archive); };
    QPointer<app::LegacyHistoryWindow> window = open(); window->show();
    auto *choose = window->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled()); choose->click();
    auto *table = window->findChild<QTableWidget *>("legacyHistoryRecords"); QTRY_COMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 6)->text(), QString("javascript:inert"));
    if (sqlite) QCOMPARE(table->item(0, 7)->text(), QString("9007199254740993"));
    auto *accept = window->findChild<QPushButton *>("importLegacyHistory");
    confirm(accept, QMessageBox::Cancel); QVERIFY(!QFileInfo::exists(archive));
    confirm(accept, QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(read(source), original);
    window->close(); QTRY_VERIFY(window.isNull());
    window = open(); window->show(); choose = window->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled());
    table = window->findChild<QTableWidget *>("legacyHistoryRecords"); QCOMPARE(table->rowCount(), 1);
    if (sqlite) QCOMPARE(table->item(0, 7)->text(), QString("9007199254740993"));
    confirm(window->findChild<QPushButton *>("undoLegacyHistory"), QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 0);
    QVERIFY(QFile::remove(source));
    window->findChild<QPushButton *>("recoverLegacyHistory")->click(); QTRY_VERIFY(choose->isEnabled()); QTRY_COMPARE(table->rowCount(), 1);
    confirm(window->findChild<QPushButton *>("importLegacyHistory"), QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled());
    QCOMPARE(table->rowCount(), 1); QVERIFY(!QFileInfo::exists(source));
    window->close(); QTRY_VERIFY(window.isNull());
  }
  void nativeImportCancelRestartUndoAndRecoveryKeepTheReviewedSnapshot() {
    QTemporaryDir dir; const auto source = dir.filePath("History.json"), archive = dir.filePath("imported.json"), live = dir.filePath("live-history.json");
    const auto original = fragment({record(1), 17, record(2)}); save(source, original); save(live, "native history unchanged");
    const auto open = [&] { return new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return source; }, archive); };
    QPointer<app::LegacyHistoryWindow> window = open(); window->show();
    auto *choose = window->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled()); choose->click();
    auto *table = window->findChild<QTableWidget *>("legacyHistoryRecords"); QTRY_COMPARE(table->rowCount(), 2);
    auto *accept = window->findChild<QPushButton *>("importLegacyHistory"); QVERIFY(accept->isEnabled());
    confirm(accept, QMessageBox::Cancel); QVERIFY(!QFileInfo::exists(archive)); QCOMPARE(read(source), original);
    // A concurrent source replacement is left untouched: Import saves exactly what was reviewed.
    const auto replacement = fragment({record(3)}); save(source, replacement);
    confirm(accept, QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled());
    QCOMPARE(table->rowCount(), 2); QCOMPARE(read(source), replacement); QCOMPARE(read(live), QByteArray("native history unchanged"));
    const auto stored = QJsonDocument::fromJson(read(archive)).object().value("imports").toArray().first().toObject();
    QCOMPARE(QByteArray::fromBase64(stored.value("source").toString().toLatin1()), original);
    window->close(); QTRY_VERIFY(window.isNull());
    window = open(); window->show(); choose = window->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled());
    table = window->findChild<QTableWidget *>("legacyHistoryRecords"); QCOMPARE(table->rowCount(), 2);
    QVERIFY(!window->findChild<QPushButton *>("importLegacyHistory")->isEnabled());
    choose->click(); QTRY_COMPARE(table->rowCount(), 1);
    confirm(window->findChild<QPushButton *>("importLegacyHistory"), QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 3);
    confirm(window->findChild<QPushButton *>("undoLegacyHistory"), QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 2);
    confirm(window->findChild<QPushButton *>("undoLegacyHistory"), QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 0);
    QVERIFY(QFile::remove(source));
    auto *recover = window->findChild<QPushButton *>("recoverLegacyHistory"); QVERIFY(recover->isEnabled()); recover->click();
    QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 2);
    confirm(window->findChild<QPushButton *>("importLegacyHistory"), QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 2);
    QVERIFY(!QFileInfo::exists(source)); QCOMPARE(read(live), QByteArray("native history unchanged"));
    auto *remove = window->findChild<QPushButton *>("removeLegacyHistoryRecovery"); QVERIFY(remove->isEnabled());
    const auto beforeRemoval = read(archive); confirm(remove, QMessageBox::Cancel); QCOMPARE(read(archive), beforeRemoval);
    confirm(remove, QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled()); QCOMPARE(table->rowCount(), 2);
    QVERIFY(!window->findChild<QPushButton *>("recoverLegacyHistory")->isEnabled());
    QCOMPARE(QJsonDocument::fromJson(read(archive)).object().value("imports").toArray().size(), 1);
    window->close(); QTRY_VERIFY(window.isNull());
    QCOMPARE(history::LegacyHistoryArchive(archive).read().preview.records.size(), size_t(2));
  }
  void nativeImportRefusesAnArchiveChangedSinceReviewThenReloadsAndRetries() {
    QTemporaryDir dir; const auto source = dir.filePath("History.json"), archive = dir.filePath("imported.json");
    save(source, fragment({record(1)}));
    QPointer<app::LegacyHistoryWindow> window = new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return source; }, archive);
    window->show(); auto *choose = window->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled()); choose->click();
    auto *accept = window->findChild<QPushButton *>("importLegacyHistory"); QTRY_VERIFY(accept->isEnabled());
    history::LegacyHistoryArchive other(archive); QCOMPARE(other.importSnapshot(fragment({record(2)}), {}, true), QString());
    const auto concurrent = read(archive);
    confirm(accept, QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled());
    QVERIFY(window->findChild<QLabel *>("legacyHistoryStatus")->text().contains("legacy-history-storage-changed")); QCOMPARE(read(archive), concurrent);
    window->findChild<QPushButton *>("savedLegacyHistory")->click(); QTRY_VERIFY(choose->isEnabled());
    choose->click(); QTRY_VERIFY(accept->isEnabled()); confirm(accept, QMessageBox::Yes); QTRY_VERIFY(choose->isEnabled());
    QCOMPARE(other.read().preview.records.size(), size_t(2)); window->close(); QTRY_VERIFY(window.isNull());
  }
  void selectedSpecialFileCannotStallTheReader() {
    QTemporaryDir dir; const auto path = dir.filePath("History.json"), archive = dir.filePath("archive.json");
    QCOMPARE(::mkfifo(QFile::encodeName(path).constData(), 0600), 0);
    QPointer<app::LegacyHistoryWindow> window = new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return path; }, archive);
    auto *choose = window->findChild<QPushButton *>("chooseLegacyHistory"); QTRY_VERIFY(choose->isEnabled()); choose->click();
    QTRY_VERIFY(choose->isEnabled()); QVERIFY(window->findChild<QLabel *>("legacyHistoryStatus")->text().contains("legacy-history-unreadable"));
    QVERIFY(!QFileInfo::exists(archive)); window->close(); QTRY_VERIFY(window.isNull());
  }
};
QTEST_MAIN(LegacyHistoryTest)
#include "LegacyHistoryTest.moc"
