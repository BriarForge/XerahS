#include "ImageInsert.h"
#include "RasterSource.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <new>

namespace xerahs::app {
namespace {
bool cancelled(const xerahs::editor::RenderControl &control) { return control.cancelled && control.cancelled(); }
PreparedImageAsset fail(const QString &code) { return {{}, {}, code}; }
void progress(const xerahs::editor::RenderControl &control, int value) { if (control.progress) control.progress(value); }
}
bool insertImageSizeAllowed(QSize size) {
  return size.isValid() && !size.isEmpty() && size.width() <= kMaxInsertedImageDimension &&
      size.height() <= kMaxInsertedImageDimension && qint64(size.width()) * size.height() <= kMaxInsertedImagePixels;
}
PreparedImageAsset prepareImageAsset(const QImage &source, const xerahs::editor::RenderControl &control) {
  try {
    if (cancelled(control)) return fail("canvas-cancelled");
    if (source.isNull()) return fail("source-empty");
    if (!insertImageSizeAllowed(source.size())) return fail("source-too-large");
    progress(control, 10);
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) return fail("source-too-large");
    if (cancelled(control)) return fail("canvas-cancelled");
    QByteArray png; QBuffer output(&png);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG")) return fail("image-insert-encode-failed");
    if (cancelled(control)) return fail("canvas-cancelled");
    progress(control, 100);
    if (cancelled(control)) return fail("canvas-cancelled");
    return {std::move(png), image.size(), {}};
  } catch (const std::bad_alloc &) { return fail("source-too-large"); }
  catch (...) { return fail("image-insert-failed"); }
}
PreparedImageAsset loadImageAsset(const QString &path, const xerahs::editor::RenderControl &control) {
  try {
    if (cancelled(control)) return fail("canvas-cancelled");
    if (!QFileInfo(path).isFile()) return fail("source-unsupported");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail("source-unsupported");
    const auto length = file.size();
    if (length <= 0) return fail("source-empty");
    if (length > kMaxEncodedImageBytes) return fail("source-too-large");
    QByteArray bytes;
    while (!file.atEnd()) {
      if (cancelled(control)) return fail("canvas-cancelled");
      const auto chunk = file.read(65536);
      if (file.error() != QFile::NoError) return fail("source-unsupported");
      if (bytes.size() + chunk.size() > kMaxEncodedImageBytes) return fail("source-too-large");
      bytes.append(chunk);
      progress(control, int(std::min(20.0, double(bytes.size()) / length * 20)));
    }
    QBuffer input(&bytes); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input); reader.setAutoTransform(true);
    if (!reader.canRead()) return fail("source-unsupported");
    const auto size = reader.size();
    if (!size.isValid() || size.isEmpty()) return fail("source-corrupt");
    // The per-asset ceiling is checked before any decoder allocates pixels.
    if (!insertImageSizeAllowed(size)) return fail("source-too-large");
    if (cancelled(control)) return fail("canvas-cancelled");
    const auto image = reader.read();
    if (cancelled(control)) return fail("canvas-cancelled");
    if (image.isNull()) return fail("source-corrupt");
    return prepareImageAsset(image, {control.cancelled, [&](int value) { progress(control, 20 + value * 80 / 100); }});
  } catch (const std::bad_alloc &) { return fail("source-too-large"); }
  catch (...) { return fail("image-insert-failed"); }
}
} // namespace xerahs::app
