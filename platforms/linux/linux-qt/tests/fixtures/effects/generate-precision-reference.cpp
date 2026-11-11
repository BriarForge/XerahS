// Regenerate corrected hashes using only the independent drawing plans.
// From this directory:
// c++ -std=c++17 -fPIC generate-precision-reference.cpp -o /tmp/xerahs-precision-reference $(pkg-config --cflags --libs Qt6Gui)
// Run QT_QPA_PLATFORM=offscreen /tmp/xerahs-precision-reference and review the
// resulting JSON; retain the original historical hashes in golden-hashes.json.
#include "../../EffectPrecisionReference.h"
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
QImage input(int index) {
  const QSize sizes[]{QSize(32, 24), QSize(64, 48), QSize(17, 9)};
  QImage image(sizes[index], QImage::Format_ARGB32);
  quint32 state = 0x12345678;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      state = state * 1664525u + 1013904223u;
      const int n = int(state >> 24), r = x * 255 / (image.width() - 1),
                g = y * 255 / (image.height() - 1);
      image.setPixel(x, y,
                     index == 1 ? qRgba((r * 3 + n) / 4, (g * 3 + n) / 4,
                                        (r + g + n * 2) / 4, 255)
                                : qRgba(r, g, (x * 31 + y * 17) % 256,
                                        (x * 19 + y * 23) % 256));
    }
  return image;
}
QString digest(const QImage &image) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArray::number(image.width()) + "x" +
               QByteArray::number(image.height()) + ":");
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto p = image.pixel(x, y);
      const char rgba[]{char(qRed(p)), char(qGreen(p)), char(qBlue(p)),
                        char(qAlpha(p))};
      hash.addData(QByteArrayView(rgba, 4));
    }
  return QString::fromLatin1(hash.result().toHex());
}
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QJsonObject effects;
  for (const auto &id : precision_reference::ids()) {
    QJsonArray outputs;
    for (int i = 0; i < 3; ++i)
      outputs.append(QJsonObject{
          {"error", ""},
          {"sha256", digest(precision_reference::render(id, input(i)))}});
    effects.insert(id, outputs);
  }
  const auto json = QJsonDocument(effects).toJson();
  std::fwrite(json.data(), 1, size_t(json.size()), stdout);
}
