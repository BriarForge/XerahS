// EDITOR-UTILITIES-001 / EU-007: the .ico container is verified by parsing the
// bytes back with an independent reader, not by trusting the writer.
#include "IconWriter.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;

namespace {

struct Entry {
  int width = 0;
  int height = 0;
  int colorCount = 0;
  int reserved = 0;
  int planes = 0;
  int bitCount = 0;
  quint32 bytesInRes = 0;
  quint32 imageOffset = 0;
};

struct Icon {
  quint16 reserved = 0;
  quint16 type = 0;
  QList<Entry> entries;
  QByteArray bytes;
};

Icon parseIcon(const QByteArray &bytes) {
  Icon icon;
  icon.bytes = bytes;
  QDataStream stream(bytes);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream >> icon.reserved >> icon.type;
  quint16 count = 0;
  stream >> count;
  for (int i = 0; i < count; ++i) {
    quint8 width = 0, height = 0, colorCount = 0, reserved = 0;
    quint16 planes = 0, bitCount = 0;
    Entry entry;
    stream >> width >> height >> colorCount >> reserved >> planes >> bitCount >> entry.bytesInRes
           >> entry.imageOffset;
    entry.width = width;
    entry.height = height;
    entry.colorCount = colorCount;
    entry.reserved = reserved;
    entry.planes = planes;
    entry.bitCount = bitCount;
    icon.entries.append(entry);
  }
  return icon;
}

// A stored width of zero means 256 pixels.
int storedSize(const Entry &entry) { return entry.width == 0 ? 256 : entry.width; }

QImage decodePng(const QByteArray &bytes) {
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer, "png");
  return reader.read();
}

// A source with a transparent corner: the top left quadrant is clear.
QImage sourceImage() {
  QImage image(64, 64, QImage::Format_ARGB32);
  image.fill(QColor(0, 0, 255, 255));
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) image.setPixelColor(x, y, QColor(0, 0, 0, 0));
  return image;
}

int paddedRowSize(int pixels) { return ((pixels + 3) / 4) * 4; }

int paddedMaskRowSize(int pixels) { return ((pixels + 31) / 32) * 4; }

}  // namespace

