// EDITOR-UTILITIES-001 / EU-007: multi-size Windows .ico container writer.
// Qt's ico plugin writes a single image, so the container is written here.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QIODevice>
#include <QList>
#include <QString>

namespace xerahs::app {

struct IconOptions {
  QList<int> sizes;
  enum class Mode { TrueColor32, Palette8 } mode = Mode::TrueColor32;
};

// Writes the container to out. The return value is an empty string on success
// and otherwise one of "no-sizes", "invalid-size", "invalid-image" or
// "write-failed". The sizes are validated first, then the image.
QString writeIcon(const QImage &source, const IconOptions &options, QIODevice *out);

// Encodes the container in memory. On failure the same code is stored in error
// when one is given, and an empty array is returned.
QByteArray encodeIcon(const QImage &source, const IconOptions &options, QString *error);

// The entry as it is written, so the dialog can preview the same pixels.
QImage iconEntryImage(const QImage &source, int size, IconOptions::Mode mode);

// The sizes the contract offers, in ascending order.
QList<int> iconSizes();

}  // namespace xerahs::app
