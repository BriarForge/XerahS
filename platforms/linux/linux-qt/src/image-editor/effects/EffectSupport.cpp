// EDITOR-EFFECTS-001: helpers shared by the built-in effect files. EE-007, EE-018.
#include "EffectSupport.h"

#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

QImage approximateGaussianBlur(const QImage &source, float sigma, BlurBorder border, const EffectContext &context,
                               int progressLo, int progressHi) {
  if (context.cancelled() || source.isNull()) return {};
  const int width = source.width(), height = source.height();
  const int span = std::max(1, progressHi - progressLo);
  auto phase = [&](int index) { return progressLo + span * index / 3; };
  if (!(sigma > 0.f)) {
    QImage copy(source.size(), QImage::Format_ARGB32);
    if (copy.isNull()) return {};
    copy.setColorSpace(source.colorSpace());
    if (!runRows(context, height, progressLo, progressHi, [&](int y) {
          std::memcpy(copy.scanLine(y), source.constScanLine(y), size_t(width) * 4);
        }))
      return {};
    return copy;
  }
  const int radius = std::max(1, int(std::ceil(double(sigma) * 3.0)));
  std::vector<float> kernel(size_t(radius * 2 + 1));
  const float sigmaSq = sigma * sigma;
  float weight = 0.f;
  for (int i = -radius; i <= radius; ++i) {
    const float value = std::exp(-0.5f * float(i * i) / sigmaSq);
    kernel[size_t(i + radius)] = value;
    weight += value;
  }
  for (float &value : kernel) value /= weight;

  std::vector<float> in(size_t(width) * size_t(height) * 4), out(in.size());
  if (!runRows(context, height, phase(0), phase(1), [&](int y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        float *dst = in.data() + size_t(y) * size_t(width) * 4;
        for (int x = 0; x < width; ++x) {
          const float alpha = qAlpha(row[x]) / 255.f;
          dst[x * 4] = qRed(row[x]) / 255.f * alpha;
          dst[x * 4 + 1] = qGreen(row[x]) / 255.f * alpha;
          dst[x * 4 + 2] = qBlue(row[x]) / 255.f * alpha;
          dst[x * 4 + 3] = alpha;
        }
      }))
    return {};

  auto sample = [&](const std::vector<float> &buffer, int x, int y, int widthLimit, int heightLimit, int channel) {
    if (x < 0 || y < 0 || x >= widthLimit || y >= heightLimit) {
      if (border == BlurBorder::Decal) return 0.f;
      x = std::clamp(x, 0, widthLimit - 1);
      y = std::clamp(y, 0, heightLimit - 1);
    }
    return buffer[(size_t(y) * size_t(widthLimit) + size_t(x)) * 4 + size_t(channel)];
  };
  if (!runRows(context, height, phase(1), phase(2), [&](int y) {
        for (int x = 0; x < width; ++x) {
          float *dst = out.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
          for (int c = 0; c < 4; ++c) {
            float sum = 0.f;
            for (int k = -radius; k <= radius; ++k) sum += kernel[size_t(k + radius)] * sample(in, x + k, y, width, height, c);
            dst[c] = sum;
          }
        }
      }))
    return {};
  if (!runRows(context, height, phase(2), progressHi, [&](int y) {
        for (int x = 0; x < width; ++x) {
          float blurred[4]{0, 0, 0, 0};
          for (int c = 0; c < 4; ++c) {
            float sum = 0.f;
            for (int k = -radius; k <= radius; ++k)
              sum += kernel[size_t(k + radius)] * sample(out, x, y + k, width, height, c);
            blurred[c] = sum;
          }
          float *dst = in.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
          for (int c = 0; c < 4; ++c) dst[c] = blurred[c];
        }
      }))
    return {};

  QImage result(source.size(), QImage::Format_ARGB32);
  if (result.isNull()) return {};
  result.setColorSpace(source.colorSpace());
  for (int y = 0; y < height; ++y) {
    auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const float *px = in.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
      const float alpha = px[3];
      if (alpha <= 1e-6f) {
        row[x] = 0;
        continue;
      }
      auto channel = [&](float premul) {
        const float straight = std::clamp(premul / alpha, 0.f, 1.f);
        return int(std::nearbyintf(std::clamp(straight, 0.f, 1.f) * 255.f));
      };
      row[x] = qRgba(channel(px[0]), channel(px[1]), channel(px[2]), int(std::nearbyintf(std::clamp(alpha, 0.f, 1.f) * 255.f)));
    }
  }
  return result;
}

}  // namespace xerahs::effects::support
