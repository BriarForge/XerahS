// Load/save path of the editor (ES-001, ES-011, ES-016, ES-029) over real files.

#include "EditorSource.h"
#include "RasterSource.h"
#include "ComparisonImages.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUuid>
#include <zlib.h>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <cmath>
#include <stdexcept>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
// Independent gunzip (not the production reader): a gzip member holding UTF-8 JSON.
QByteArray gunzip(const QByteArray &gzip) {
  z_stream stream{}; if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return {};
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(gzip.constData())); stream.avail_in = uInt(gzip.size());
  QByteArray out; char buffer[65536]; int status = Z_OK;
  while (status == Z_OK) { stream.next_out = reinterpret_cast<Bytef *>(buffer); stream.avail_out = sizeof buffer; status = inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) break; out.append(buffer, qsizetype(sizeof buffer - stream.avail_out)); }
  inflateEnd(&stream); return status == Z_STREAM_END ? out : QByteArray{};
}
// Minimal validator for the JSON Schema keywords annotation-document.schema.json actually uses.
// Every rule is read from the schema file; nothing about the document shape is hard-coded here.
void validate(const QJsonObject &root, QJsonObject schema, const QJsonValue &value, const QString &where, QStringList &errors) {
  if (schema.contains("$ref")) { QJsonValue node = root; for (const auto &part : schema.value("$ref").toString().mid(2).split('/')) node = node.toObject().value(part); schema = node.toObject(); }
  const auto fail = [&](const QString &why) { errors << where + ": " + why; };
  if (schema.contains("const") && value != schema.value("const")) fail("const mismatch");
  if (const auto type = schema.value("type").toString(); !type.isEmpty()) {
    const bool integer = value.isDouble() && value.toDouble() == std::floor(value.toDouble());
    const bool ok = type == "string" ? value.isString() : type == "integer" ? integer : type == "number" ? value.isDouble() : type == "boolean" ? value.isBool() : type == "object" ? value.isObject() : type == "array" ? value.isArray() : false;
    if (!ok) { fail("expected " + type); return; }
  }
  if (value.isDouble()) {
    if (schema.contains("minimum") && value.toDouble() < schema.value("minimum").toDouble()) fail("below minimum");
    if (schema.contains("maximum") && value.toDouble() > schema.value("maximum").toDouble()) fail("above maximum");
    if (schema.contains("exclusiveMinimum") && value.toDouble() <= schema.value("exclusiveMinimum").toDouble()) fail("not above exclusiveMinimum");
  }
  if (value.isString()) {
    const QString text = value.toString();
    if (schema.contains("pattern") && !QRegularExpression(schema.value("pattern").toString()).match(text).hasMatch()) fail("pattern mismatch");
    const auto format = schema.value("format").toString();
    if (format == "date-time") {  // RFC 3339: explicit offset or Z
      static const QRegularExpression rfc3339(R"(^\d{4}-\d{2}-\d{2}[Tt]\d{2}:\d{2}:\d{2}(\.\d+)?([Zz]|[+-]\d{2}:\d{2})$)");
      if (!rfc3339.match(text).hasMatch() || !QDateTime::fromString(text, Qt::ISODate).isValid()) fail("not an RFC 3339 date-time");
    }
    if (format == "uuid" && QUuid::fromString(text).isNull()) fail("not a uuid");
    if (schema.value("contentEncoding").toString() == "base64") {
      const auto decoded = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
      if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || decoded.decoded.toBase64() != text.toLatin1()) fail("not canonical base64");
      else if (schema.value("contentMediaType").toString() == "image/png" && !decoded.decoded.startsWith("\x89PNG\r\n\x1a\n")) fail("decoded content is not a PNG");
    }
  }
  if (value.isObject()) {
    const auto object = value.toObject(); const auto properties = schema.value("properties").toObject();
    for (const auto &key : schema.value("required").toArray()) if (!object.contains(key.toString())) fail("missing required " + key.toString());
    for (auto it = properties.begin(); it != properties.end(); ++it) if (object.contains(it.key())) validate(root, it.value().toObject(), object.value(it.key()), where + "/" + it.key(), errors);
    if (schema.value("additionalProperties").isObject()) for (auto it = object.begin(); it != object.end(); ++it) if (!properties.contains(it.key())) validate(root, schema.value("additionalProperties").toObject(), it.value(), where + "/" + it.key(), errors);
    if (schema.value("additionalProperties") == QJsonValue(false)) for (auto it = object.begin(); it != object.end(); ++it) if (!properties.contains(it.key())) fail("unexpected " + it.key());
    for (const auto &rule : schema.value("allOf").toArray()) {
      const auto branch = rule.toObject(); QStringList ifErrors; validate(root, branch.value("if").toObject(), value, where, ifErrors);
      if (ifErrors.isEmpty() && branch.contains("then")) validate(root, branch.value("then").toObject(), value, where, errors);
    }
  }
  if (value.isArray() && schema.contains("items")) { int i = 0; for (const auto &item : value.toArray()) validate(root, schema.value("items").toObject(), item, where + "/" + QString::number(i++), errors); }
}
}  // namespace

