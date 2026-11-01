#include "ImageRecognition.h"
#include "image-editor/AnnotationDocument.h"

#include <QColorSpace>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace xerahs::app;
namespace {
QImage printedText() {
  QImage image(1200, 240, QImage::Format_ARGB32);
  image.fill(Qt::white);
  image.setColorSpace(QColorSpace::SRgb);
  image.setText(QStringLiteral("Fixture"), QStringLiteral("private OCR input"));
  QPainter painter(&image);
  painter.setPen(Qt::black);
  QFont font(QStringLiteral("DejaVu Sans"));
  font.setPixelSize(64);
  painter.setFont(font);
  painter.drawText(QRect(40, 40, 1120, 160), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("LOCAL OCR TEST 123"));
  return image;
}
class EnvironmentValue {
public:
  explicit EnvironmentValue(const char *name) : m_name(name), m_set(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name)) {}
  ~EnvironmentValue() { if (m_set) qputenv(m_name, m_value); else qunsetenv(m_name); }
private:
  const char *m_name;
  bool m_set;
  QByteArray m_value;
};
}
class OcrRecognitionTest final : public QObject {
  Q_OBJECT
private slots:
  void availabilityMatchesBuild() {
#ifdef XERAHS_TESSERACT
    QVERIFY(textRecognitionAvailable());
#else
    QVERIFY(!textRecognitionAvailable());
    const auto result = recognizeTextImage(printedText());
    QCOMPARE(result.diagnostic, QStringLiteral("ocr-unavailable"));
    QVERIFY(result.values.isEmpty()); QVERIFY(!result.cancelled);
#endif
  }
  void rejectsInvalidAndOversizeInput() {
    const auto invalid = recognizeTextImage({});
    QCOMPARE(invalid.diagnostic, QStringLiteral("ocr-image-invalid"));
    QImage oversized(int(xerahs::editor::kMaxDimension + 1), 1, QImage::Format_Grayscale8);
    QVERIFY(!oversized.isNull());
    const auto large = recognizeTextImage(oversized);
    QCOMPARE(large.diagnostic, QStringLiteral("ocr-image-too-large"));
    QVERIFY(large.values.isEmpty());
  }
  void cancellationBeforeRecognitionHasNoResult() {
    const auto image = printedText(), before = image.copy();
    const auto result = recognizeTextImage(image, {[] { return true; }, {}});
    QVERIFY(result.cancelled); QVERIFY(result.values.isEmpty()); QVERIFY(result.diagnostic.isEmpty());
    QCOMPARE(image, before);
  }
  void recognizesKnownPrintedTextWithoutChangingSource() {
    if (!textRecognitionAvailable()) QSKIP("Tesseract dependency is not available in this build.");
    const auto image = printedText(), before = image.copy();
    QList<int> progress;
    const auto result = recognizeTextImage(image, {{}, [&](int value) { progress.append(value); }});
    QVERIFY2(result.diagnostic.isEmpty(), qPrintable(result.diagnostic)); QVERIFY(!result.cancelled);
    QCOMPARE(result.values.size(), 1); QCOMPARE(result.values.first().simplified(), QStringLiteral("LOCAL OCR TEST 123"));
    QCOMPARE(image, before); QCOMPARE(image.colorSpace(), before.colorSpace()); QCOMPARE(image.text(QStringLiteral("Fixture")), before.text(QStringLiteral("Fixture")));
    QVERIFY(!progress.isEmpty()); QCOMPARE(progress.last(), 100);
  }
  void blankAndInvisibleTextReturnZeroValues() {
    if (!textRecognitionAvailable()) QSKIP("Tesseract dependency is not available in this build.");
    QImage blank(900, 220, QImage::Format_ARGB32); blank.fill(Qt::white);
    const auto empty = recognizeTextImage(blank);
    QVERIFY2(empty.diagnostic.isEmpty(), qPrintable(empty.diagnostic)); QVERIFY(empty.values.isEmpty()); QVERIFY(!empty.cancelled);
    auto invisible = printedText();
    for (int y = 0; y < invisible.height(); ++y) {
      auto *row = reinterpret_cast<QRgb *>(invisible.scanLine(y));
      for (int x = 0; x < invisible.width(); ++x) row[x] &= 0x00FFFFFF;
    }
    const auto hidden = recognizeTextImage(invisible);
    QVERIFY2(hidden.diagnostic.isEmpty(), qPrintable(hidden.diagnostic)); QVERIFY(hidden.values.isEmpty()); QVERIFY(!hidden.cancelled);
  }
  void cancellationDuringNativeRecognitionDiscardsPartialText() {
    if (!textRecognitionAvailable()) QSKIP("Tesseract dependency is not available in this build.");
    QImage page(1200, 3000, QImage::Format_ARGB32); page.fill(Qt::white);
    const auto line = printedText();
    QPainter painter(&page);
    for (int y = 0; y < page.height(); y += line.height()) painter.drawImage(0, y, line);
    painter.end();
    std::atomic<bool> stop{false}; bool recognitionProgress = false;
    const auto result = recognizeTextImage(page, {[&] { return stop.load(); }, [&](int value) {
      if (value > 10 && value < 100) { recognitionProgress = true; stop.store(true); }
    }});
    QVERIFY(recognitionProgress); QVERIFY(result.cancelled); QVERIFY(result.values.isEmpty()); QVERIFY(result.diagnostic.isEmpty());
  }
  void missingEnglishDataIsActionable() {
    if (!textRecognitionAvailable()) QSKIP("Tesseract dependency is not available in this build.");
    QTemporaryDir missing; QVERIFY(missing.isValid());
    EnvironmentValue restore("TESSDATA_PREFIX"); qputenv("TESSDATA_PREFIX", missing.path().toUtf8());
    const auto result = recognizeTextImage(printedText());
    QCOMPARE(result.diagnostic, QStringLiteral("ocr-language-unavailable")); QVERIFY(result.values.isEmpty()); QVERIFY(!result.cancelled);
  }
};
QTEST_MAIN(OcrRecognitionTest)
#include "OcrRecognitionTest.moc"
