#include "ImageRecognition.h"
#include "image-editor/AnnotationDocument.h"
#include <QPainter>
#ifdef XERAHS_ZXING
#include <ZXing/ReadBarcode.h>
#endif

namespace xerahs::app {
bool qrScanningAvailable() {
#ifdef XERAHS_ZXING
  return true;
#else
  return false;
#endif
}
ImageRecognitionResult scanQrImage(const QImage &source, const xerahs::editor::RenderControl &control) {
  if (control.cancelled && control.cancelled()) return {{}, {}, true};
  if (source.isNull()) return {{}, "recognition-input-invalid"};
  if (source.width() > xerahs::editor::kMaxDimension || source.height() > xerahs::editor::kMaxDimension ||
      qint64(source.width()) * source.height() > xerahs::editor::kMaxPixels) return {{}, "recognition-input-too-large"};
#ifndef XERAHS_ZXING
  return {{}, "qr-scanner-unavailable"};
#else
  try {
    if (control.progress) control.progress(5);
    QImage visible(source.size(), QImage::Format_RGB32);
    if (visible.isNull()) return {{}, "recognition-input-too-large"};
    visible.fill(Qt::white);
    { QPainter painter(&visible); painter.drawImage(0, 0, source); }
    const QImage gray = visible.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull()) return {{}, "recognition-input-too-large"};
    if (control.cancelled && control.cancelled()) return {{}, {}, true};
    if (control.progress) control.progress(20);
    const ZXing::ImageView image(gray.constBits(), gray.width(), gray.height(), ZXing::ImageFormat::Lum, gray.bytesPerLine());
    const auto barcodes = ZXing::ReadBarcodes(image, ZXing::ReaderOptions().setFormats(ZXing::BarcodeFormat::QRCode));
    ImageRecognitionResult result;
    for (const auto &barcode : barcodes) {
      if (control.cancelled && control.cancelled()) return {{}, {}, true};
      if (barcode.isValid()) {
        const auto text = barcode.text(ZXing::TextMode::Plain);
        result.values.append(QString::fromUtf8(text.data(), text.size()));
      }
    }
    if (control.progress) control.progress(100);
    if (control.cancelled && control.cancelled()) return {{}, {}, true};
    return result;
  } catch (...) { return {{}, "qr-scan-failed"}; }
#endif
}
} // namespace xerahs::app
