// EDITOR-UTILITIES-001 / EU-009, EU-010: the background-model catalogue, the
// bounded ONNX header reader and the verify-then-adopt model store. The
// approved contract catalogue itself is loaded and pinned by these tests; the
// ONNX fixtures are hand-encoded bytes (tests/OnnxFixture.h), so nothing here
// touches the network or a real model file.
#include "background/BackgroundModels.h"

#include "OnnxFixture.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>

using namespace xerahs::bg;
namespace fixture = OnnxFixture;

namespace {

QString writeTemp(const QTemporaryDir &dir, const char *name, const QByteArray &bytes) {
  const QString path = dir.path() + QLatin1Char('/') + QLatin1String(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  file.write(bytes);
  file.close();
  return path;
}

QString sha256Hex(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString md5Hex(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

// A catalogue entry whose pins describe the test's own fixture bytes.
CatalogueEntry fixtureEntry(const QByteArray &modelBytes) {
  CatalogueEntry entry;
  entry.id = QStringLiteral("u2netp");
  entry.label = QStringLiteral("U2-Net lightweight (u2netp)");
  entry.description = QStringLiteral("fixture");
  entry.upstream = {QStringLiteral("U-2-Net"), QStringLiteral("authors"), QStringLiteral("paper"),
                    QStringLiteral("https://github.com/xuebinqin/U-2-Net")};
  entry.licence = {QStringLiteral("Apache-2.0"), QStringLiteral("https://github.com/xuebinqin/U-2-Net/blob/master/LICENSE"),
                   QStringLiteral("note")};
  entry.artifact.url = QStringLiteral("https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx");
  entry.artifact.host = QStringLiteral("github.com");
  entry.artifact.hostLicence = QStringLiteral("rembg MIT");
  entry.artifact.sha256 = sha256Hex(modelBytes);
  entry.artifact.md5 = md5Hex(modelBytes);
  entry.artifact.sizeBytes = modelBytes.size();
  entry.input = [] {
    TensorContract contract;
    contract.name = QStringLiteral("input.1");
    contract.elemType = 1;
    for (const qint64 dim : {qint64(1), qint64(3), qint64(320), qint64(320)}) {
      TensorDim value;
      value.value = dim;
      contract.dims.append(value);
    }
    return contract;
  }();
  entry.firstOutput = entry.input;
  entry.firstOutput.name = QStringLiteral("1959");
  entry.firstOutput.dims = {TensorDim{1, {}}, TensorDim{1, {}}, TensorDim{320, {}}, TensorDim{320, {}}};
  return entry;
}

QByteArray mutate(const QByteArray &json, const QString &from, const QString &to) {
  QByteArray copy = json;
  copy.replace(from.toUtf8(), to.toUtf8());
  return copy;
}

}  // namespace

class BackgroundModelsTest final : public QObject {
  Q_OBJECT

private slots:
  void catalogueLoadsTheApprovedContractFile();
  void catalogueRejectsInvalidData();
  void onnxHeaderParsesHandEncodedModel();
  void onnxHeaderParsesSymbolicDimensions();
  void onnxHeaderRejectsMalformedBytes();
  void tensorContractMatchingIsStrict();
  void validateModelFileVerifiesPinnedIntegrity();
  void adoptModelKeepsTheStoreSafe();
  void encodeInputTensorMatchesTheDecidedPolicy();
  void encodeInputTensorRefusesUnusableInputs();
  void composeMaskedImageProducesTheDecidedAlpha();
  void composeMaskedImageDegenerateConstantIsDefined();
  void composeMaskedImageRefusesInvalidMasks();

private:
  QString catalogueJson() const;
};

QString BackgroundModelsTest::catalogueJson() const {
  QFile file(QStringLiteral(XERAHS_BACKGROUND_CATALOGUE));
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void BackgroundModelsTest::catalogueLoadsTheApprovedContractFile() {
  const CatalogueResult catalogue = loadCatalogueFromFile(QStringLiteral(XERAHS_BACKGROUND_CATALOGUE));
  QVERIFY2(catalogue.ok, qPrintable(catalogue.error));
  QCOMPARE(catalogue.entries.size(), 1);
  const CatalogueEntry &entry = catalogue.entries.first();
  QCOMPARE(entry.id, QStringLiteral("u2netp"));
  QCOMPARE(entry.licence.id, QStringLiteral("Apache-2.0"));
  QCOMPARE(entry.upstream.url, QStringLiteral("https://github.com/xuebinqin/U-2-Net"));
  QCOMPARE(entry.artifact.host, QStringLiteral("github.com"));
  QCOMPARE(entry.artifact.url,
           QStringLiteral("https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx"));
  QCOMPARE(entry.artifact.sizeBytes, qint64(4574861));
  QCOMPARE(entry.artifact.sha256,
           QStringLiteral("309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8"));
  QCOMPARE(entry.artifact.md5, QStringLiteral("8e83ca70e441ab06c318d82300c84806"));
  QCOMPARE(entry.input.name, QStringLiteral("input.1"));
  QCOMPARE(entry.input.elemType, 1);
  QCOMPARE(entry.input.dims.size(), 4);
  QCOMPARE(entry.input.dims.at(2).value, qint64(320));
  QCOMPARE(entry.firstOutput.name, QStringLiteral("1959"));
  QCOMPARE(entry.firstOutput.dims.at(1).value, qint64(1));
  QCOMPARE(entry.preprocess.mean[0], 0.485);
  QCOMPARE(entry.preprocess.std[2], 0.225);
  QCOMPARE(entry.maskPolicy.alpha, QStringLiteral("mask * 255"));
  QCOMPARE(entry.fileName(), QStringLiteral("u2netp.onnx"));
}

void BackgroundModelsTest::catalogueRejectsInvalidData() {
  const QString json = catalogueJson();
  QVERIFY(!json.isEmpty());

  struct Case {
    const char *name;
    QString from, to;
  };
  const Case cases[] = {
      {"bad sha256", QStringLiteral("309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8"),
       QStringLiteral("ZZZZ")},
      {"http url", QStringLiteral("https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx"),
       QStringLiteral("http://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx")},
      {"host mismatch", QStringLiteral("\"host\": \"github.com\""), QStringLiteral("\"host\": \"example.com\"")},
      {"zero size", QStringLiteral("\"size_bytes\": 4574861"), QStringLiteral("\"size_bytes\": 0")},
      {"uppercase id", QStringLiteral("\"id\": \"u2netp\""), QStringLiteral("\"id\": \"U2NETP\"")},
      {"unsupported elem type", QStringLiteral("\"elem_type\": \"float32\""), QStringLiteral("\"elem_type\": \"int64\"")},
      {"zero dim", QStringLiteral("\"dims\": [1, 3, 320, 320]"), QStringLiteral("\"dims\": [1, 3, 320, 0]")},
      {"non-positive std", QStringLiteral("\"std\": [0.229, 0.224, 0.225]"), QStringLiteral("\"std\": [0, 0.224, 0.225]")},
  };
  for (const Case &test : cases) {
    const CatalogueResult result = loadCatalogue(mutate(json.toUtf8(), test.from, test.to));
    QString message = QStringLiteral("mutation %1 should be rejected").arg(test.name);
    QVERIFY2(!result.ok, qPrintable(message + QStringLiteral(": ") + result.error));
    QVERIFY2(!result.error.isEmpty(), "the rejection carries a diagnostic");
  }

  CatalogueResult result = loadCatalogue(QByteArrayLiteral("{\"schema_version\": 2, \"models\": []}"));
  QVERIFY(!result.ok);
  result = loadCatalogue(QByteArrayLiteral("not json"));
  QVERIFY(!result.ok);
}

void BackgroundModelsTest::onnxHeaderParsesHandEncodedModel() {
  QTemporaryDir dir;
  const QString path = writeTemp(dir, "fixture.onnx", fixture::u2netpLikeModel());
  const HeaderResult header = readModelHeader(path);
  QCOMPARE(header.status, HeaderStatus::Ok);
  QCOMPARE(header.header.irVersion, qint64(6));
  QCOMPARE(header.header.producer, QStringLiteral("fixture"));
  QCOMPARE(header.header.graphName, QStringLiteral("torch-jit-export"));
  QCOMPARE(header.header.inputs.size(), 1);
  QCOMPARE(header.header.inputs.first().name, QStringLiteral("input.1"));
  QCOMPARE(header.header.inputs.first().elemType, 1);
  QCOMPARE(header.header.inputs.first().dims.size(), 4);
  QCOMPARE(header.header.inputs.first().dims.at(3).value, qint64(320));
  QCOMPARE(header.header.outputs.size(), 3);
  QCOMPARE(header.header.outputs.first().name, QStringLiteral("1959"));
  QVERIFY(headerIsUsable(header.header));
}

void BackgroundModelsTest::onnxHeaderParsesSymbolicDimensions() {
  const QList<fixture::Tensor> inputs{{QStringLiteral("data"), 1, {0, 0}, {QStringLiteral("N"), QStringLiteral("C")}}};
  const QList<fixture::Tensor> outputs{{QStringLiteral("mask"), 1, {0, 0, 0, 0},
                                       {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}}};
  QTemporaryDir dir;
  const QString path = writeTemp(dir, "dynamic.onnx", fixture::encodeModel(inputs, outputs));
  const HeaderResult header = readModelHeader(path);
  QCOMPARE(header.status, HeaderStatus::Ok);
  QVERIFY(!header.header.inputs.first().dims.at(0).known());
  QCOMPARE(header.header.inputs.first().dims.at(0).param, QStringLiteral("N"));
}

void BackgroundModelsTest::onnxHeaderRejectsMalformedBytes() {
  QTemporaryDir dir;
  const QByteArray valid = fixture::u2netpLikeModel();

  const HeaderResult empty = readModelHeader(writeTemp(dir, "empty.onnx", {}));
  QCOMPARE(empty.status, HeaderStatus::NoGraph);

  const HeaderResult garbage = readModelHeader(writeTemp(dir, "garbage.onnx", QByteArrayLiteral("\xff\xff\xff\xff")));
  QCOMPARE(garbage.status, HeaderStatus::Malformed);

  const HeaderResult truncated = readModelHeader(writeTemp(dir, "cut.onnx", valid.left(valid.size() - 10)));
  QCOMPARE(truncated.status, HeaderStatus::Malformed);

  QByteArray grouped = valid;
  grouped.append(char(0x83));   // field 16, wire type 3 (deprecated group)
  const HeaderResult group = readModelHeader(writeTemp(dir, "group.onnx", grouped));
  QCOMPARE(group.status, HeaderStatus::Malformed);

  // A submessage declaring more bytes than the file holds is refused.
  QByteArray lying;
  lying.append(char(0x3A));                    // field 7 (graph), wire type 2
  lying.append(char(0x7F));                    // declares a 127-byte payload...
  lying.append(QByteArray(10, '\0'));          // ...but only 10 bytes follow
  const HeaderResult liar = readModelHeader(writeTemp(dir, "lying.onnx", lying));
  QCOMPARE(liar.status, HeaderStatus::Malformed);

  const HeaderResult missing = readModelHeader(dir.path() + QStringLiteral("/absent.onnx"));
  QCOMPARE(missing.status, HeaderStatus::Unreadable);

  const HeaderResult noInputs =
      readModelHeader(writeTemp(dir, "noinputs.onnx",
                                fixture::encodeModel({}, {fixture::u2netpFirstOutput()})));
  QCOMPARE(noInputs.status, HeaderStatus::NoInputs);

  const HeaderResult noOutputs =
      readModelHeader(writeTemp(dir, "nooutputs.onnx", fixture::encodeModel({fixture::u2netpInput()}, {})));
  QCOMPARE(noOutputs.status, HeaderStatus::NoOutputs);
}

void BackgroundModelsTest::tensorContractMatchingIsStrict() {
  QTemporaryDir dir;
  const QString path = writeTemp(dir, "m.onnx", fixture::u2netpLikeModel());
  const CatalogueEntry entry = fixtureEntry(fixture::u2netpLikeModel());
  const HeaderResult header = readModelHeader(path);
  QCOMPARE(header.status, HeaderStatus::Ok);
  QVERIFY(header.header.inputs.first().matches(entry.input));
  QVERIFY(header.header.outputs.first().matches(entry.firstOutput));

  TensorContract renamed = header.header.inputs.first();
  renamed.name = QStringLiteral("other");
  QVERIFY(!renamed.matches(entry.input));

  TensorContract reTyped = header.header.inputs.first();
  reTyped.elemType = 2;
  QVERIFY(!reTyped.matches(entry.input));

  TensorContract resized = header.header.inputs.first();
  resized.dims[3].value = 321;
  QVERIFY(!resized.matches(entry.input));

  TensorContract symbolic = header.header.inputs.first();
  symbolic.dims[3] = TensorDim{};
  symbolic.dims[3].param = QStringLiteral("W");
  QVERIFY(!symbolic.matches(entry.input));
}

void BackgroundModelsTest::validateModelFileVerifiesPinnedIntegrity() {
  QTemporaryDir dir;
  const QByteArray bytes = fixture::u2netpLikeModel();
  const CatalogueEntry entry = fixtureEntry(bytes);

  const ValidationResult ok = validateModelFile(writeTemp(dir, "good.onnx", bytes), entry);
  QCOMPARE(ok.status, ValidationStatus::Ok);
  QVERIFY(ok.headerParsed);
  QVERIFY(ok.errorId().isEmpty());
  QVERIFY(ok.message().isEmpty());

  QByteArray tampered = bytes;
  tampered[tampered.size() / 2] = char(tampered.at(tampered.size() / 2) ^ 0x40);
  const ValidationResult mismatch = validateModelFile(writeTemp(dir, "tampered.onnx", tampered), entry);
  QCOMPARE(mismatch.status, ValidationStatus::ShaMismatch);
  QCOMPARE(mismatch.errorId(), QStringLiteral("model-integrity-mismatch"));
  QVERIFY(!mismatch.message().isEmpty());

  CatalogueEntry wrongSize = entry;
  wrongSize.artifact.sizeBytes = bytes.size() + 1;
  const ValidationResult size = validateModelFile(writeTemp(dir, "good2.onnx", bytes), wrongSize);
  QCOMPARE(size.status, ValidationStatus::SizeMismatch);
  QCOMPARE(size.errorId(), QStringLiteral("model-size-mismatch"));

  CatalogueEntry wrongMd5 = entry;
  wrongMd5.artifact.md5 = QStringLiteral("00000000000000000000000000000000");
  const ValidationResult md5 = validateModelFile(writeTemp(dir, "good3.onnx", bytes), wrongMd5);
  QCOMPARE(md5.status, ValidationStatus::MdMismatch);

  const ValidationResult missing = validateModelFile(dir.path() + QStringLiteral("/none.onnx"), entry);
  QCOMPARE(missing.status, ValidationStatus::Missing);
  QCOMPARE(missing.errorId(), QStringLiteral("model-missing"));

  CatalogueEntry wrongContract = entry;
  wrongContract.input.dims[3].value = 128;
  const ValidationResult contract = validateModelFile(writeTemp(dir, "good4.onnx", bytes), wrongContract);
  QCOMPARE(contract.status, ValidationStatus::ContractMismatch);
  QCOMPARE(contract.errorId(), QStringLiteral("model-contract-mismatch"));
}

void BackgroundModelsTest::adoptModelKeepsTheStoreSafe() {
  QTemporaryDir dir;
  const QString storeDir = dir.path() + QStringLiteral("/models");
  const QByteArray good = fixture::u2netpLikeModel();
  const CatalogueEntry entry = fixtureEntry(good);

  // A tampered download is refused without touching the store.
  QByteArray tampered = good;
  tampered[1] = char(tampered.at(1) ^ 0x20);
  QString detail;
  QCOMPARE(adoptModel(writeTemp(dir, "bad.download", tampered), storeDir, entry, &detail),
           AdoptStatus::NotVerified);
  QCOMPARE(detail, QStringLiteral("model-integrity-mismatch"));
  QVERIFY(!QFile::exists(storeModelPath(storeDir, entry.id)));

  // A verified download lands atomically and the temporary file is gone.
  QCOMPARE(adoptModel(writeTemp(dir, "good.download", good), storeDir, entry, &detail), AdoptStatus::Ok);
  const QString adopted = storeModelPath(storeDir, entry.id);
  QVERIFY(QFile::exists(adopted));
  QVERIFY(!QFile::exists(storeTempPath(storeDir, entry.id)));

  // A failed adoption never removes the previous verified model.
  QCOMPARE(adoptModel(writeTemp(dir, "bad2.download", tampered), storeDir, entry, &detail),
           AdoptStatus::NotVerified);
  QVERIFY(QFile::exists(adopted));

  // A store directory under a regular file cannot be created.
  const QString blocker = writeTemp(dir, "blocker", QByteArrayLiteral("x"));
  QCOMPARE(adoptModel(writeTemp(dir, "good2.download", good), blocker + QStringLiteral("/sub"), entry, &detail),
           AdoptStatus::MoveFailed);
  QCOMPARE(detail, QStringLiteral("store-dir-failed"));
}

// --- EU-011: the inference pipeline ---

namespace {

// A 320x320 source with a hard colour edge. Same-size smooth scaling is
// pixel-exact, so every expected tensor value below is computed from first
// principles, not from the implementation.
QImage hardEdgeImage() {
  QImage image(320, 320, QImage::Format_RGB888);
  for (int y = 0; y < 320; ++y)
    for (int x = 0; x < 320; ++x)
      image.setPixel(x, y, x < 160 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
  return image;
}

float normalizedChannel(int value, int channel, const CatalogueEntry &entry) {
  return float((value / 255.0 - entry.preprocess.mean[channel]) / entry.preprocess.std[channel]);
}

// A synthetic entry whose output contract matches a small source image, so
// the mask resize is the pixel-exact same-size identity.
CatalogueEntry composeEntry(int width, int height) {
  CatalogueEntry entry = fixtureEntry(fixture::u2netpLikeModel());
  TensorDim rows, columns;
  rows.value = height;
  columns.value = width;
  entry.firstOutput.dims = {TensorDim{1, {}}, TensorDim{1, {}}, rows, columns};
  return entry;
}

}  // namespace

void BackgroundModelsTest::encodeInputTensorMatchesTheDecidedPolicy() {
  const CatalogueEntry entry = fixtureEntry(fixture::u2netpLikeModel());
  const QImage source = hardEdgeImage();
  const PipelineInput input = encodeInputTensor(source, entry);
  QCOMPARE(input.status, PipelineStatus::Ok);
  QCOMPARE(input.tensor.size(), qsizetype(1 * 3 * 320 * 320 * int(sizeof(float))));

  const auto *values = reinterpret_cast<const float *>(input.tensor.constData());
  const auto at = [values](int channel, int y, int x) {
    return values[(qsizetype(channel) * 320 + y) * qsizetype(320) + x];
  };
  // Left half pure red, right half pure blue: CHW layout, decided mean/std.
  for (const QPoint point : {QPoint(40, 160), QPoint(150, 20), QPoint(0, 0), QPoint(159, 319)}) {
    QCOMPARE(at(0, point.y(), point.x()), normalizedChannel(255, 0, entry));
    QCOMPARE(at(1, point.y(), point.x()), normalizedChannel(0, 1, entry));
    QCOMPARE(at(2, point.y(), point.x()), normalizedChannel(0, 2, entry));
  }
  for (const QPoint point : {QPoint(200, 160), QPoint(319, 0), QPoint(250, 300)}) {
    QCOMPARE(at(0, point.y(), point.x()), normalizedChannel(0, 0, entry));
    QCOMPARE(at(1, point.y(), point.x()), normalizedChannel(0, 1, entry));
    QCOMPARE(at(2, point.y(), point.x()), normalizedChannel(255, 2, entry));
  }
}

void BackgroundModelsTest::encodeInputTensorRefusesUnusableInputs() {
  const CatalogueEntry entry = fixtureEntry(fixture::u2netpLikeModel());
  const PipelineInput nullImage = encodeInputTensor(QImage(), entry);
  QCOMPARE(nullImage.status, PipelineStatus::ImageUnusable);
  QCOMPARE(nullImage.errorId(), QStringLiteral("image-unusable"));

  CatalogueEntry channels = entry;
  TensorDim one;
  one.value = 1;
  channels.input.dims[1] = one;   // a contract the pipeline cannot encode
  const PipelineInput wrongContract = encodeInputTensor(hardEdgeImage(), channels);
  QCOMPARE(wrongContract.status, PipelineStatus::ContractUnsupported);
  QCOMPARE(wrongContract.errorId(), QStringLiteral("model-contract-mismatch"));
}

void BackgroundModelsTest::composeMaskedImageProducesTheDecidedAlpha() {
  const int width = 13, height = 21;
  const CatalogueEntry entry = composeEntry(width, height);
  QImage source(width, height, QImage::Format_RGB32);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      source.setPixel(x, y, qRgb(30 + x, 60 + y, 90));
  source.setPixel(5, 7, qRgb(200, 100, 50));

  // A mask ramp along x: min-max normalization is the identity here, so the
  // decided alpha is floor(mask * 255) with the source colour untouched.
  QVector<float> mask;
  mask.reserve(width * height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      mask.append(float(x) / float(width - 1));

  const PipelineOutput output = composeMaskedImage(source, mask, entry);
  QCOMPARE(output.status, PipelineStatus::Ok);
  QCOMPARE(output.image.width(), width);
  QCOMPARE(output.image.height(), height);
  QCOMPARE(output.image.format(), QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (x == 5 && y == 7) continue;   // the deliberately marked pixel, checked below
      const QRgb pixel = output.image.pixel(x, y);
      QCOMPARE(qAlpha(pixel), int(std::floor(float(x) / float(width - 1) * 255.0f)));
      QCOMPARE(qRed(pixel), 30 + x);
      QCOMPARE(qGreen(pixel), 60 + y);
      QCOMPARE(qBlue(pixel), 90);
    }
  }
  const QRgb marked = output.image.pixel(5, 7);
  QCOMPARE(qRed(marked), 200);
  QCOMPARE(qGreen(marked), 100);
  QCOMPARE(qBlue(marked), 50);
}

void BackgroundModelsTest::composeMaskedImageDegenerateConstantIsDefined() {
  const int width = 5, height = 4;
  const CatalogueEntry entry = composeEntry(width, height);
  QImage source(width, height, QImage::Format_RGB32);
  source.fill(qRgb(10, 20, 30));

  QVector<float> positive(width * height, 0.3f);
  const PipelineOutput foreground = composeMaskedImage(source, positive, entry);
  QCOMPARE(foreground.status, PipelineStatus::Ok);
  QCOMPARE(qAlpha(foreground.image.pixel(2, 2)), 255);

  QVector<float> negative(width * height, -0.3f);
  const PipelineOutput background = composeMaskedImage(source, negative, entry);
  QCOMPARE(background.status, PipelineStatus::Ok);
  QCOMPARE(qAlpha(background.image.pixel(2, 2)), 0);
}

void BackgroundModelsTest::composeMaskedImageRefusesInvalidMasks() {
  const int width = 5, height = 4;
  const CatalogueEntry entry = composeEntry(width, height);
  QImage source(width, height, QImage::Format_RGB32);
  source.fill(qRgb(10, 20, 30));

  QVector<float> wrongCount(width * height + 1, 0.5f);
  const PipelineOutput wrong = composeMaskedImage(source, wrongCount, entry);
  QCOMPARE(wrong.status, PipelineStatus::MaskInvalid);
  QCOMPARE(wrong.errorId(), QStringLiteral("model-output-invalid"));

  QVector<float> withNaN(width * height, 0.5f);
  withNaN[2] = std::numeric_limits<float>::quiet_NaN();
  const PipelineOutput broken = composeMaskedImage(source, withNaN, entry);
  QCOMPARE(broken.status, PipelineStatus::MaskInvalid);
  QCOMPARE(broken.errorId(), QStringLiteral("model-output-invalid"));

  const PipelineOutput nullImage = composeMaskedImage(QImage(), QVector<float>(width * height, 0.5f), entry);
  QCOMPARE(nullImage.status, PipelineStatus::ImageUnusable);
}

QTEST_MAIN(BackgroundModelsTest)
#include "BackgroundModelsTest.moc"