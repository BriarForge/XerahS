#include "ImageRecognition.h"
#include "QrRender.h"
#include "image-editor/AnnotationDocument.h"
#include <QPainter>
#include <QTest>
using namespace xerahs::app;
namespace {
QImage code(const QString &text) {
  const auto encoded = xerahs::tools::encodeQr(text, xerahs::tools::QrEcc::M);
  return renderQr(encoded.matrix, 300).image;
}
}
class QrRecognitionTest final : public QObject {
  Q_OBJECT
private slots:
  void invalidAndCancelled() {
    QCOMPARE(scanQrImage({}).diagnostic, QString("recognition-input-invalid"));
    QImage tooWide(int(xerahs::editor::kMaxDimension + 1), 1, QImage::Format_RGB32);
    QCOMPARE(scanQrImage(tooWide).diagnostic, QString("recognition-input-too-large"));
    const auto result = scanQrImage(code("private"), {[] { return true; }, {}});
    QVERIFY(result.cancelled); QVERIFY(result.values.isEmpty());
  }
  void realSingleUnicodeAndZero() {
#ifndef XERAHS_ZXING
    QVERIFY(!qrScanningAvailable()); QCOMPARE(scanQrImage(code("known")).diagnostic, QString("qr-scanner-unavailable"));
#else
    QVERIFY(qrScanningAvailable());
    const QString text = QString::fromUtf8("XerahS QR • café 日本語");
    const auto image = code(text); const auto original = image.copy();
    const auto result = scanQrImage(image); QVERIFY(result.diagnostic.isEmpty()); QCOMPARE(result.values, QStringList{text});
    QCOMPARE(image, original);
    QImage empty(320, 200, QImage::Format_RGB32); empty.fill(Qt::white);
    const auto zero = scanQrImage(empty); QVERIFY(zero.diagnostic.isEmpty()); QVERIFY(zero.values.isEmpty());
#endif
  }
  void multipleAndInvisiblePixels() {
#ifdef XERAHS_ZXING
    const auto first = code("first"), second = code("second");
    QImage image(first.width() + second.width() + 100, qMax(first.height(), second.height()) + 80, QImage::Format_ARGB32); image.fill(Qt::white);
    { QPainter painter(&image); painter.drawImage(10, 20, first); painter.drawImage(first.width() + 80, 20, second); }
    auto result = scanQrImage(image); QVERIFY(result.diagnostic.isEmpty()); result.values.sort(); QCOMPARE(result.values, QStringList({"first", "second"}));
    image.fill(Qt::white);
    { QPainter painter(&image); painter.drawImage(10, 20, first); painter.drawImage(first.width() + 80, 20, first); }
    QCOMPARE(scanQrImage(image).values, QStringList({"first", "first"}));
    QImage invisible = first.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < invisible.height(); ++y) for (int x = 0; x < invisible.width(); ++x) invisible.setPixel(x, y, invisible.pixel(x, y) & 0x00ffffff);
    QVERIFY(scanQrImage(invisible).values.isEmpty());
#endif
  }
};
QTEST_MAIN(QrRecognitionTest)
#include "QrRecognitionTest.moc"