class EditorSourceTest : public QObject {
  Q_OBJECT
private slots:
  void retainedDocumentReopensSourceAndAnnotationsWithoutRasterReads() {
    QTemporaryDir dir; const auto raster = dir.filePath("borrowed.png"); QImage image(30, 24, QImage::Format_ARGB32); image.fill(Qt::blue); QVERIFY(image.save(raster));
    auto source = prepareCapturedSource(image); QVERIFY(source.document); xerahs::editor::EditorSession session(*source.document); session.createRectangle({5, 5}, {15, 15});
    auto document = session.document(); document.imagePath = raster; document.imageHash = "sha256:" + QString(64, '0'); QVERIFY(QFile::remove(raster));
    const auto reopened = prepareCapturedDocument(document); QVERIFY(reopened.document); QCOMPARE(reopened.image, image);
    QVERIFY(reopened.document->imagePath.isEmpty() && reopened.document->imageHash.isEmpty()); QCOMPARE(reopened.document->annotations.size(), size_t(1));
    const auto rendered = renderEditorImage(reopened.image, *reopened.document); QVERIFY(!rendered.image.isNull()); QCOMPARE(rendered.image.pixel(5, 5), qRgb(255, 0, 0));
    QCOMPARE(renderEditorImage(reopened.image, *reopened.document, {[] { return true; }, {}}).diagnostic, QString("canvas-cancelled"));
    document.canvasWidth = 31; QVERIFY(!prepareCapturedDocument(document).document);
  }
  void capturedSourceOwnsPixelsPreservesAlphaAndProfileAndInfersNoFile() {
    QByteArray memory(3 * 2 * 4, '\0');
    QImage input(reinterpret_cast<uchar *>(memory.data()), 3, 2, 12, QImage::Format_ARGB32);
    input.fill(qRgba(128, 0, 0, 80)); input.setColorSpace(QColorSpace::SRgbLinear); input.setDevicePixelRatio(2);
    const auto result = prepareCapturedSource(input); QVERIFY(result.document); QVERIFY(result.diagnostic.isEmpty());
    QCOMPARE(result.document->canvasWidth, qint64(3)); QCOMPARE(result.document->canvasHeight, qint64(2));
    QVERIFY(result.document->imagePath.isEmpty() && result.document->imageHash.isEmpty() && result.sidecarPath.isEmpty());
    QCOMPARE(result.image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear)); QCOMPARE(result.image.devicePixelRatio(), qreal(1));
    QCOMPARE(result.image.pixel(0, 0), qRgba(128, 0, 0, 80)); QCOMPARE(result.displayImage.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(std::abs(qRed(result.displayImage.pixel(0, 0)) - 188) <= 1); QCOMPARE(qAlpha(result.displayImage.pixel(0, 0)), 80);
    input = {}; memory.fill('\0');
    QCOMPARE(result.image.pixel(0, 0), qRgba(128, 0, 0, 80));
    QCOMPARE(QImage::fromData(result.document->sourceImagePng, "PNG").pixel(0, 0), qRgba(128, 0, 0, 80));
  }
  void capturedSourceCancellationDiscardsAllPreparedOutput_data() {
    QTest::addColumn<int>("cancelAt");
    for (int stage : {0, 20, 60, 70, 90, 100}) QTest::newRow(qPrintable(QString::number(stage))) << stage;
  }
  void capturedSourceCancellationDiscardsAllPreparedOutput() {
    QFETCH(int, cancelAt); QImage image(12, 8, QImage::Format_ARGB32); image.fill(qRgba(23, 45, 67, 89));
    const auto before = image.copy(); int progress = 0;
    const auto result = prepareCapturedSource(image, {[&] { return progress >= cancelAt; }, [&](int p) { progress = p; }});
    QCOMPARE(result.diagnostic, QString("canvas-cancelled")); QVERIFY(!result.document && result.image.isNull() && result.displayImage.isNull());
    QCOMPARE(image, before);
  }
  void capturedSourceRejectsEmptyOversizedInputAndRedactsCallbackExceptions() {
    QCOMPARE(prepareCapturedSource({}).diagnostic, diagnostic::sourceEmpty);
    QImage excessive(int(kMaxDimension + 1), 1, QImage::Format_ARGB32);
    const auto rejected = prepareCapturedSource(excessive); QCOMPARE(rejected.diagnostic, diagnostic::sourceTooLarge); QVERIFY(!rejected.document);
    QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::red);
    const auto failed = prepareCapturedSource(image, {{}, [](int) { throw std::runtime_error("private value"); }});
    QCOMPARE(failed.diagnostic, diagnostic::documentInvalid); QVERIFY(!failed.document && failed.image.isNull());
  }
  void sourceLoadingPreparesPrivateDisplayPixelsAndCancelsWithoutPublishing_data() {
    QTest::addColumn<int>("cancelAt");
    for (int stage : {0, 40, 50, 60, 75, 80, 100}) QTest::newRow(qPrintable(QString::number(stage))) << stage;
  }
  void sourceLoadingPreparesPrivateDisplayPixelsAndCancelsWithoutPublishing() {
    QFETCH(int, cancelAt);
    QTemporaryDir dir; const QString path = dir.filePath("linear.png");
    QImage image(3, 2, QImage::Format_ARGB32); image.fill(qRgba(128, 0, 0, 80)); image.setColorSpace(QColorSpace::SRgbLinear);
    QVERIFY(image.save(path)); const auto first = loadSource(path); QVERIFY(first.document);
    QCOMPARE(first.image, image); QCOMPARE(first.image.colorSpace(), image.colorSpace());
    QCOMPARE(first.displayImage.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(std::abs(qRed(first.displayImage.pixel(0, 0)) - 188) <= 1); QCOMPARE(qAlpha(first.displayImage.pixel(0, 0)), 80);
    EditorSession session(*first.document); QVERIFY(session.createRectangle({.25, .25}, {1.5, 1.5}));
    const auto saved = saveEdit(path, image, session.document()); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    QFile raster(path), sidecar(path + ".xann"); QVERIFY(raster.open(QIODevice::ReadOnly)); const auto rasterBefore = raster.readAll(); raster.close();
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); const auto sidecarBefore = sidecar.readAll(); sidecar.close();
    int percent = 0;
    const auto stopped = loadSource(path, {[&] { return percent >= cancelAt; }, [&](int p) { percent = p; }});
    QCOMPARE(stopped.diagnostic, QStringLiteral("canvas-cancelled"));
    QVERIFY(!stopped.document && !stopped.pendingChoice && stopped.image.isNull() && stopped.displayImage.isNull());
    QVERIFY(raster.open(QIODevice::ReadOnly)); QCOMPARE(raster.readAll(), rasterBefore);
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), sidecarBefore);
  }
  void encodedFilesAreBoundedBeforeBufferAllocation() {
    QTemporaryDir dir; const QString path = dir.filePath("oversized.png");
    QFile huge(path); QVERIFY(huge.open(QIODevice::WriteOnly)); QVERIFY(huge.resize(kMaxEncodedImageBytes + 1)); huge.close();
    const auto raster = loadRaster(path); QCOMPARE(raster.diagnostic, diagnostic::sourceTooLarge);
    QVERIFY(raster.image.isNull() && raster.fileBytes.isEmpty());
    QCOMPARE(loadSource(path).diagnostic, diagnostic::sourceTooLarge);
    QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::blue); QVERIFY(image.save(path));
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); QVERIFY(sidecar.resize(kMaxEncodedImageBytes + 1)); sidecar.close();
    const auto document = loadSource(path); QCOMPARE(document.diagnostic, diagnostic::documentTooLarge);
    QVERIFY(!document.document && document.image.isNull() && document.displayImage.isNull());
    QCOMPARE(QImage(path), image); QCOMPARE(QFileInfo(sidecar.fileName()).size(), kMaxEncodedImageBytes + 1);
  }
  void rasterDecodePreservesAlphaProfileAndOriginalBytes() {
    QTemporaryDir dir;
    const QString path = dir.filePath("alpha.png");
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(qRgba(64, 128, 192, 80));
    image.setColorSpace(QColorSpace::SRgbLinear);
    QVERIFY(image.save(path));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray before = file.readAll(); file.close();
    const auto loaded = loadRaster(path);
    QVERIFY(loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.image, image);
    QCOMPARE(loaded.image.colorSpace(), image.colorSpace());
    QCOMPARE(loaded.fileBytes, before);
    int percent = 0;
    const auto cancelled = loadRaster(path, {[&] { return percent >= 20; }, [&](int p) { percent = p; }});
    QCOMPARE(cancelled.diagnostic, QStringLiteral("canvas-cancelled"));
    QVERIFY(cancelled.image.isNull() && cancelled.fileBytes.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
  }

  void orientationIsAppliedExactlyOnceToPixelsAndEmbeddedSource() {
    QTemporaryDir dir;
    QImage image(2, 3, QImage::Format_RGB32);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x)
      image.setPixel(x, y, qRgb(x * 150, y * 90, 40));
    QByteArray jpeg; QBuffer buffer(&jpeg); QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPEG", 100)); buffer.close();
    // Independent TIFF/EXIF fixture: little-endian orientation tag 6 (CW 90).
    const QByteArray exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
    QByteArray segment = QByteArray::fromHex("ffe1");
    segment.append(char((exif.size() + 2) >> 8)); segment.append(char((exif.size() + 2) & 255)); segment.append(exif);
    jpeg.insert(2, segment);
    const QString path = dir.filePath("oriented.jpg");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(jpeg), jpeg.size()); file.close();
    QImageReader rawReader(path); rawReader.setAutoTransform(false);
    const QImage raw = rawReader.read().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(raw.size(), QSize(2, 3));
    const auto loaded = loadRaster(path); QVERIFY(loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.image.size(), QSize(3, 2));
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x)
      QCOMPARE(loaded.image.pixel(x, y), raw.pixel(y, 2 - x));
    const auto source = loadSource(path); QVERIFY(source.document);
    QCOMPARE(source.image, loaded.image);
    const QImage embedded = QImage::fromData(source.document->sourceImagePng, "PNG");
    QCOMPARE(embedded, loaded.image);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), jpeg);
  }

  void rasterHeadersAreLimitedBeforePixelDecode() {
    QTemporaryDir dir;
    QByteArray bmp(54, '\0'); bmp[0] = 'B'; bmp[1] = 'M';
    auto put32 = [&](int offset, quint32 value) { qToLittleEndian(value, bmp.data() + offset); };
    put32(2, 54); put32(10, 54); put32(14, 40); put32(18, 100001); put32(22, 1);
    bmp[26] = 1; bmp[28] = 32;
    QFile file(dir.filePath("oversized.bmp")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bmp); file.close();
    QImageReader reader(file.fileName()); QVERIFY(reader.canRead()); QCOMPARE(reader.size(), QSize(100001, 1));
    const auto loaded = loadRaster(file.fileName());
    QCOMPARE(loaded.diagnostic, diagnostic::sourceTooLarge);
    QVERIFY(loaded.image.isNull() && loaded.fileBytes.isEmpty());
  }

  void comparisonLoadsTheRasterWithoutReadingOrChangingItsSidecar() {
    QTemporaryDir dir;
    const QString path = dir.filePath("raster.png");
    QImage image(2, 3, QImage::Format_ARGB32); image.fill(qRgba(20, 40, 60, 80)); QVERIFY(image.save(path));
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write("corrupt"); sidecar.close();
    QCOMPARE(loadSource(path).diagnostic, diagnostic::documentInvalid);
    const auto comparison = loadComparisonImage(path); QVERIFY(comparison.diagnostic.isEmpty());
    QCOMPARE(comparison.image.size(), image.size());
    QCOMPARE(comparison.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
      QCOMPARE(comparison.image.pixel(x, y), image.pixel(x, y));
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), QByteArray("corrupt"));
    QCOMPARE(QImage(path), image);
  }

  // ES-013: a document saved through the production path is a gzip stream of UTF-8 JSON that
  // validates against the shared schema and carries every element the requirement lists.
  void savedXannIsGzipUtf8JsonThatValidatesAgainstTheSharedSchema() {
    QFile schemaFile(XERAHS_XANN_SCHEMA); QVERIFY2(schemaFile.open(QIODevice::ReadOnly), XERAHS_XANN_SCHEMA);
    const auto schema = QJsonDocument::fromJson(schemaFile.readAll()).object(); QVERIFY(!schema.isEmpty()); const auto required = schema.value("required").toArray(); QVERIFY(required.size() >= 10);
    QTemporaryDir dir; const QString path = dir.filePath("shot.png"); QImage blank(40, 30, QImage::Format_ARGB32); blank.fill(Qt::white); QVERIFY(blank.save(path));
    LoadedSource first = loadSource(path); QVERIFY(first.document); EditorSession session(*first.document);
    QVERIFY(session.createRectangle({5, 5}, {20, 20})); QVERIFY(session.createRectangle({6, 7}, {30, 25}));
    auto document = session.document(); document.embeddedImages.emplace_back("asset", first.document->sourceImagePng);
    const QList<QUuid> order{annotationId(document.annotations[0]), annotationId(document.annotations[1])};
    const SaveOutcome outcome = saveEdit(path, first.image, document); QVERIFY(outcome.rasterSaved && outcome.sidecarSaved);
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::ReadOnly)); const QByteArray gzip = sidecar.readAll(); QCOMPARE(gzip.left(2), QByteArray("\x1f\x8b", 2));
    const QByteArray json = gunzip(gzip); QVERIFY(!json.isEmpty());
    QStringDecoder utf8(QStringDecoder::Utf8); const QString decoded = utf8(json); QVERIFY(!utf8.hasError()); QVERIFY(!decoded.isEmpty() && decoded.startsWith('{'));
    QJsonParseError parseError; const auto payload = QJsonDocument::fromJson(json, &parseError); QCOMPARE(parseError.error, QJsonParseError::NoError); QVERIFY(payload.isObject());
    const auto object = payload.object();
    for (const auto &key : required) QVERIFY2(object.contains(key.toString()), qPrintable("missing required key " + key.toString()));
    QStringList errors; validate(schema, schema, object, "#", errors); QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
    // The elements ES-013 names, checked against the real files rather than the schema alone.
    QFile raster(path); QVERIFY(raster.open(QIODevice::ReadOnly));
    QCOMPARE(object.value("imageHash").toString(), QString("sha256:") + QString::fromLatin1(QCryptographicHash::hash(raster.readAll(), QCryptographicHash::Sha256).toHex()));
    QCOMPARE(object.value("canvasWidth").toInt(), 40); QCOMPARE(object.value("canvasHeight").toInt(), 30);
    QVERIFY(QDateTime::fromString(object.value("createdAt").toString(), Qt::ISODate).isValid()); QVERIFY(QDateTime::fromString(object.value("modifiedAt").toString(), Qt::ISODate) >= QDateTime::fromString(object.value("createdAt").toString(), Qt::ISODate));
    QCOMPARE(QImage::fromData(QByteArray::fromBase64(object.value("sourceImagePngBase64").toString().toLatin1()), "PNG"), blank);  // unannotated source
    QCOMPARE(object.value("embeddedImages").toObject().keys(), QStringList{"asset"});
    const auto annotations = object.value("annotations").toArray(); QCOMPARE(annotations.size(), 2);
    for (int i = 0; i < 2; ++i) QCOMPARE(QUuid::fromString(annotations[i].toObject().value("id").toString()), order[i]);  // ordered
    // The validator must reject what the schema forbids, or it proves nothing.
    for (const auto &[key, bad] : {std::pair<QString, QJsonValue>{"version", 2}, {"canvasWidth", 100001}, {"imageHash", "md5:x"}, {"createdAt", "yesterday"}, {"sourceImagePngBase64", "AAAA"}, {"annotations", "none"}}) {
      auto broken = object; broken.insert(key, bad); QStringList rejected; validate(schema, schema, broken, "#", rejected); QVERIFY2(!rejected.isEmpty(), qPrintable(key));
    }
    auto missing = object; missing.remove("embeddedImages"); QStringList rejected; validate(schema, schema, missing, "#", rejected); QVERIFY(!rejected.isEmpty());
  }

  // ES-029: a PNG cut inside its pixel data has a readable header but cannot be decoded.
  void truncatedPngReportsSourceCorruptWithoutCreatingASession() {
    QTemporaryDir dir; const QString path = dir.filePath("cut.png"); QImage image(40, 30, QImage::Format_ARGB32); image.fill(Qt::red); QVERIFY(image.save(path));
    QFile whole(path); QVERIFY(whole.open(QIODevice::ReadOnly)); const QByteArray full = whole.readAll(); whole.close(); QVERIFY(full.size() > 80);
    for (const int cut : {int(full.size()) - 20, int(full.size()) / 2}) {
      QFile truncated(path); QVERIFY(truncated.open(QIODevice::WriteOnly | QIODevice::Truncate)); QCOMPARE(truncated.write(full.left(cut)), qint64(cut)); truncated.close();
      QImageReader header(path); QVERIFY(header.canRead()); QCOMPARE(header.size(), QSize(40, 30));  // header intact, pixels not
      const auto raster = loadRaster(path); QCOMPARE(raster.diagnostic, diagnostic::sourceCorrupt); QVERIFY(raster.image.isNull() && raster.fileBytes.isEmpty());
      const auto loaded = loadSource(path); QVERIFY(!loaded.document && !loaded.pendingChoice); QCOMPARE(loaded.diagnostic, diagnostic::sourceCorrupt); QVERIFY(loaded.image.isNull());
      QCOMPARE(decodeRaster(full.left(cut)).diagnostic, diagnostic::sourceCorrupt);
      QVERIFY(!QFile::exists(path + ".xann"));
    }
  }

  void rejectsNonImages() {
    QTemporaryDir dir;
    QFile f(dir.filePath("bad.png"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not an image");
    f.close();
    QCOMPARE(loadSource(f.fileName()).diagnostic, diagnostic::sourceUnsupported);
    QFile empty(dir.filePath("empty.png"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QCOMPARE(loadSource(empty.fileName()).diagnostic, diagnostic::sourceEmpty);
    QCOMPARE(loadSource(dir.filePath("missing.png")).diagnostic, diagnostic::sourceUnsupported);
  }

  void saveThenReopenKeepsAnnotationsEditable() {
    QTemporaryDir dir;
    const QString path = dir.filePath("shot.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    QVERIFY(blank.save(path));

    LoadedSource first = loadSource(path);
    QVERIFY(first.document);
    QCOMPARE(first.document->canvasWidth, 40);
    EditorSession session(*first.document);
    QVERIFY(session.createRectangle({5, 5}, {20, 20}));

    const SaveOutcome outcome = saveEdit(path, first.image, session.document());
    QVERIFY(outcome.rasterSaved);
    QVERIFY(outcome.sidecarSaved);
    QVERIFY(session.recordSave(outcome.rasterSaved, outcome.sidecarSaved).isEmpty());
    QVERIFY(!session.dirty());
    QVERIFY(QFile::exists(path + ".xann"));
    QCOMPARE(QImage(path).pixel(5, 12), qRgba(255, 0, 0, 255));  // flattened export

    // Reopening edits the embedded unannotated source, not the flattened raster.
    LoadedSource second = loadSource(path);
    QVERIFY(second.document);
    QVERIFY(second.warnings.isEmpty());
    QCOMPARE(second.document->annotations.size(), size_t(1));
    QCOMPARE(second.image.pixel(5, 12), qRgba(255, 255, 255, 255));
  }

  void changedRasterRequiresChoiceAndKeepsValidatedSnapshots() {
    QTemporaryDir dir;
    const QString path = dir.filePath("shot.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    QVERIFY(blank.save(path));
    LoadedSource first = loadSource(path);
    EditorSession session(*first.document);
    session.createRectangle({5, 5}, {20, 20});
    QVERIFY(saveEdit(path, first.image, session.document()).rasterSaved);
    QImage other(24, 20, QImage::Format_ARGB32);
    other.fill(Qt::black);
    QVERIFY(other.save(path));
    LoadedSource reopened = loadSource(path);
    QVERIFY(!reopened.document && reopened.pendingChoice && reopened.image.isNull());
    QVERIFY(resolveSourceChoice(reopened, SourceChoice::Unspecified).pendingChoice);
    // A file changes again while the user decides: use the validated snapshot.
    QImage later(10, 10, QImage::Format_ARGB32); later.fill(Qt::blue); QVERIFY(later.save(path));
    const auto current = resolveSourceChoice(reopened, SourceChoice::CurrentRaster);
    QVERIFY(current.document); QCOMPARE(current.image, other); QCOMPARE(current.document->canvasWidth, 24);
    const auto embedded = resolveSourceChoice(reopened, SourceChoice::EmbeddedSource);
    QVERIFY(embedded.document); QCOMPARE(embedded.image, blank); QCOMPARE(embedded.document->canvasWidth, 40);
    QCOMPARE(embedded.document->annotations.size(), size_t(1));
    QCOMPARE(serializeDocument(*current.document).value(u"annotations"), serializeDocument(*embedded.document).value(u"annotations"));
    QCOMPARE(QImage(path), later);  // no choice rewrites the user's file
  }

  void invalidOrNewerSidecarsNeverCreateABareRasterSession() {
    QTemporaryDir dir; const QString path = dir.filePath("safe.png");
    QImage image(4, 3, QImage::Format_ARGB32); image.fill(Qt::white); QVERIFY(image.save(path));
    const auto initial = loadSource(path); QVERIFY(initial.document);
    auto document = *initial.document; document.version = 2;
    const auto newer = writeXann(document); QVERIFY(newer);
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); QCOMPARE(sidecar.write(*newer), newer->size()); sidecar.close();
    const auto failed = loadSource(path);
    QVERIFY(!failed.document && !failed.pendingChoice); QCOMPARE(failed.diagnostic, diagnostic::documentVersionUnsupported);
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), *newer); sidecar.close(); QCOMPARE(QImage(path), image);
    QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate)); sidecar.write("corrupt"); sidecar.close();
    const auto corrupt = loadSource(path); QVERIFY(!corrupt.document); QCOMPARE(corrupt.diagnostic, diagnostic::documentInvalid);
    document.version = 1; document.sourceImagePng.truncate(29);  // header passes; pixel decoding must fail
    const auto truncated = writeXann(document); QVERIFY(truncated);
    QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate)); sidecar.write(*truncated); sidecar.close();
    const auto damagedSource = loadSource(path); QVERIFY(!damagedSource.document); QCOMPARE(damagedSource.diagnostic, diagnostic::documentInvalid);
  }

  void freeAngleRotationSavesAndReopensWithHistory() {
    QTemporaryDir dir;
    const QString path = dir.filePath("rotated.png");
    QImage blank(40, 30, QImage::Format_ARGB32);
    blank.fill(Qt::transparent);
    QVERIFY(blank.save(path));
    const LoadedSource first = loadSource(path);
    QVERIFY(first.document);
    EditorSession session(*first.document);
    QVERIFY(session.createRectangle({10, 5}, {30, 25}));
    QVERIFY(session.rotateSelection(17));
    QCOMPARE(session.undoCount(), 2);
    QVERIFY(session.undo());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).style.rotationDegrees, 0);
    QVERIFY(session.redo());
    const auto outcome = saveEdit(path, first.image, session.document());
    QVERIFY(outcome.rasterSaved && outcome.sidecarSaved);
    QVERIFY(session.recordSave(outcome.rasterSaved, outcome.sidecarSaved).isEmpty());
    const LoadedSource reopened = loadSource(path);
    QVERIFY(reopened.document);
    QVERIFY(reopened.warnings.isEmpty());
    QCOMPARE(std::get<RectangleAnnotation>(reopened.document->annotations[0]).style.rotationDegrees, 17);
    QCOMPARE(reopened.image.pixel(10, 15), qRgba(0, 0, 0, 0));
    QVERIFY(qAlpha(QImage(path).pixel(10, 15)) > 0);
  }
};

QTEST_MAIN(EditorSourceTest)
#include "EditorSourceTest.moc"
