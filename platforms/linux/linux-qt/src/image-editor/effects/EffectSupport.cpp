// EDITOR-EFFECTS-001: helpers shared by the built-in effect files. EE-007, EE-018.
#include "EffectSupport.h"

#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>

namespace xerahs::effects::support {
namespace {
bool sizeAcceptable(const QSize &size) {
  return size.isValid() && size.width() > 0 && size.height() > 0 && size.width() <= kMaxImageSide &&
         size.height() <= kMaxImageSide;
}

// Skia clamps a filtered channel to the byte gamut and rounds it to the nearest byte.
int filteredByte(float value) { return int(std::floor(std::clamp(value, 0.f, 255.f) + 0.5f)); }
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

EffectResult convolve3x3(const QImage &source, const std::array<float, 9> &kernel, float gain, float bias,
                         bool convolveAlpha, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const int width = source.width();
  const int height = source.height();
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      float sum[4] = {0.f, 0.f, 0.f, 0.f};
      for (int j = 0; j < 3; ++j) {
        const int sy = std::clamp(y + j - 1, 0, height - 1);
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(sy));
        for (int i = 0; i < 3; ++i) {
          const int sx = std::clamp(x + i - 1, 0, width - 1);
          const QRgb pixel = row[sx];
          const float coefficient = kernel[size_t(j * 3 + i)];
          sum[0] += coefficient * float(qRed(pixel));
          sum[1] += coefficient * float(qGreen(pixel));
          sum[2] += coefficient * float(qBlue(pixel));
          sum[3] += coefficient * float(qAlpha(pixel));
        }
      }
      const int alpha = convolveAlpha ? filteredByte(sum[3] * gain + bias) : qAlpha(input[x]);
      output[x] = qRgba(filteredByte(sum[0] * gain + bias), filteredByte(sum[1] * gain + bias),
                        filteredByte(sum[2] * gain + bias), alpha);
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

}  // namespace xerahs::effects::support