class IconWriterTest : public QObject {
  Q_OBJECT

private slots:
  // The directory describes the entries and points at real data in the file.
  void headerAndEntriesAreWellFormed() {
    IconOptions options;
    options.sizes = {256, 16, 48};
    options.mode = IconOptions::Mode::TrueColor32;
    QString error;
    const QByteArray bytes = encodeIcon(sourceImage(), options, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Icon icon = parseIcon(bytes);
    QCOMPARE(icon.reserved, quint16(0));
    QCOMPARE(icon.type, quint16(1));
    QCOMPARE(icon.entries.size(), 3);

    // The entries are sorted by size, and 256 is stored as a zero byte.
    QCOMPARE(storedSize(icon.entries.at(0)), 16);
    QCOMPARE(storedSize(icon.entries.at(1)), 48);
    QCOMPARE(storedSize(icon.entries.at(2)), 256);
    QCOMPARE(icon.entries.at(2).width, 0);
    QCOMPARE(icon.entries.at(2).height, 0);
    for (const Entry &entry : icon.entries) {
      QCOMPARE(entry.height, entry.width);
      QCOMPARE(entry.colorCount, 0);
      QCOMPARE(entry.reserved, 0);
      QCOMPARE(entry.planes, 1);
      QCOMPARE(entry.bitCount, 32);
    }

    // Every offset is inside the file, and the data follows the directory.
    quint32 expectedOffset = 6 + 16 * 3;
    for (const Entry &entry : icon.entries) {
      QCOMPARE(entry.imageOffset, expectedOffset);
      QVERIFY(entry.imageOffset + entry.bytesInRes <= quint32(bytes.size()));
      expectedOffset += entry.bytesInRes;
    }
    QCOMPARE(expectedOffset, quint32(bytes.size()));

    // Each entry holds the PNG image it claims.
    for (const Entry &entry : icon.entries) {
      const QImage decoded = decodePng(bytes.mid(entry.imageOffset, entry.bytesInRes));
      QCOMPARE(decoded.width(), storedSize(entry));
      QCOMPARE(decoded.height(), storedSize(entry));
    }
  }

  // EU-007: a 32 bit entry keeps the alpha of the source.
  void trueColorEntriesKeepAlpha() {
    IconOptions options;
    options.sizes = {16, 32, 48};
    options.mode = IconOptions::Mode::TrueColor32;
    QString error;
    const QByteArray bytes = encodeIcon(sourceImage(), options, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Icon icon = parseIcon(bytes);
    QCOMPARE(icon.entries.size(), 3);
    for (const Entry &entry : icon.entries) {
      const int size = storedSize(entry);
      const QImage decoded = decodePng(bytes.mid(entry.imageOffset, entry.bytesInRes));
      QCOMPARE(decoded.width(), size);
      QCOMPARE(decoded.height(), size);
      QVERIFY(decoded.hasAlphaChannel());
      // The transparent corner stays transparent.
      QCOMPARE(decoded.pixelColor(0, 0).alpha(), 0);
      // The opaque corner keeps its colour.
      QCOMPARE(decoded.pixelColor(size - 1, size - 1).alpha(), 255);
      QCOMPARE(decoded.pixelColor(size - 1, size - 1).blue(), 255);
    }
  }

  // EU-007: an 8 bit entry is a headerless DIB with a palette and a mask.
  void paletteEntryHasTheExpectedHeaderAndMask() {
    IconOptions options;
    options.sizes = {32};
    options.mode = IconOptions::Mode::Palette8;
    QString error;
    const QByteArray bytes = encodeIcon(sourceImage(), options, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Icon icon = parseIcon(bytes);
    QCOMPARE(icon.entries.size(), 1);
    const Entry &entry = icon.entries.first();
    QCOMPARE(entry.bitCount, 8);
    QCOMPARE(entry.planes, 1);
    QCOMPARE(entry.colorCount, 0);
    const QByteArray data = bytes.mid(entry.imageOffset, entry.bytesInRes);

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 headerSize = 0;
    quint32 compression = 0;
    quint32 sizeImage = 0;
    qint32 width = 0;
    qint32 height = 0;
    quint16 planes = 0;
    quint16 bitCount = 0;
    stream >> headerSize >> width >> height >> planes >> bitCount >> compression >> sizeImage;
    QCOMPARE(headerSize, quint32(40));
    QCOMPARE(width, qint32(32));
    QCOMPARE(height, qint32(64));
    QCOMPARE(planes, quint16(1));
    QCOMPARE(bitCount, quint16(8));
    QCOMPARE(compression, quint32(0));
    QCOMPARE(sizeImage, quint32(0));

    const int xorRows = 32 * paddedRowSize(32);
    const int andRows = 32 * paddedMaskRowSize(32);
    QCOMPARE(data.size(), qsizetype(40 + 1024 + xorRows + andRows));

    // The mask is the tail of the entry: one bit per pixel, rows bottom-up.
    const QByteArray mask = data.right(andRows);
    const int maskStride = paddedMaskRowSize(32);
    // The top row of the image is the last mask row.
    const quint8 topByte = quint8(mask.at((32 - 1) * maskStride));
    QVERIFY2(topByte & 0x80, "the transparent corner is not marked in the AND mask");
    // An opaque pixel in the middle is not marked.
    const int middleRow = 32 - 1 - 16;
    const quint8 middleByte = quint8(mask.at(middleRow * maskStride + 16 / 8));
    QVERIFY2(!(middleByte & 0x80), "an opaque pixel is marked transparent");
  }

  // EU-007: without a size there is no container to write.
  void reportsNoSizes() {
    IconOptions options;
    QString error = QStringLiteral("unset");
    const QByteArray bytes = encodeIcon(sourceImage(), options, &error);
    QVERIFY(bytes.isEmpty());
    QCOMPARE(error, QStringLiteral("no-sizes"));

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QCOMPARE(writeIcon(sourceImage(), options, &buffer), QStringLiteral("no-sizes"));
    QCOMPARE(buffer.data().size(), qsizetype(0));
  }

  // EU-007: only the offered sizes may be written.
  void reportsAnInvalidSize() {
    IconOptions options;
    options.sizes = {20};
    QString error;
    const QByteArray bytes = encodeIcon(sourceImage(), options, &error);
    QVERIFY(bytes.isEmpty());
    QCOMPARE(error, QStringLiteral("invalid-size"));

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QCOMPARE(writeIcon(sourceImage(), options, &buffer), QStringLiteral("invalid-size"));
  }

  // EU-007: the image has to be a real one.
  void reportsAnInvalidImage() {
    IconOptions options;
    options.sizes = {16};
    QString error;
    const QByteArray bytes = encodeIcon(QImage(), options, &error);
    QVERIFY(bytes.isEmpty());
    QCOMPARE(error, QStringLiteral("invalid-image"));
  }

  // A destination that cannot be written reports a failure, not a short file.
  void reportsAWriteFailure() {
    IconOptions options;
    options.sizes = {16};
    QCOMPARE(writeIcon(sourceImage(), options, nullptr), QStringLiteral("write-failed"));
    QFile file(QStringLiteral("/nonexistent-xerahs-directory/icon.ico"));
    QCOMPARE(writeIcon(sourceImage(), options, &file), QStringLiteral("write-failed"));
  }

  // The device receives exactly the encoded container.
  void writeIconWritesTheEncodedBytes() {
    IconOptions options;
    options.sizes = {32, 16};
    options.mode = IconOptions::Mode::TrueColor32;
    QString error;
    const QByteArray encoded = encodeIcon(sourceImage(), options, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QCOMPARE(writeIcon(sourceImage(), options, &buffer), QString());
    QCOMPARE(buffer.data(), encoded);
  }

  // The container is readable as an .ico by the image plugins.
  void qtReadsThe32BitContainer() {
    IconOptions options;
    options.sizes = {16, 32, 48};
    options.mode = IconOptions::Mode::TrueColor32;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("icon.ico");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(writeIcon(sourceImage(), options, &file), QString());
    file.close();
    QVERIFY(QFileInfo(path).size() > 0);

    QImageReader reader(path, "ico");
    QCOMPARE(reader.imageCount(), 3);
    QVERIFY(!reader.read().isNull());
  }
};

QTEST_MAIN(IconWriterTest)
#include "IconWriterTest.moc"
