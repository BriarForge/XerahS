#include "RasterSource.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <new>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
LoadedRaster fail(const QString &diagnostic) {
  return {{}, {}, diagnostic};
}
bool cancelled(const RenderControl &control) {
  return control.cancelled && control.cancelled();
}
void progress(const RenderControl &control, int value) {
  if (control.progress)
    control.progress(value);
}
} // namespace
LoadedRaster loadRaster(const QString &path, const RenderControl &control) {
  try {
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    if (!QFileInfo(path).isFile())
      return fail(diagnostic::sourceUnsupported);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      return fail(diagnostic::sourceUnsupported);
    const qint64 length = file.size();
    QByteArray bytes;
    while (!file.atEnd()) {
      if (cancelled(control))
        return fail(QStringLiteral("canvas-cancelled"));
      const QByteArray chunk = file.read(65536);
      if (file.error() != QFile::NoError)
        return fail(diagnostic::sourceUnsupported);
      bytes.append(chunk);
      if (length > 0)
        progress(control, int(std::min(10.0, double(bytes.size()) / length * 10)));
    }
    if (bytes.isEmpty())
      return fail(diagnostic::sourceEmpty);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (!reader.canRead())
      return fail(diagnostic::sourceUnsupported);
    const QSize size = reader.size();
    // Every supported input must have validated dimensions before pixel decode.
    if (!size.isValid() || size.isEmpty())
      return fail(diagnostic::sourceCorrupt);
    if (size.width() > kMaxDimension || size.height() > kMaxDimension ||
        qint64(size.width()) * size.height() > kMaxPixels)
      return fail(diagnostic::sourceTooLarge);
    progress(control, 20);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    QImage image = reader.read();
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    if (image.isNull())
      return fail(diagnostic::sourceCorrupt);
    if (image.width() > kMaxDimension || image.height() > kMaxDimension ||
        qint64(image.width()) * image.height() > kMaxPixels)
      return fail(diagnostic::sourceTooLarge);
    image = image.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
      return fail(diagnostic::sourceTooLarge);
    progress(control, 100);
    if (cancelled(control))
      return fail(QStringLiteral("canvas-cancelled"));
    return {std::move(image), std::move(bytes), {}};
  } catch (const std::bad_alloc &) {
    return fail(diagnostic::sourceTooLarge);
  }
}
} // namespace xerahs::app
