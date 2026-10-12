#include "SnapshotComposition.h"
#include <QColorSpace>
#include <cstring>
namespace xerahs::app {
using namespace xerahs::capture;
ComposedSnapshot composeSnapshot(const QImage &desktop, const PhysicalRect &rectangle, const Topology &topology,
                                const CompositeControl &control) {
  const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
  if (cancelled()) return {{}, {}, true};
  if (desktop.isNull()) return {{}, diagnostic::captureSourceFailed, false};
  const auto result = composite(rectangle, topology, [&](const Display &, const PhysicalRect &source) -> std::optional<RgbaImage> {
    if (cancelled() || source.left < 0 || source.top < 0 || source.right > desktop.width() || source.bottom > desktop.height()) return {};
    auto region = desktop.copy(QRect(int(source.left), int(source.top), int(source.width()), int(source.height())));
    if (cancelled()) return {};
    region = region.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (cancelled() || region.width() != source.width() || region.height() != source.height()) return {};
    RgbaImage pixels{source.width(), source.height(), {}}; pixels.pixels.resize(qsizetype(source.width() * source.height() * 4));
    for (int row = 0; row < region.height(); ++row) {
      if (cancelled()) return {};
      std::memcpy(pixels.pixels.data() + qsizetype(row) * source.width() * 4, region.constScanLine(row), std::size_t(source.width() * 4));
    }
    return pixels;
  }, control);
  if (result.cancelled || cancelled()) return {{}, {}, true};
  if (!result.image) return {{}, result.diagnostic.value_or(diagnostic::captureSourceFailed), false};
  QImage image(reinterpret_cast<const uchar *>(result.image->pixels.constData()), int(result.image->width), int(result.image->height),
      int(result.image->width * 4), QImage::Format_RGBA8888_Premultiplied);
  image = image.convertToFormat(QImage::Format_ARGB32);
  if (cancelled()) return {{}, {}, true};
  if (image.isNull()) return {{}, diagnostic::captureSourceFailed, false};
  image.setColorSpace(desktop.colorSpace());
  return {std::move(image), {}, false};
}
} // namespace xerahs::app
