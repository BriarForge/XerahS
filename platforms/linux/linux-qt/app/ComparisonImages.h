// Native decoding/colour conversion boundary for the Qt Core comparison model.
#pragma once
#include "image-editor/ImageComparison.h"
#include <QImage>

namespace xerahs::app {
struct PreparedComparisonImage {
  QImage image;
  QString diagnostic;
};
// Private display copies use sRGB. Untagged inputs are interpreted as sRGB;
// source pixels, profiles, annotations and files are never changed.
PreparedComparisonImage prepareComparisonImage(const QImage &source,
                                               const std::vector<xerahs::editor::Annotation> &annotations = {},
                                               const xerahs::editor::RenderControl &control = {});
PreparedComparisonImage loadComparisonImage(const QString &path, const xerahs::editor::RenderControl &control = {});
} // namespace xerahs::app
