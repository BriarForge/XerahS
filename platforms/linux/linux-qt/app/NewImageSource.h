// Qt colour conversion and PNG boundary for EC-001.
#pragma once
#include "image-editor/NewImage.h"
#include <QColorSpace>
#include <QImage>

namespace xerahs::app {
struct PreparedNewImage {
  std::optional<xerahs::editor::AnnotationDocument> document;
  QImage source;
  QString diagnostic;
};
QColorSpace newImageColorSpace(xerahs::editor::NewImageProfile profile);
// The native UI supplies an sRGB background colour. Initial pixels retain
// their profile interpretation (untagged means sRGB) and are converted privately
// to the selected working profile before production composition.
PreparedNewImage prepareNewImage(const xerahs::editor::NewImageOptions &options, const QImage &initial = {},
                                 const xerahs::editor::RenderControl &control = {});
} // namespace xerahs::app
