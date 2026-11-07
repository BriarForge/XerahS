#pragma once
#include <QCoreApplication>
#include <QMap>
#include <QString>
namespace xerahs::app {
// The action ids stay literal; only the prose that names an action is translated.
inline const char *captureActionLabelSource(const QString &id) {
  static const QMap<QString, const char *> sources{
    {"after-capture-window", QT_TRANSLATE_NOOP("CaptureActions", "Choose capture actions again")}, {"quick-task-menu", QT_TRANSLATE_NOOP("CaptureActions", "Show quick task menu")},
    {"image-effects", QT_TRANSLATE_NOOP("CaptureActions", "Apply image effects")}, {"annotation", QT_TRANSLATE_NOOP("CaptureActions", "Annotate image")}, {"save", QT_TRANSLATE_NOOP("CaptureActions", "Save image")},
    {"save-as", QT_TRANSLATE_NOOP("CaptureActions", "Save image as new PNG…")}, {"save-thumbnail", QT_TRANSLATE_NOOP("CaptureActions", "Save thumbnail")}, {"image-clipboard", QT_TRANSLATE_NOOP("CaptureActions", "Copy image to clipboard")},
    {"before-upload-window", QT_TRANSLATE_NOOP("CaptureActions", "Confirm before upload")}, {"upload", QT_TRANSLATE_NOOP("CaptureActions", "Upload image")}, {"url-clipboard", QT_TRANSLATE_NOOP("CaptureActions", "Copy URL to clipboard")}, {"ocr", QT_TRANSLATE_NOOP("CaptureActions", "Recognize text (OCR)")},
    {"ocr-clipboard", QT_TRANSLATE_NOOP("CaptureActions", "Copy recognized text")}, {"qr-scan", QT_TRANSLATE_NOOP("CaptureActions", "Scan QR code")}, {"pin", QT_TRANSLATE_NOOP("CaptureActions", "Pin image on screen")},
    {"print", QT_TRANSLATE_NOOP("CaptureActions", "Print image")}, {"custom-actions", QT_TRANSLATE_NOOP("CaptureActions", "Run custom actions")}, {"file-clipboard", QT_TRANSLATE_NOOP("CaptureActions", "Copy image file")},
    {"path-clipboard", QT_TRANSLATE_NOOP("CaptureActions", "Copy file path")}, {"reveal", QT_TRANSLATE_NOOP("CaptureActions", "Show saved file in folder")}, {"analyze", QT_TRANSLATE_NOOP("CaptureActions", "Analyze image")},
    {"delete", QT_TRANSLATE_NOOP("CaptureActions", "Delete saved file")}};
  return sources.value(id, nullptr);
}
inline QString captureActionLabel(const QString &id) {
  const auto *source = captureActionLabelSource(id);
  return source ? QCoreApplication::translate("CaptureActions", source) : id;
}
} // namespace xerahs::app
