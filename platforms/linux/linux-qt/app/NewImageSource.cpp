#include "NewImageSource.h"
#include <QBuffer>
#include <QDateTime>
#include <algorithm>
#include <new>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
PreparedNewImage fail(const QString &error) {
  return {std::nullopt, {}, {}, error};
}
bool cancelled(const RenderControl &control) {
  return control.cancelled && control.cancelled();
}
void progress(const RenderControl &control, int value) {
  if (control.progress)
    control.progress(value);
}
} // namespace
QColorSpace newImageColorSpace(NewImageProfile profile) {
  switch (profile) {
  case NewImageProfile::SRgb:
    return QColorSpace::SRgb;
  case NewImageProfile::SRgbLinear:
    return QColorSpace::SRgbLinear;
  case NewImageProfile::DisplayP3:
    return QColorSpace::DisplayP3;
  case NewImageProfile::AdobeRgb:
    return QColorSpace::AdobeRgb;
  }
  return {};
}
PreparedNewImage prepareNewImage(const NewImageOptions &options, const QImage &initial, const RenderControl &control) {
  try {
    if (auto error = validateNewImage(options))
      return fail(*error);
    if (!initial.isNull())
      if (auto error = validateNewImage({initial.width(), initial.height()}))
        return fail(*error);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    const QColorSpace profile = newImageColorSpace(options.profile);
    QImage colour(1, 1, QImage::Format_ARGB32);
    if (colour.isNull())
      return fail(diagnostic::documentTooLarge);
    colour.setColorSpace(QColorSpace::SRgb);
    colour.fill(options.background);
    colour = colour.convertedToColorSpace(profile);
    if (colour.isNull())
      return fail(diagnostic::documentTooLarge);
    NewImageOptions encoded = options;
    encoded.background = colour.pixel(0, 0);
    std::optional<ArgbImage> pixels;
    if (!initial.isNull()) {
      QImage tagged = initial;
      if (!tagged.colorSpace().isValid())
        tagged.setColorSpace(QColorSpace::SRgb);
      const QImage normalized = tagged.convertedToColorSpace(profile).convertToFormat(QImage::Format_ARGB32);
      if (normalized.isNull())
        return fail(diagnostic::documentTooLarge);
      if (cancelled(control))
        return fail(QStringLiteral("canvas-cancelled"));
      pixels = ArgbImage{normalized.width(), normalized.height(), {}};
      pixels->pixels.resize(std::size_t(pixels->width * pixels->height));
      for (int y = 0; y < normalized.height(); ++y) {
        if (cancelled(control))
          return fail(QStringLiteral("canvas-cancelled"));
        std::copy_n(reinterpret_cast<const QRgb *>(normalized.constScanLine(y)), normalized.width(),
                    pixels->pixels.begin() + std::size_t(y) * normalized.width());
        progress(control, (y + 1) * 20 / normalized.height());
      }
    }
    progress(control, 20);
    const auto created = createImage(encoded, pixels ? &*pixels : nullptr,
                                     {control.cancelled, [&](int p) { progress(control, 20 + p * 50 / 100); }});
    if (created.error)
      return fail(*created.error);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    QImage source(int(options.width), int(options.height), QImage::Format_ARGB32);
    if (source.isNull())
      return fail(diagnostic::documentTooLarge);
    source.setColorSpace(profile);
    for (int y = 0; y < source.height(); ++y) {
      if (cancelled(control))
        return fail(QStringLiteral("canvas-cancelled"));
      std::copy_n(created.image->pixels.begin() + std::size_t(y) * source.width(), source.width(),
                  reinterpret_cast<QRgb *>(source.scanLine(y)));
      progress(control, 70 + (y + 1) * 20 / source.height());
    }
    AnnotationDocument document;
    document.canvasWidth = options.width;
    document.canvasHeight = options.height;
    document.createdAt = document.modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QImage display = profile == QColorSpace(QColorSpace::SRgb) ? source : source.convertedToColorSpace(QColorSpace::SRgb);
    if (display.isNull()) return fail(diagnostic::documentTooLarge);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    QBuffer png(&document.sourceImagePng);
    if (!png.open(QIODevice::WriteOnly) || !source.save(&png, "PNG"))
      return fail(QStringLiteral("canvas-encoding-failed"));
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    const auto parsed = parseDocument(serializeDocument(document));
    if (parsed.error)
      return fail(*parsed.error);
    progress(control, 100);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    return {std::move(document), std::move(source), display, {}};
  } catch (const std::bad_alloc &) {
    return fail(diagnostic::documentTooLarge);
  }
}
} // namespace xerahs::app
