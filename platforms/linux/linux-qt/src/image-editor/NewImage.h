// EC-001/EC-017: validated, immutable new-image preparation.
#pragma once
#include "image-editor/AnnotationRenderer.h"

namespace xerahs::editor {
enum class NewImageProfile { SRgb, SRgbLinear, DisplayP3, AdobeRgb };
struct NewImageOptions {
  qint64 width = 800, height = 600;
  // Non-premultiplied bytes in the chosen profile; native colour conversion
  // belongs to the codec boundary, before pixel composition.
  Argb background = 0;
  NewImageProfile profile = NewImageProfile::SRgb;
};
std::optional<QString> validateNewImage(const NewImageOptions &options);
struct NewImageResult {
  std::optional<ArgbImage> image;
  std::optional<QString> error;
};
// Initial pixels are source-over the background at native size, top left.
// Pixels outside the requested canvas are cropped; uncovered areas use fill.
// Transparent background retains initial bytes exactly, including hidden RGB.
NewImageResult createImage(const NewImageOptions &options, const ArgbImage *initial = nullptr,
                           const RenderControl &control = {});
} // namespace xerahs::editor
