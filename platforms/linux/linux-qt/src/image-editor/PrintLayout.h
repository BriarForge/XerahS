#pragma once
#include <QRectF>
#include <optional>
namespace xerahs::editor {
// One image page, centred within the printer's printable area. 100% fits;
// smaller percentages leave more paper around it and never crop the snapshot.
std::optional<QRectF> printImageRect(int width, int height, const QRectF &printable, double percentOfFit);
// 8-bit non-premultiplied sRGB ARGB on explicitly disclosed white paper.
quint32 printPixelOnWhite(quint32 argb);
}
