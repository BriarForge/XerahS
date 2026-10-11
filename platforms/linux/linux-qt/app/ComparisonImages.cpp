#include "ComparisonImages.h"
#include "RasterSource.h"
#include <QColorSpace>
#include <algorithm>
#include <new>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
PreparedComparisonImage fail(const QString &diagnostic) {
  return {{}, diagnostic};
}
bool cancelled(const RenderControl &control) {
  return control.cancelled && control.cancelled();
}
void progress(const RenderControl &control, int value) {
  if (control.progress)
    control.progress(value);
}
} // namespace
PreparedComparisonImage prepareComparisonImage(const QImage &source, const std::vector<Annotation> &annotations,
                                               const RenderControl &control) {
  try {
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    const auto valid = comparisonLayout({source.width(), source.height()}, {source.width(), source.height()},
                                        ComparisonAlignment::TopLeft);
    if (valid.error)
      return fail(*valid.error);
    QImage tagged = source;
    if (!tagged.colorSpace().isValid())
      tagged.setColorSpace(QColorSpace::SRgb);
    const QImage normalized = tagged.convertedToColorSpace(QColorSpace::SRgb).convertToFormat(QImage::Format_ARGB32);
    if (normalized.isNull())
      return fail(diagnostic::documentTooLarge);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    progress(control, 10);
    if (annotations.empty()) {
      progress(control, 100);
      if (cancelled(control))
        return fail(QStringLiteral("canvas-cancelled"));
      return {normalized, {}};
    }
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    ArgbImage input{normalized.width(), normalized.height(), {}};
    input.pixels.resize(std::size_t(input.width * input.height));
    for (int y = 0; y < normalized.height(); ++y) {
      if (cancelled(control))
        return fail(QStringLiteral("canvas-cancelled"));
      std::copy_n(reinterpret_cast<const QRgb *>(normalized.constScanLine(y)), normalized.width(),
                  input.pixels.begin() + std::size_t(y) * input.width);
      progress(control, 10 + (y + 1) * 10 / normalized.height());
    }
    const auto rendered = renderComparisonSource(
        input, annotations, {control.cancelled, [&](int p) { progress(control, 20 + p * 70 / 100); }});
    if (rendered.error)
      return fail(*rendered.error == RenderError::Cancelled ? QStringLiteral("canvas-cancelled")
                  : *rendered.error == RenderError::UnsupportedAnnotation
                      ? QStringLiteral("comparison-annotation-unsupported")
                      : diagnostic::documentInvalid);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    QImage display(normalized.size(), QImage::Format_ARGB32);
    if (display.isNull())
      return fail(diagnostic::documentTooLarge);
    display.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < display.height(); ++y) {
      if (cancelled(control))
        return fail(QStringLiteral("canvas-cancelled"));
      std::copy_n(rendered.image->pixels.begin() + std::size_t(y) * input.width, display.width(),
                  reinterpret_cast<QRgb *>(display.scanLine(y)));
      progress(control, 90 + (y + 1) * 10 / display.height());
    }
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    return {std::move(display), {}};
  } catch (const std::bad_alloc &) {
    return fail(diagnostic::documentTooLarge);
  }
}
PreparedComparisonImage loadComparisonImage(const QString &path, const RenderControl &control) {
  const auto loaded = loadRaster(path, {control.cancelled, [&](int p) { progress(control, p / 2); }});
  if (!loaded.diagnostic.isEmpty())
    return fail(loaded.diagnostic);
  return prepareComparisonImage(loaded.image, {}, {control.cancelled, [&](int p) { progress(control, 50 + p / 2); }});
}
} // namespace xerahs::app
