// EDITOR-UTILITIES-001 / EU-009, EU-011: the approved inference engine against
// the real onnxruntime. Every model here is a hand-encoded executable fixture
// (tests/OnnxFixture.h): a one-node ReduceMean whose output values are known
// from first principles, a nodeless model that passes the header parser but
// must be refused by the runtime, and a name-mismatch variant for the
// contract cross-check. No network, no real model file.
#include "OnnxMaskEngine.h"

#include "OnnxFixture.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace xerahs::app;
namespace fixture = OnnxFixture;
using xerahs::bg::CatalogueEntry;
using xerahs::bg::DevicePolicy;
using xerahs::bg::TensorContract;
using xerahs::bg::TensorDim;

namespace {

QString writeTemp(const QTemporaryDir &dir, const char *name, const QByteArray &bytes) {
  const QString path = dir.filePath(QLatin1String(name));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  file.write(bytes);
  file.close();
  return path;
}

// A catalogue entry whose contracts describe the ReduceMean fixture. The
// engine never checks checksums (the store validated those); only the tensor
// contract matters here.
CatalogueEntry reduceMeanEntry() {
  CatalogueEntry entry;
  entry.id = QStringLiteral("u2netp");
  entry.label = QStringLiteral("fixture");
  entry.preprocess.mean[0] = 0.485;
  entry.preprocess.mean[1] = 0.456;
  entry.preprocess.mean[2] = 0.406;
  entry.preprocess.std[0] = 0.229;
  entry.preprocess.std[1] = 0.224;
  entry.preprocess.std[2] = 0.225;
  auto contract = [](const QString &name, const QList<qint64> &dims) {
    TensorContract tensor;
    tensor.name = name;
    tensor.elemType = 1;
    for (const qint64 dim : dims) {
      TensorDim value;
      value.value = dim;
      tensor.dims.append(value);
    }
    return tensor;
  };
  entry.input = contract(QStringLiteral("input.1"), {1, 3, 320, 320});
  entry.firstOutput = contract(QStringLiteral("1959"), {1, 1, 320, 320});
  return entry;
}

// A 320x320 source with a hard colour edge: the left half pure red, the
// right half pure blue. Same-size smooth scaling is pixel-exact, so the
// expected tensor values and mask means are known exactly.
QImage hardEdgeImage() {
  QImage image(320, 320, QImage::Format_RGB888);
  for (int y = 0; y < 320; ++y) {
    auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
    Q_UNUSED(line);
  }
  for (int y = 0; y < 320; ++y)
    for (int x = 0; x < 320; ++x)
      image.setPixel(x, y, x < 160 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
  return image;
}

// The channel mean of the normalized input for one half of the image.
float channelMean(int r, int g, int b, const CatalogueEntry &entry) {
  const float values[3] = {
      float((r / 255.0 - entry.preprocess.mean[0]) / entry.preprocess.std[0]),
      float((g / 255.0 - entry.preprocess.mean[1]) / entry.preprocess.std[1]),
      float((b / 255.0 - entry.preprocess.mean[2]) / entry.preprocess.std[2]),
  };
  return (values[0] + values[1] + values[2]) / 3.0f;
}

}  // namespace

class OnnxMaskEngineTest final : public QObject {
  Q_OBJECT

private slots:
  void infoReportsTheLinkedEngine();
  void computeRunsTheFixtureAndComposesTheDecidedOutput();
  void computeRejectsGarbageThatPassedTheHeaderParser();
  void computeCatchesAContractMismatchAtTheRuntime();
  void computeRejectsGpuRequestOnTheCpuBuild();
  void cancelBeforeComputeIsReportedAndReset();
};

void OnnxMaskEngineTest::infoReportsTheLinkedEngine() {
  auto engine = makeOnnxMaskEngine();
  QVERIFY(engine);
  const xerahs::bg::EngineInfo info = engine->info();
  QVERIFY(info.linked);
  QVERIFY2(info.name.startsWith(QStringLiteral("onnxruntime 1.")),
           qPrintable(info.name));
  QVERIFY(info.cpu);
  // Self-consistent device honesty: GPU is only reported when a hardware
  // execution provider actually ships with the linked runtime.
  QCOMPARE(info.gpu, info.detail.contains(QStringLiteral("CUDAExecutionProvider")) ||
                         info.detail.contains(QStringLiteral("ROCmExecutionProvider")) ||
                         info.detail.contains(QStringLiteral("TensorrtExecutionProvider")));
}

void OnnxMaskEngineTest::computeRunsTheFixtureAndComposesTheDecidedOutput() {
  QTemporaryDir dir;
  const QString modelPath = writeTemp(dir, "mean.onnx", fixture::encodeReduceMeanModel());
  const CatalogueEntry entry = reduceMeanEntry();
  auto engine = makeOnnxMaskEngine();
  engine->resetCancel();

  const xerahs::bg::EngineResult result =
      engine->compute(hardEdgeImage(), modelPath, entry, DevicePolicy::Cpu);
  QVERIFY2(result.ok, qPrintable(result.errorId + QLatin1Char(' ') + result.message));
  QVERIFY(!result.cancelled);

  // EU-011: source dimensions preserved, colour preserved, defined alpha.
  QCOMPARE(result.image.width(), 320);
  QCOMPARE(result.image.height(), 320);
  QCOMPARE(result.image.format(), QImage::Format_ARGB32);
  // The mask mean is lower on the red half than the blue half, so min-max
  // normalization maps red to alpha 0 and blue to alpha 255 - derived from
  // the decided preprocessing, not from the implementation.
  const float redMean = channelMean(255, 0, 0, entry);
  const float blueMean = channelMean(0, 0, 255, entry);
  QVERIFY(redMean < blueMean);
  for (const QPoint point : {QPoint(40, 160), QPoint(10, 200)}) {
    const QRgb pixel = result.image.pixel(point.x(), point.y());
    QCOMPARE(qAlpha(pixel), 0);
    QCOMPARE(qRed(pixel), 255);
    QCOMPARE(qGreen(pixel), 0);
    QCOMPARE(qBlue(pixel), 0);
  }
  for (const QPoint point : {QPoint(200, 160), QPoint(310, 20)}) {
    const QRgb pixel = result.image.pixel(point.x(), point.y());
    QCOMPARE(qAlpha(pixel), 255);
    QCOMPARE(qRed(pixel), 0);
    QCOMPARE(qGreen(pixel), 0);
    QCOMPARE(qBlue(pixel), 255);
  }

  // A second run reuses the cached session and reproduces the same output.
  const xerahs::bg::EngineResult again =
      engine->compute(hardEdgeImage(), modelPath, entry, DevicePolicy::Cpu);
  QVERIFY(again.ok);
  QCOMPARE(again.image.pixel(40, 160), result.image.pixel(40, 160));
  QCOMPARE(again.image.pixel(200, 160), result.image.pixel(200, 160));
}

void OnnxMaskEngineTest::computeRejectsGarbageThatPassedTheHeaderParser() {
  QTemporaryDir dir;
  // u2netpLikeModel declares the right value infos but contains no nodes:
  // the bounded header parser accepts it; the runtime must refuse it.
  const QString modelPath = writeTemp(dir, "nodeless.onnx", fixture::u2netpLikeModel());
  auto engine = makeOnnxMaskEngine();
  const xerahs::bg::EngineResult result =
      engine->compute(hardEdgeImage(), modelPath, reduceMeanEntry(), DevicePolicy::Cpu);
  QVERIFY(!result.ok);
  QVERIFY(!result.cancelled);
  QCOMPARE(result.errorId, QStringLiteral("model-engine-open-failed"));
}

void OnnxMaskEngineTest::computeCatchesAContractMismatchAtTheRuntime() {
  QTemporaryDir dir;
  const QString modelPath =
      writeTemp(dir, "renamed.onnx",
                fixture::encodeReduceMeanModel(QStringLiteral("data"), QStringLiteral("1959")));
  auto engine = makeOnnxMaskEngine();
  const xerahs::bg::EngineResult result =
      engine->compute(hardEdgeImage(), modelPath, reduceMeanEntry(), DevicePolicy::Cpu);
  QVERIFY(!result.ok);
  QCOMPARE(result.errorId, QStringLiteral("model-contract-mismatch"));
}

void OnnxMaskEngineTest::computeRejectsGpuRequestOnTheCpuBuild() {
  QTemporaryDir dir;
  const QString modelPath = writeTemp(dir, "mean.onnx", fixture::encodeReduceMeanModel());
  auto engine = makeOnnxMaskEngine();
  if (engine->info().gpu)
    QSKIP("This build ships a hardware execution provider; the CPU-only refusal cannot be asserted.");
  const xerahs::bg::EngineResult result =
      engine->compute(hardEdgeImage(), modelPath, reduceMeanEntry(), DevicePolicy::Gpu);
  QVERIFY(!result.ok);
  QVERIFY(!result.cancelled);
  QCOMPARE(result.errorId, QStringLiteral("model-device-unavailable"));
  QVERIFY(!result.message.isEmpty());
}

void OnnxMaskEngineTest::cancelBeforeComputeIsReportedAndReset() {
  QTemporaryDir dir;
  const QString modelPath = writeTemp(dir, "mean.onnx", fixture::encodeReduceMeanModel());
  const CatalogueEntry entry = reduceMeanEntry();
  auto engine = makeOnnxMaskEngine();
  engine->resetCancel();

  engine->cancel();
  const xerahs::bg::EngineResult cancelled =
      engine->compute(hardEdgeImage(), modelPath, entry, DevicePolicy::Cpu);
  QVERIFY(!cancelled.ok);
  QVERIFY(cancelled.cancelled);
  QCOMPARE(cancelled.errorId, QStringLiteral("inference-cancelled"));

  // The flag is cooperative and must be reset for the next run to work.
  engine->resetCancel();
  const xerahs::bg::EngineResult resumed =
      engine->compute(hardEdgeImage(), modelPath, entry, DevicePolicy::Cpu);
  QVERIFY2(resumed.ok, qPrintable(resumed.errorId));
}

QTEST_MAIN(OnnxMaskEngineTest)
#include "OnnxMaskEngineTest.moc"
