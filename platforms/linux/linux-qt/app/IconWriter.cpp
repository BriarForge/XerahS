#include "IconWriter.h"

#include <QBuffer>
#include <QDataStream>
#include <QPainter>

#include <algorithm>

namespace xerahs::app {
namespace {

constexpr int kDirectorySize = 6;
constexpr int kEntrySize = 16;
constexpr int kPaletteSize = 256;

// The sizes the contract offers.
const QList<int> &offeredSizes() {
  static const QList<int> sizes{16, 32, 48, 64, 128, 256};
  return sizes;
}

bool isOfferedSize(int size) {
  return std::binary_search(offeredSizes().cbegin(), offeredSizes().cend(), size);
}

// Rows are padded to a four byte boundary.
int paddedRowSize(int pixels, int bytesPerPixel) {
  return ((pixels * bytesPerPixel + 3) / 4) * 4;
}

int paddedMaskRowSize(int pixels) {
  return ((pixels + 31) / 32) * 4;
}

// The entry canvas: the source scaled to fit, centred on a transparent square.
QImage scaledEntry(const QImage &source, int size) {
  QImage canvas(size, size, QImage::Format_ARGB32);
  canvas.fill(Qt::transparent);
  if (canvas.isNull()) return {};
  const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
  if (argb.isNull()) return {};
  const QImage scaled = argb.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
  painter.end();
  return canvas;
}

// The palette conversion sees opaque colours; transparency is carried by the
// AND mask, so the alpha channel is dropped here on purpose.
QImage quantize(const QImage &argb) {
  QImage opaque = argb.convertToFormat(QImage::Format_ARGB32);
  if (opaque.isNull()) return {};
  for (int y = 0; y < opaque.height(); ++y) {
    auto *line = reinterpret_cast<QRgb *>(opaque.scanLine(y));
    for (int x = 0; x < opaque.width(); ++x) line[x] |= 0xff000000;
  }
  return opaque.convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither | Qt::AvoidDither);
}

void writeHeader(QDataStream &stream, int size) {
  stream << quint32(40) << qint32(size) << qint32(2 * size) << quint16(1) << quint16(8) << quint32(0)
         << quint32(0) << qint32(0) << qint32(0) << quint32(0) << quint32(0);
}

void writePalette(QDataStream &stream, const QImage &indexed) {
  const QVector<QRgb> colors = indexed.colorTable();
  for (int i = 0; i < kPaletteSize; ++i) {
    const QRgb color = i < colors.size() ? colors.at(i) : qRgb(0, 0, 0);
    stream << quint8(color & 0xff) << quint8((color >> 8) & 0xff) << quint8((color >> 16) & 0xff)
           << quint8(0);
  }
}

// The colour rows run bottom-up, each padded to a four byte boundary.
void writeColourRows(QDataStream &stream, const QImage &indexed, int size) {
  const int stride = paddedRowSize(size, 1);
  for (int row = 0; row < size; ++row) {
    const int y = size - 1 - row;
    for (int x = 0; x < size; ++x) {
      const int index = indexed.pixelIndex(x, y);
      stream << quint8(index < 0 ? 0 : index);
    }
    for (int pad = size; pad < stride; ++pad) stream << quint8(0);
  }
}

// The mask runs bottom-up as well: a set bit means the pixel is transparent.
void writeMaskRows(QDataStream &stream, const QImage &argb, int size) {
  const int stride = paddedMaskRowSize(size);
  for (int row = 0; row < size; ++row) {
    const int y = size - 1 - row;
    QVector<quint8> mask(stride, 0);
    for (int x = 0; x < size; ++x) {
      if (qAlpha(argb.pixel(x, y)) < 128) mask[x / 8] |= quint8(0x80 >> (x % 8));
    }
    for (const auto byte : mask) stream << byte;
  }
}

QByteArray trueColorEntry(const QImage &entry) {
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  if (!entry.save(&buffer, "PNG")) return {};
  buffer.close();
  return png;
}

QByteArray paletteEntry(const QImage &entry, int size) {
  const QImage indexed = quantize(entry);
  if (indexed.isNull() || indexed.format() != QImage::Format_Indexed8) return {};
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  writeHeader(stream, size);
  writePalette(stream, indexed);
  writeColourRows(stream, indexed, size);
  writeMaskRows(stream, entry, size);
  return data;
}

struct BuiltEntry {
  int size = 0;
  int bitCount = 32;
  QByteArray data;
};

QByteArray buildIcon(const QImage &source, const QList<int> &sizes, IconOptions::Mode mode,
                     QString *error) {
  QList<BuiltEntry> entries;
  for (const int size : sizes) {
    const QImage entry = scaledEntry(source, size);
    BuiltEntry built;
    built.size = size;
    if (mode == IconOptions::Mode::Palette8) {
      built.bitCount = 8;
      built.data = paletteEntry(entry, size);
    } else {
      built.bitCount = 32;
      built.data = trueColorEntry(entry);
    }
    if (built.data.isEmpty()) {
      if (error) *error = QStringLiteral("write-failed");
      return {};
    }
    entries.append(built);
  }

  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream << quint16(0) << quint16(1) << quint16(entries.size());
  quint32 offset = quint32(kDirectorySize + kEntrySize * entries.size());
  for (const auto &entry : entries) {
    const quint8 dimension = entry.size >= 256 ? quint8(0) : quint8(entry.size);
    stream << dimension << dimension << quint8(0) << quint8(0) << quint16(1)
           << quint16(entry.bitCount) << quint32(entry.data.size()) << offset;
    offset += quint32(entry.data.size());
  }
  for (const auto &entry : entries) bytes.append(entry.data);
  if (error) error->clear();
  return bytes;
}

// Rejects an empty selection and a size the contract does not offer.
QString validateSizes(const QList<int> &requested, QList<int> *sizes) {
  if (requested.isEmpty()) return QStringLiteral("no-sizes");
  QList<int> unique;
  for (const int size : requested) {
    if (!isOfferedSize(size)) return QStringLiteral("invalid-size");
    if (!unique.contains(size)) unique.append(size);
  }
  std::sort(unique.begin(), unique.end());
  *sizes = unique;
  return {};
}

bool isValidImage(const QImage &image) {
  return !image.isNull() && image.width() > 0 && image.height() > 0;
}

}  // namespace

