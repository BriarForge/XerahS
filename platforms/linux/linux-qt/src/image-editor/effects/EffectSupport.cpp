// EDITOR-EFFECTS-001: helpers shared by the built-in effect files. EE-007, EE-018.
#include "EffectSupport.h"

#include <QFileInfo>
#include <QImageReader>

namespace xerahs::effects::support {
namespace {
bool sizeAcceptable(const QSize &size) {
  return size.isValid() && size.width() > 0 && size.height() > 0 && size.width() <= kMaxImageSide &&
         size.height() <= kMaxImageSide;
}
}

bool validateImageFile(const QString &path) {
  if (path.trimmed().isEmpty()) return false;
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile()) return false;
  QImageReader reader(path);
  if (!reader.canRead()) return false;
  // Some formats do not report a size before decoding; loadImageFile re-checks after the read.
  const QSize size = reader.size();
  return !size.isValid() || sizeAcceptable(size);
}

QImage loadImageFile(const QString &path) {
  if (!validateImageFile(path)) return {};
  QImageReader reader(path);
  QImage image = reader.read();
  if (image.isNull() || !sizeAcceptable(image.size())) return {};
  return image.convertToFormat(QImage::Format_ARGB32);
}

}  // namespace xerahs::effects::support
