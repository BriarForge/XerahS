#include "image-editor/AnnotationDocument.h"
#include <QJsonDocument>
#include <QLocale>
#include <QTest>
#include <zlib.h>

using namespace xerahs::editor;
using xerahs::compatibility::ImportDisposition;
namespace {
QByteArray gzip(const QByteArray &plain, int level = Z_BEST_COMPRESSION) {
  z_stream stream{};
  if (deflateInit2(&stream, level, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
  QByteArray out(qsizetype(deflateBound(&stream, uLong(plain.size()))), '\0');
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(plain.constData())); stream.avail_in = uInt(plain.size());
  stream.next_out = reinterpret_cast<Bytef *>(out.data()); stream.avail_out = uInt(out.size());
  const int status = deflate(&stream, Z_FINISH); out.resize(qsizetype(stream.total_out)); deflateEnd(&stream);
  return status == Z_STREAM_END ? out : QByteArray{};
}
QJsonObject fixture() {
  AnnotationDocument document;
  document.canvasWidth = document.canvasHeight = 1;
  document.createdAt = document.modifiedAt = "2026-10-08T00:00:00Z";
  document.sourceImagePng = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");
  RectangleAnnotation shape; shape.id = QUuid("d15c96b8-ab5d-41e8-a1f2-14e660b63cc0"); shape.end = {1, 1};
  document.annotations.push_back(shape);
  return serializeDocument(document);
}
ParseResult load(const QJsonObject &payload) { return readXann(gzip(QJsonDocument(payload).toJson())); }
}

class XannCompatibilityTest : public QObject {
  Q_OBJECT
private slots:
  void reportsEachDispositionWithoutValues() {
    auto input = fixture();
    input.insert("createdAt", "2026-10-08T08:00:00+08:00");
    input.insert("vendor", QJsonObject{{"label", "private-value-must-not-appear"}});
    auto annotations = input.value("annotations").toArray(); auto known = annotations[0].toObject();
    known.remove("opacity"); known.remove("visible"); known.insert("zIndex", 88); known.insert("strokeColor", "#ffabcdef"); annotations[0] = known;
    annotations.append(QJsonObject{{"id", "64b5a6e5-ad9a-4abe-8bf2-975d86fdc458"}, {"type", "future-brush"}, {"vendor", 42}});
    input.insert("annotations", annotations);
    const auto parsed = load(input); QVERIFY(parsed.document);
    for (const auto category : {ImportDisposition::Retained, ImportDisposition::Transformed, ImportDisposition::Defaulted, ImportDisposition::Quarantined, ImportDisposition::Ignored}) QVERIFY(parsed.report.count(category) > 0);
    QCOMPARE(parsed.report.count(ImportDisposition::Rejected), qsizetype(0));
    QVERIFY(!parsed.report.summary().contains("private-value"));
    QVERIFY(std::holds_alternative<UnsupportedAnnotation>(parsed.document->annotations[1]));
    QCOMPARE(serializeDocument(*parsed.document).value("vendor"), input.value("vendor"));
    QCOMPARE(serializeDocument(*parsed.document).value("annotations").toArray()[1], annotations[1]);
    input.insert("consentGranted", true); const auto rejected = load(input);
    QVERIFY(!rejected.document); QCOMPARE(rejected.error.value_or(""), QString("document-unsafe-content")); QVERIFY(rejected.report.count(ImportDisposition::Rejected) > 0);
  }
  void nativePresetReceiptsRemainInertWithoutAllowingArbitraryTypeTags() {
    const QJsonObject tag{{"$type", "ShareX.ImageEffectsLib.Contrast"}};
    QJsonObject receipt{{"format", "com.xerahs.app/effect-preset"}, {"version", 1},
      {"legacy", QJsonObject{{"Effects", QJsonArray{tag}}}},
      {"steps", QJsonArray{QJsonObject{{"legacy", tag}}}}};
    auto input = fixture(); input.insert("effectPresetOperations", QJsonArray{receipt});
    const auto result = load(input); QVERIFY(result.document);
    const auto written = writeXann(*result.document); QVERIFY(written); const auto back = readXann(*written); QVERIFY(back.document);
    QCOMPARE(back.document->extra.value("effectPresetOperations"), input.value("effectPresetOperations"));
    receipt.insert("password", "private-value"); input.insert("effectPresetOperations", QJsonArray{receipt});
    QCOMPARE(load(input).error.value_or(""), QString("document-unsafe-content"));
    input = fixture(); input.insert("vendor", tag);
    QCOMPARE(load(input).error.value_or(""), QString("document-unsafe-content"));
  }
  void unsafeUnknownsRejectWithoutEcho_data() {
    QTest::addColumn<QString>("key");
    for (const auto *key : {"password", "auth", "arguments", "script", "$ref", "requiredFeatures", "nativeHandle"}) QTest::newRow(key) << QString(key);
  }
  void unsafeUnknownsRejectWithoutEcho() {
    QFETCH(QString, key); auto input = fixture(); input.insert("vendor", QJsonObject{{key, "sensitive-local-value"}});
    const auto result = load(input); QVERIFY(!result.document); QCOMPARE(result.error.value_or(""), QString("document-unsafe-content")); QVERIFY(!result.report.summary().contains("sensitive"));
    auto shapeInput = fixture(); auto annotations = shapeInput.value("annotations").toArray(); auto shape = annotations[0].toObject(); shape.insert(key, "sensitive-local-value"); annotations[0] = shape; shapeInput.insert("annotations", annotations);
    const auto shapeResult = load(shapeInput); QVERIFY(!shapeResult.document); QCOMPARE(shapeResult.error.value_or(""), QString("document-unsafe-content"));
    auto exportable = *load(fixture()).document; exportable.extra.insert("vendor", QJsonObject{{key, "sensitive-local-value"}});
    QVERIFY(!writeXann(exportable));  // COMPCAT-006: unsafe content never leaves through export
  }
  void adversarialJsonIsRejected_data() {
    QTest::addColumn<QByteArray>("json"); QTest::addColumn<QString>("code");
    QTest::newRow("malformed") << QByteArray("{not-json}") << QString("document-invalid");
    QTest::newRow("duplicate-key") << QByteArray("{\"version\":1,\"version\":1}") << QString("document-duplicate-key");
    QTest::newRow("escaped-duplicate-key") << QByteArray("{\"version\":1,\"ver\\u0073ion\":1}") << QString("document-duplicate-key");
    QTest::newRow("invalid-utf8") << QByteArray("{\"label\":\"\xff\"}") << QString("document-invalid-encoding");
    QTest::newRow("unpaired-surrogate") << QByteArray("{\"label\":\"\\ud800\"}") << QString("document-invalid-encoding");
    QTest::newRow("deeply-nested") << (QByteArray(65, '[') + '0' + QByteArray(65, ']')) << QString("document-too-deep");
  }
  void adversarialJsonIsRejected() {
    QFETCH(QByteArray, json); QFETCH(QString, code); const auto result = readXann(gzip(json));
    QVERIFY(!result.document); QCOMPARE(result.error.value_or(""), code); QCOMPARE(result.report.count(ImportDisposition::Rejected), qsizetype(1));
  }
  void boundedGzipRejectsTruncationTrailingMembersExpansionAndSize() {
    const auto valid = gzip(QJsonDocument(fixture()).toJson()); QVERIFY(!valid.isEmpty());
    QCOMPARE(readXann(valid.left(valid.size() - 2)).error.value_or(""), QString("document-invalid"));
    QCOMPARE(readXann(valid + valid).error.value_or(""), QString("document-invalid"));
    QCOMPARE(readXann(valid + QByteArray("junk")).error.value_or(""), QString("document-invalid"));
    QCOMPARE(readXann(valid, 32).error.value_or(""), QString("document-too-large"));
    const QByteArray bomb(16 * 1024 * 1024, 'a'); const auto compressed = gzip(bomb);
    QVERIFY(qint64(bomb.size()) > qint64(compressed.size()) * kMaxXannExpansionRatio);
    QCOMPARE(readXann(compressed).error.value_or(""), QString("document-too-large"));
  }
  void lexicalElementBudgetIsEnforcedBeforeDocumentConstruction() {
    const auto bytes = QByteArray("[") + QByteArray("0,").repeated(1000000) + "0]";
    const auto result = readXann(gzip(bytes, Z_NO_COMPRESSION));
    QVERIFY(!result.document); QCOMPARE(result.error.value_or(""), QString("document-too-many-elements"));
  }
  void pathTraversalAndCyclicReferencesReject() {
    for (const auto &path : {QString("../private/image.png"), QString("folder/../image.png"), QString("folder\\..\\image.png")}) {
      auto payload = fixture(); payload.insert("imagePath", path); const auto result = load(payload); QVERIFY(!result.document); QCOMPARE(result.error.value_or(""), QString("document-path-traversal"));
      payload = fixture(); payload.insert("extensions", QJsonObject{{"assetPath", path}}); QCOMPARE(load(payload).error.value_or(""), QString("document-path-traversal"));
    }
    auto payload = fixture(); payload.insert("extensions", QJsonObject{{"$ref", "#"}}); QCOMPARE(load(payload).error.value_or(""), QString("document-unsafe-content"));
  }
  void portableExportReportsBasenameAndPreservesSafeFields() {
    auto input = fixture(); input.insert("imagePath", "C:\\private\\images\\cafe\u0301.png"); input.insert("vendor", QJsonObject{{"label", "opaque\u0301"}});
    const auto parsed = load(input); QVERIFY(parsed.document);
    xerahs::compatibility::ExportReport report; const auto encoded = writeXann(*parsed.document, &report); QVERIFY(encoded);
    QCOMPARE(report.target, QString("xann")); QCOMPARE(report.version, 1); QCOMPARE(report.omissions, QStringList{"nonportable-image-path"});
    const auto back = readXann(*encoded); QVERIFY(back.document); QCOMPARE(back.document->imagePath, QString::fromUtf8("caf\xc3\xa9.png"));
    QCOMPARE(back.document->extra.value("vendor"), input.value("vendor"));
    QCOMPARE(back.document->createdAt, QString("2026-10-08T00:00:00.000Z"));
    auto unsafe = *parsed.document; unsafe.extra.insert("password", "private"); QVERIFY(!writeXann(unsafe));
  }
  void localesNewlinesAndUnicodeVariantsKeepTheSameMeaning() {
    const QLocale previous = QLocale();
    auto input = fixture(); input.insert("createdAt", "2026-10-08T08:00:00+08:00"); input.insert("imagePath", QString::fromUtf8("caf\xc3\xa9.png"));
    const auto native = parseDocument(input); QVERIFY(native.document); xerahs::compatibility::ExportReport report;
    const auto expected = writeXann(*native.document, &report); QVERIFY(expected);
    for (const auto &locale : {QLocale("en_US"), QLocale("de_DE"), QLocale("tr_TR")}) {
      QLocale::setDefault(locale);
      for (bool crlf : {false, true}) {
        auto variant = input; variant.insert("imagePath", "cafe\u0301.png");
        auto json = QJsonDocument(variant).toJson(QJsonDocument::Indented); if (crlf) json.replace("\n", "\r\n");
        const auto parsed = readXann(gzip(json)); QVERIFY(parsed.document); const auto encoded = writeXann(*parsed.document); QVERIFY(encoded); QCOMPARE(*encoded, *expected);
      }
    }
    QLocale::setDefault(previous);
  }
};
QTEST_GUILESS_MAIN(XannCompatibilityTest)
#include "XannCompatibilityTest.moc"