QString writeIcon(const QImage &source, const IconOptions &options, QIODevice *out) {
  if (!out) return QStringLiteral("write-failed");
  QList<int> sizes;
  const QString sizeError = validateSizes(options.sizes, &sizes);
  if (!sizeError.isEmpty()) return sizeError;
  if (!isValidImage(source)) return QStringLiteral("invalid-image");

  QString error;
  const QByteArray bytes = buildIcon(source, sizes, options.mode, &error);
  if (bytes.isEmpty()) return error.isEmpty() ? QStringLiteral("write-failed") : error;
  if (out->write(bytes) != bytes.size()) return QStringLiteral("write-failed");
  return {};
}

QByteArray encodeIcon(const QImage &source, const IconOptions &options, QString *error) {
  QList<int> sizes;
  const QString sizeError = validateSizes(options.sizes, &sizes);
  if (!sizeError.isEmpty()) {
    if (error) *error = sizeError;
    return {};
  }
  if (!isValidImage(source)) {
    if (error) *error = QStringLiteral("invalid-image");
    return {};
  }
  QString buildError;
  const QByteArray bytes = buildIcon(source, sizes, options.mode, &buildError);
  if (bytes.isEmpty()) {
    if (error) *error = buildError.isEmpty() ? QStringLiteral("write-failed") : buildError;
    return {};
  }
  if (error) error->clear();
  return bytes;
}

QImage iconEntryImage(const QImage &source, int size, IconOptions::Mode mode) {
  if (!isValidImage(source) || size <= 0) return {};
  const QImage entry = scaledEntry(source, size);
  if (entry.isNull()) return {};
  if (mode != IconOptions::Mode::Palette8) return entry;

  const QImage indexed = quantize(entry);
  if (indexed.isNull()) return {};
  const QVector<QRgb> colors = indexed.colorTable();
  QImage visible(size, size, QImage::Format_ARGB32);
  for (int y = 0; y < size; ++y) {
    auto *line = reinterpret_cast<QRgb *>(visible.scanLine(y));
    for (int x = 0; x < size; ++x) {
      const int index = indexed.pixelIndex(x, y);
      const QRgb color = index >= 0 && index < colors.size() ? colors.at(index) : qRgb(0, 0, 0);
      const int alpha = qAlpha(entry.pixel(x, y)) < 128 ? 0 : 255;
      line[x] = (alpha << 24) | (color & 0x00ffffff);
    }
  }
  return visible;
}

QList<int> iconSizes() { return offeredSizes(); }

}  // namespace xerahs::app
