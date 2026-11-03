#include "storage/Compatibility.h"
#include "storage/JsonBoundary.h"
#include "storage/PrivateJsonFile.h"
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::compatibility;
using namespace xerahs::storage;
class JsonBoundaryTest final : public QObject {
  Q_OBJECT
private slots:
  void adversarial_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<QString>("code");
    QTest::newRow("malformed") << QByteArray("{untrusted") << QString("invalid-json");
    QTest::newRow("duplicate-key") << QByteArray("{\"value\":1,\"value\":2}") << QString("duplicate-key");
    QTest::newRow("escaped-duplicate-key") << QByteArray("{\"value\":1,\"\\u0076alue\":2}") << QString("duplicate-key");
    QTest::newRow("nested-duplicate-key") << QByteArray("{\"child\":{\"a\":1,\"a\":2}}") << QString("duplicate-key");
    QTest::newRow("deep") << QByteArray(65, '[') + "0" + QByteArray(65, ']') << QString("too-deep");
    QTest::newRow("oversized") << QByteArray(1048577, ' ') << QString("too-large");
    QTest::newRow("elements") << QByteArray("[") + QByteArray("0,").repeated(100000) + "0]" << QString("too-many-elements");
    QTest::newRow("invalid-utf8") << QByteArray("{\"x\":\"") + char(0xff) + "\"}" << QString("invalid-encoding");
    QTest::newRow("overlong-utf8") << QByteArray("{\"x\":\"") + char(0xc0) + char(0xaf) + "\"}" << QString("invalid-encoding");
    QTest::newRow("truncated-utf8") << QByteArray("{\"x\":\"") + char(0xe2) + char(0x82) << QString("invalid-encoding");
    QTest::newRow("lone-high-surrogate") << QByteArray("{\"x\":\"\\ud800\"}") << QString("invalid-encoding");
    QTest::newRow("lone-low-surrogate") << QByteArray("{\"x\":\"\\udfff\"}") << QString("invalid-encoding");
    QTest::newRow("wrong-surrogate-pair") << QByteArray("{\"x\":\"\\ud800\\u0061\"}") << QString("invalid-encoding");
    QTest::newRow("concatenated-json") << QByteArray("{}{}") << QString("invalid-json");
    QTest::newRow("compressed-json") << QByteArray::fromHex("1f8b0800000000000003") << QString("invalid-encoding");
  }
  void adversarial() {
    QFETCH(QByteArray, bytes); QFETCH(QString, code);
    const auto result = readJson(bytes); QCOMPARE(result.code, code); QVERIFY(result.document.isNull());
    QTemporaryDir directory; const auto path = directory.filePath("state.json");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
    PrivateJsonFile store(path, {"test", 1}); const auto read = store.read();
    QVERIFY(read.status == StoreStatus::Invalid || read.status == StoreStatus::TooLarge);
    if (read.status == StoreStatus::Invalid) QCOMPARE(read.diagnostic, code);
    QByteArray snapshot("unchanged");
    QCOMPARE(store.write({}, 1, read.snapshot, &snapshot), read.status);
    QCOMPARE(snapshot, QByteArray("unchanged"));
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
  }
  void lexicalStringsAndBounds() {
    const QByteArray strings = "{\"a\":{\"same\":1},\"b\":{\"same\":2},\"text\":\"{\\\"a\\\":1}\",\"emoji\":\"\\ud83d\\ude00\"}";
    QVERIFY(readJson(strings).ok());
    QVERIFY(readJson("{}", {2, 1, 1}).ok());
    QCOMPARE(readJson("{}", {1, 1, 1}).code, QString("too-large"));
    QCOMPARE(readJson("{}", {2, 0, 1}).code, QString("too-deep"));
    QCOMPARE(readJson("{}", {2, 1, 0}).code, QString("too-many-elements"));
    QVERIFY(readJson("[1]", {3, 1, 2}).ok());
    QCOMPARE(readJson("[1]", {3, 1, 1}).code, QString("too-many-elements"));
    // Multi-byte sequences can cross the UTF-8 validation chunk boundary.
    const QByteArray longText = "{\"x\":\"" + QByteArray(4089, 'x') + QString::fromUtf8("😀").toUtf8() + "\"}";
    QVERIFY(readJson(longText).ok());
  }
  void localeAndNewlineVariants() {
    const auto previous = QLocale();
    const QByteArray lf = "{\n\"number\":1.25,\n\"name\":\"caf\\u00e9\",\n\"path\":\"assets/image.png\"\n}";
    const auto expected = readJson(lf).document;
    for (const auto &locale : {QLocale::c(), QLocale(QLocale::German), QLocale(QLocale::Turkish)}) {
      QLocale::setDefault(locale);
      QCOMPARE(readJson(lf).document, expected);
      auto crlf = lf; crlf.replace("\n", "\r\n"); QCOMPARE(readJson(crlf).document, expected);
      QCOMPARE(expected.object().value("name").toString(), QString::fromUtf8("café"));
      QCOMPARE(expected.object().value("number").toDouble(), 1.25);
      QVERIFY(!QJsonDocument(expected).toJson().contains('\r'));
    }
    QLocale::setDefault(previous);
  }
  void reportsNeverIncludeInput() {
    ImportReport report;
    for (int category = 0; category < 6; ++category) report.add(static_cast<ImportDisposition>(category), category + 1);
    QCOMPARE(report.summary(), QString("Retained: 1; transformed: 2; defaulted: 3; quarantined: 4; rejected: 5; ignored: 6"));
    QVERIFY(containsUnsafeUnknown(QJsonObject{{"future", QJsonObject{{"consentGranted", true}}}}));
    QVERIFY(containsUnsafeUnknown(QJsonObject{{"future", QJsonArray{QJsonObject{{"password", "not-reported"}}}}}));
    QVERIFY(!containsUnsafeUnknown(QJsonObject{{"future", QJsonObject{{"author", "safe"}, {"label", "safe"}}}}));
    QVERIFY(unsafeUnknown("$ref")); QVERIFY(unsafeUnknown("RequiredFeatures"));
  }
};
QTEST_GUILESS_MAIN(JsonBoundaryTest)
#include "JsonBoundaryTest.moc"
