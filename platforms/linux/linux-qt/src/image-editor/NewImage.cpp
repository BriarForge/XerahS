#include "image-editor/NewImage.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace xerahs::editor {
namespace {
Argb over(Argb source, Argb background) {
  if ((background >> 24) == 0 || (source >> 24) == 255)
    return source;
  if ((source >> 24) == 0)
    return background;
  const double a = (source >> 24) / 255.0, ad = (background >> 24) / 255.0;
  const double ao = a + ad * (1 - a);
  const auto quantize = [](double value) { return quint32(std::clamp(std::floor(value * 255 + .5), 0.0, 255.0)); };
  Argb result = quantize(ao) << 24;
  for (int shift : {16, 8, 0}) {
    const double cs = ((source >> shift) & 255) / 255.0, cd = ((background >> shift) & 255) / 255.0;
    result |= quantize((cs * a + cd * ad * (1 - a)) / ao) << shift;
  }
  return result;
}
} // namespace
std::optional<QString> validateNewImage(const NewImageOptions &options) {
  if (options.width < 1 || options.height < 1)
    return diagnostic::documentInvalid;
  if (options.width > kMaxDimension || options.height > kMaxDimension || options.width * options.height > kMaxPixels)
    return diagnostic::documentTooLarge;
  switch (options.profile) {
  case NewImageProfile::SRgb:
  case NewImageProfile::SRgbLinear:
  case NewImageProfile::DisplayP3:
  case NewImageProfile::AdobeRgb:
    return std::nullopt;
  }
  return diagnostic::documentInvalid;
}
NewImageResult createImage(const NewImageOptions &options, const ArgbImage *initial, const RenderControl &control) {
  const auto fail = [](const QString &error) { return NewImageResult{std::nullopt, error}; };
  try {
    if (auto error = validateNewImage(options))
      return fail(*error);
    if (initial) {
      if (auto error = validateNewImage({initial->width, initial->height}))
        return fail(*error);
      if (initial->pixels.size() != std::size_t(initial->width * initial->height))
        return fail(diagnostic::documentInvalid);
    }
    const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
    if (cancelled())
      return fail(QStringLiteral("canvas-cancelled"));
    ArgbImage output{options.width, options.height, {}};
    output.pixels.resize(std::size_t(options.width * options.height));
    for (qint64 y = 0; y < options.height; ++y) {
      if (cancelled())
        return fail(QStringLiteral("canvas-cancelled"));
      auto row = output.pixels.begin() + std::size_t(y * options.width);
      std::fill_n(row, std::size_t(options.width), options.background);
      if (initial && y < initial->height)
        for (qint64 x = 0; x < std::min(options.width, initial->width); ++x)
          row[x] = over(initial->at(x, y), options.background);
      if (control.progress)
        control.progress(int((y + 1) * 100 / options.height));
    }
    if (cancelled())
      return fail(QStringLiteral("canvas-cancelled"));
    return {std::move(output), std::nullopt};
  } catch (const std::bad_alloc &) {
    return fail(diagnostic::documentTooLarge);
  }
}
} // namespace xerahs::editor
