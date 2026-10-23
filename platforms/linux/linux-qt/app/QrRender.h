// EDITOR-UTILITIES-001 / EU-003: a QR matrix becomes a black-on-white image with
// a 4-module quiet zone and a whole number of pixels per module, so the preview
// is identical for identical input.
#pragma once

#include "tools/QrEncoder.h"

#include <QImage>
#include <QString>

namespace xerahs::app {

constexpr int kQrQuietZoneModules = 4;
constexpr int kQrMaxImagePixels = 4096;

// Smallest legal image side for this matrix: one pixel per module plus quiet zone.
int qrMinimumPixels(const tools::QrMatrix &matrix);

// error is empty on success, or "qr-invalid-size" when pixels is below
// qrMinimumPixels or above kQrMaxImagePixels. The image side is the largest
// whole multiple of (modules + quiet zone) that fits in pixels.
struct QrImageResult {
  bool ok = false;
  QImage image;
  QString error;
};
QrImageResult renderQr(const tools::QrMatrix &matrix, int pixels);

}  // namespace xerahs::app
