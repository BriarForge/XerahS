#include "history/LegacyHistory.h"
#include "LegacyHistoryWindow.h"
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
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
}
class LegacyHistoryTest final : public QObject {
  Q_OBJECT
private slots:
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
    auto *choose = destroyed->findChild<QPushButton *>("chooseLegacyHistory"); choose->click(); QVERIFY(destroyed.isNull());
    QTemporaryDir dir; const auto source = dir.filePath("History.json"); save(source, fragment({record()}));
    QPointer<app::LegacyHistoryWindow> closed = new app::LegacyHistoryWindow(nullptr, [&](QWidget *) { return source; });
    closed->findChild<QPushButton *>("chooseLegacyHistory")->click(); closed->close();
    QTRY_VERIFY(closed.isNull()); QTest::qWait(30); QCOMPARE(read(source), fragment({record()}));
  }
};
QTEST_MAIN(LegacyHistoryTest)
#include "LegacyHistoryTest.moc"
