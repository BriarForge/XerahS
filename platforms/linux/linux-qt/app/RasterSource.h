// Validated native image decoding shared by editor loading and comparison.
#pragma once
#include "image-editor/AnnotationRenderer.h"
#include <QImage>

namespace xerahs::app {
// Native I/O ceiling as well as decoded pixel/header limits. Check before
// buffering encoded files, including files whose size changes while reading.
constexpr qint64 kMaxEncodedImageBytes = 1073741824;
struct LoadedRaster {
  QImage image; // orientation applied once, non-premultiplied ARGB32, original colour metadata
  QByteArray fileBytes;
  QString diagnostic;
};
LoadedRaster loadRaster(const QString &path, const xerahs::editor::RenderControl &control = {});
} // namespace xerahs::app
