// Validated native image decoding shared by editor loading and comparison.
#pragma once
#include "image-editor/AnnotationRenderer.h"
#include <QImage>

namespace xerahs::app {
struct LoadedRaster {
  QImage image; // orientation applied once, non-premultiplied ARGB32, original colour metadata
  QByteArray fileBytes;
  QString diagnostic;
};
LoadedRaster loadRaster(const QString &path, const xerahs::editor::RenderControl &control = {});
} // namespace xerahs::app
