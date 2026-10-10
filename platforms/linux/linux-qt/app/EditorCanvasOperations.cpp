#include "EditorCanvasOperations.h"

#include <QBuffer>
#include <QColorSpace>

#include <algorithm>
#include <new>

namespace xerahs::app {
using namespace xerahs::editor;

namespace {
PreparedCanvasEdit fail(const QString &error) {
  PreparedCanvasEdit result;
  result.result.error = error;
  return result;
}
bool cancelled(const CanvasControl &control) { return control.cancelled && control.cancelled(); }
void progress(const CanvasControl &control, int value) { if (control.progress) control.progress(value); }

PreparedCanvasEdit prepare(const AnnotationDocument &document, const QImage &source,
                           const CanvasOperation &operation, const CanvasControl &control) {
  if (source.isNull() || source.width() > kMaxDimension || source.height() > kMaxDimension ||
      qint64(source.width()) * source.height() > kMaxPixels) return fail(diagnostic::documentInvalid);
  const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
  if (argb.isNull()) return fail(diagnostic::documentTooLarge);
  ArgbImage input{argb.width(), argb.height(), {}};
  input.pixels.resize(std::size_t(input.width * input.height));
  for (int y = 0; y < argb.height(); ++y) {
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
    std::copy_n(row, argb.width(), input.pixels.begin() + std::size_t(y) * argb.width());
    progress(control, (y + 1) * 10 / argb.height());
  }
  const CanvasControl engineControl{control.cancelled, [&](int value) { progress(control, 10 + value * 70 / 100); }};
  PreparedCanvasEdit prepared;
  prepared.result = applyCanvasOperation(document, input, operation, engineControl);
  if (!prepared.result.changed || prepared.result.error) {
    if (!prepared.result.error) prepared.source = source;
    return prepared;
  }
  const ArgbImage &output = *prepared.result.image;
  QImage image(int(output.width), int(output.height), QImage::Format_ARGB32);
  if (image.isNull()) return fail(diagnostic::documentTooLarge);
  image.setColorSpace(source.colorSpace());
  for (int y = 0; y < image.height(); ++y) {
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
    std::copy_n(output.pixels.begin() + std::size_t(y) * output.width, image.width(), row);
    progress(control, 80 + (y + 1) * 10 / image.height());
  }
  QByteArray bytes;
  QBuffer png(&bytes);
  if (!png.open(QIODevice::WriteOnly) || !image.save(&png, "PNG")) return fail(QStringLiteral("canvas-encoding-failed"));
  // Encoding runs off the UI thread. Even cancellation during the codec's
  // synchronous work discards the result before it can enter history.
  if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
  prepared.result.document->sourceImagePng = bytes;
  prepared.result.image.reset();  // source is retained as immutable PNG history
  prepared.source = std::move(image);
  progress(control, 100);
  return prepared;
}
}  // namespace

PreparedCanvasEdit prepareCanvasEdit(const AnnotationDocument &document, const QImage &source,
                                    const CanvasOperation &operation, const CanvasControl &control) {
  try { return prepare(document, source, operation, control); }
  catch (const std::bad_alloc &) { return fail(diagnostic::documentTooLarge); }
}

CanvasPreview prepareCanvasPreview(const AnnotationDocument &document, const QImage &source,
                                   const CanvasOperation &operation, const CanvasControl &control) {
  try {
    CanvasPreview preview;
    preview.edit = prepareCanvasEdit(document, source, operation,
        {control.cancelled, [&](int p) { progress(control, p * 60 / 100); }});
    if (preview.edit.result.error) return preview;
    const QImage argb = preview.edit.source.convertToFormat(QImage::Format_ARGB32);
    if (argb.isNull()) return {fail(diagnostic::documentTooLarge), {}};
    ArgbImage input{argb.width(), argb.height(), {}};
    input.pixels.resize(std::size_t(input.width * input.height));
    for (int y = 0; y < argb.height(); ++y) {
      if (cancelled(control)) return {fail(QStringLiteral("canvas-cancelled")), {}};
      std::copy_n(reinterpret_cast<const QRgb *>(argb.constScanLine(y)), argb.width(), input.pixels.begin() + std::size_t(y) * argb.width());
      progress(control, 60 + (y + 1) * 5 / argb.height());
    }
    auto rendered = render(input, preview.edit.result.document->annotations,
        {control.cancelled, [&](int p) { progress(control, 65 + p * 30 / 100); }});
    if (rendered.error) return {fail(*rendered.error == RenderError::Cancelled ? QStringLiteral("canvas-cancelled") : diagnostic::documentInvalid), {}};
    QImage composite(argb.size(), QImage::Format_ARGB32);
    if (composite.isNull()) return {fail(diagnostic::documentTooLarge), {}};
    composite.setColorSpace(source.colorSpace());
    for (int y = 0; y < composite.height(); ++y) {
      if (cancelled(control)) return {fail(QStringLiteral("canvas-cancelled")), {}};
      std::copy_n(rendered.image->pixels.begin() + std::size_t(y) * argb.width(), argb.width(), reinterpret_cast<QRgb *>(composite.scanLine(y)));
      progress(control, 95 + (y + 1) * 5 / argb.height());
    }
    if (cancelled(control)) return {fail(QStringLiteral("canvas-cancelled")), {}};
    preview.composite = std::move(composite);
    return preview;
  } catch (const std::bad_alloc &) { return {fail(diagnostic::documentTooLarge), {}}; }
}

}  // namespace xerahs::app
