#include "history/LegacyHistory.h"
#include <QTest>
using namespace xerahs::history;
namespace {
QByteArray item(const QByteArray &name = "capture.png", const QByteArray &extra = {}) {
  return "<HistoryItem><Filename>" + name + "</Filename><Filepath>/missing/image.png</Filepath>"
      "<DateTimeUtc>2026-08-30T12:34:56.1234567Z</DateTimeUtc><Type>Image</Type>"
      "<Host>local</Host><URL>https://example.invalid/?a=1&amp;b=2</URL>" + extra + "</HistoryItem>";
}
}
class LegacyHistoryXmlTest final : public QObject {
  Q_OBJECT
private slots:
  void appendOnlyFragmentsKeepUnicodeTimestampsUnknownFieldsAndOrdinals() {
    const auto bytes = QByteArray::fromHex("efbbbf") + item("caf\xc3\xa9.png", "<Future label='retained'>value</Future>") +
        QByteArray::fromHex("efbbbf") + item("second.png");
    const auto result = previewLegacyHistory(bytes);
    QVERIFY2(result.ok(), qPrintable(result.code)); QCOMPARE(result.format, LegacyHistoryFormat::Xml);
    QCOMPARE(result.records.size(), size_t(2)); QVERIFY(result.quarantine.isEmpty());
    QCOMPARE(result.records[0].fields.value("FileName").toString(), QString::fromUtf8("caf\xc3\xa9.png"));
    QCOMPARE(result.records[0].fields.value("DateTime").toString(), QString("2026-08-30T12:34:56.1234567Z"));
    QCOMPARE(result.records[0].fields.value("URL").toString(), QString("https://example.invalid/?a=1&b=2"));
    QVERIFY(result.records[0].fields.contains("LegacyXml")); QVERIFY(!result.records[0].fields.contains("Id"));
    QCOMPARE(result.records[1].index, qsizetype(1));
    QVERIFY(previewLegacyHistory("<?xml version='1.0' encoding='UTF-8'?>" + item()).ok());
  }
  void invalidAndUnsafeRecordsDoNotDropTheirValidNeighbours() {
    const auto result = previewLegacyHistory(item("first.png") + item("bad.png", "<Filename>duplicate.png</Filename>") +
        item("unsafe.png", "<Command>never execute</Command>") + item("last.png"));
    QVERIFY(result.ok()); QCOMPARE(result.records.size(), size_t(2)); QCOMPARE(result.quarantine.size(), 2);
    QCOMPARE(result.records.back().index, qsizetype(3));
    QVERIFY(!result.report.detailedSummary().contains("never execute"));
  }
  void malformedOrExecutableXmlNeverProducesAUsablePreview_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("dtd") << QByteArray("<!DOCTYPE HistoryItem [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>") + item("&x;");
    QTest::newRow("pi") << QByteArray("<?run command?>") + item();
    QTest::newRow("broken") << item() + "<HistoryItem>";
    QTest::newRow("encoding") << QByteArray("<?xml version='1.0' encoding='UTF-16'?>") + item();
    QTest::newRow("utf8") << item(QByteArray("\xff"));
    QTest::newRow("deep") << QByteArray("<a>").repeated(65) + item() + QByteArray("</a>").repeated(65);
    QTest::newRow("large") << QByteArray(kLegacyHistoryMaxBytes + 1, '<');
  }
  void malformedOrExecutableXmlNeverProducesAUsablePreview() {
    QFETCH(QByteArray, bytes); const auto result = previewLegacyHistory(bytes);
    QVERIFY(!result.ok()); QVERIFY(result.records.empty()); QVERIFY(result.quarantine.isEmpty());
  }
};
QTEST_GUILESS_MAIN(LegacyHistoryXmlTest)
#include "LegacyHistoryXmlTest.moc"
