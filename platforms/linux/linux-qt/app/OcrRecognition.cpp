#include "ImageRecognition.h"
#include "image-editor/AnnotationDocument.h"

#include <algorithm>
#include <memory>
#include <new>

#ifdef XERAHS_TESSERACT
#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#endif

namespace xerahs::app {
namespace {
bool stopping(const xerahs::editor::RenderControl &control) {
  return control.cancelled && control.cancelled();
}
ImageRecognitionResult cancelled() { return {{}, {}, true}; }
ImageRecognitionResult failed(const QString &code) { return {{}, code, false}; }
}

bool textRecognitionAvailable() {
#ifdef XERAHS_TESSERACT
  return true;
#else
  return false;
#endif
}

ImageRecognitionResult recognizeTextImage(const QImage &source, const xerahs::editor::RenderControl &control) {
  try {
    if (stopping(control)) return cancelled();
    if (source.isNull()) return failed(QStringLiteral("ocr-image-invalid"));
    if (source.width() > xerahs::editor::kMaxDimension || source.height() > xerahs::editor::kMaxDimension ||
        qint64(source.width()) * source.height() > xerahs::editor::kMaxPixels)
      return failed(QStringLiteral("ocr-image-too-large"));
#ifndef XERAHS_TESSERACT
    return failed(QStringLiteral("ocr-unavailable"));
#else
    const QImage rgba = source.convertToFormat(QImage::Format_ARGB32);
    QImage gray(source.size(), QImage::Format_Grayscale8);
    if (rgba.isNull() || gray.isNull()) return failed(QStringLiteral("ocr-image-too-large"));
    for (int y = 0; y < rgba.height(); ++y) {
      if (stopping(control)) return cancelled();
      const auto *input = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
      auto *output = gray.scanLine(y);
      for (int x = 0; x < rgba.width(); ++x) {
        const auto pixel = input[x];
        const int alpha = qAlpha(pixel), white = 255 * (255 - alpha);
        output[x] = uchar(qGray((qRed(pixel) * alpha + white + 127) / 255,
                               (qGreen(pixel) * alpha + white + 127) / 255,
                               (qBlue(pixel) * alpha + white + 127) / 255));
      }
      if (control.progress) control.progress((y + 1) * 10 / rgba.height());
    }
    if (stopping(control)) return cancelled();
    tesseract::TessBaseAPI api;
    if (api.Init(nullptr, "eng", tesseract::OEM_LSTM_ONLY) != 0)
      return failed(QStringLiteral("ocr-language-unavailable"));
    if (stopping(control)) return cancelled();
    api.SetPageSegMode(tesseract::PSM_AUTO);
    api.SetImage(gray.constBits(), gray.width(), gray.height(), 1, int(gray.bytesPerLine()));
    api.SetSourceResolution(300);
    if (stopping(control)) return cancelled();
    auto monitorControl = control;
    tesseract::ETEXT_DESC monitor;
    monitor.cancel_this = &monitorControl;
    monitor.cancel = [](void *context, int) {
      return stopping(*static_cast<xerahs::editor::RenderControl *>(context));
    };
    monitor.progress_callback2 = [](tesseract::ETEXT_DESC *monitor, int, int, int, int) {
      const auto &control = *static_cast<xerahs::editor::RenderControl *>(monitor->cancel_this);
      if (control.progress) control.progress(10 + std::clamp(int(monitor->progress), 0, 100) * 89 / 100);
      return !stopping(control);
    };
    const int recognized = api.Recognize(&monitor);
    if (stopping(control)) return cancelled();
    if (recognized != 0) return failed(QStringLiteral("ocr-recognition-failed"));
    const std::unique_ptr<char[]> utf8(api.GetUTF8Text());
    if (!utf8) return failed(QStringLiteral("ocr-recognition-failed"));
    if (stopping(control)) return cancelled();
    const QString text = QString::fromUtf8(utf8.get());
    if (control.progress) control.progress(100);
    if (stopping(control)) return cancelled();
    return {text.trimmed().isEmpty() ? QStringList{} : QStringList{text}, {}, false};
#endif
  } catch (const std::bad_alloc &) {
    return failed(QStringLiteral("ocr-image-too-large"));
  } catch (...) {
    return failed(QStringLiteral("ocr-recognition-failed"));
  }
}
} // namespace xerahs::app
