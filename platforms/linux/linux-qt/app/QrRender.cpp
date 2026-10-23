#include "QrRender.h"

namespace xerahs::app {

int qrMinimumPixels(const tools::QrMatrix &matrix) { return matrix.size + 2 * kQrQuietZoneModules; }

QrImageResult renderQr(const tools::QrMatrix &matrix, int pixels) {
  const int span = qrMinimumPixels(matrix);
  if (matrix.size <= 0 || pixels < span || pixels > kQrMaxImagePixels) return {false, {}, QStringLiteral("qr-invalid-size")};
  const int scale = pixels / span, side = scale * span;
  QImage image(side, side, QImage::Format_Mono);
  image.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});  // index 0 black, 1 white
  image.fill(1);
  for (int y = 0; y < matrix.size; ++y)
    for (int x = 0; x < matrix.size; ++x) {
      if (!matrix.dark(x, y)) continue;
      const int px = (x + kQrQuietZoneModules) * scale, py = (y + kQrQuietZoneModules) * scale;
      for (int dy = 0; dy < scale; ++dy)
        for (int dx = 0; dx < scale; ++dx) image.setPixel(px + dx, py + dy, 0);
    }
  return {true, image, {}};
}

}  // namespace xerahs::app
