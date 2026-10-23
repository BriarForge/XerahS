// INTERFACE-CATALOG-001: the colour picker tool (EDITOR-UTILITIES-001
// screen-color-picker) presents one picked colour in these text formats.
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <optional>

namespace xerahs::tools {

enum class ColorFormat { Hex, HexAlpha, Rgb, Rgba, Hsb, Hsl, Cmyk, Decimal };

// One format of a picked colour. An invalid QColor formats as an empty string.
QString formatColor(const QColor &color, ColorFormat format);

// formatName(format) + ": " + formatColor(color, format), one line per format
// in enum order, so a caller never has to know the order itself.
QStringList formatAll(const QColor &color);

QString formatName(ColorFormat format);

// An optional '#' followed by exactly 3, 6 or 8 hexadecimal digits, with
// surrounding spaces trimmed and case ignored: "#RGB", "RRGGBB", "#RRGGBB",
// "#RRGGBBAA". The 8-digit form is RRGGBBAA to round-trip formatColor(HexAlpha);
// Qt's own #AARRGGBB form would put alpha in the wrong pair. Anything else,
// including an empty string, a bare '#' and any non-hex character, is rejected.
std::optional<QColor> parseHexColor(const QString &text);

}  // namespace xerahs::tools