#pragma once
#include "image-editor/AnnotationRenderer.h"
#include <QImage>
#include <QStringList>

namespace xerahs::app {
enum class ImageRecognitionKind { QrCode, Text };
struct ImageRecognitionResult {
  QStringList values;
  QString diagnostic;
  bool cancelled = false;
};
bool qrScanningAvailable();
bool textRecognitionAvailable();
ImageRecognitionResult scanQrImage(const QImage &, const xerahs::editor::RenderControl & = {});
ImageRecognitionResult recognizeTextImage(const QImage &, const xerahs::editor::RenderControl & = {});
} // namespace xerahs::app
