// EC-004 / EA-010: validated, owned PNG assets for one editable insertion.
#pragma once
#include "image-editor/AnnotationRenderer.h"
#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

namespace xerahs::app {
constexpr int kMaxInsertedImageDimension = 16384;
constexpr qint64 kMaxInsertedImagePixels = 64LL * 1024 * 1024;
struct PreparedImageAsset {
  QByteArray png;
  QSize dimensions;
  QString diagnostic;
  bool valid() const { return !png.isEmpty() && dimensions.isValid() && !dimensions.isEmpty() && diagnostic.isEmpty(); }
};
// GUI ingress checks these limits before owning a clipboard image. Preparation
// and file decoding run on the worker; neither operation reads the clipboard.
bool insertImageSizeAllowed(QSize);
PreparedImageAsset prepareImageAsset(const QImage &, const xerahs::editor::RenderControl & = {});
PreparedImageAsset loadImageAsset(const QString &, const xerahs::editor::RenderControl & = {});
} // namespace xerahs::app
