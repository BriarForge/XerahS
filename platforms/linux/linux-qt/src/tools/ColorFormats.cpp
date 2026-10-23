#include "tools/ColorFormats.h"

#include <QStringLiteral>

#include <algorithm>
#include <cmath>

namespace xerahs::tools {
namespace {

constexpr int kFormatCount = 8;

const char *const kFormatNames[kFormatCount] = {"HEX",    "HEX (alpha)", "RGB",     "RGBA",
                                                "HSB",    "HSL",         "CMYK",    "Decimal"};

// U+00B0, the unit shown after hue. Latin-1 covers it.
const QChar kDegree = QChar(0x00B0);
const QLatin1Char kPercent = QLatin1Char('%');

bool isHexDigit(QChar character) {
  const char16_t code = character.unicode();
  return (code >= u'0' && code <= u'9') || (code >= u'a' && code <= u'f') ||
         (code >= u'A' && code <= u'F');
}

// Qt reports a negative hue for an achromatic colour; the picker shows 0.
int hueDegrees(qreal hue) {
  if (hue < 0.0) return 0;
  return static_cast<int>(std::lround(hue * 360.0)) % 360;
}

int roundPercent(qreal fraction) {
  return static_cast<int>(std::lround(fraction * 100.0));
}

QString degrees(int value) { return QString::number(value) + kDegree; }

QString percent(int value) { return QString::number(value) + kPercent; }

// Key is the black component; the others divide by what is left of each channel.
QString cmyk(const QColor &color) {
  const qreal key = 1.0 - qMax(color.redF(), qMax(color.greenF(), color.blueF()));
  const qreal span = 1.0 - key;
  // Black leaves no span to divide; report no ink and a full key instead.
  const qreal cyan = span > 0.0 ? (1.0 - color.redF() - key) / span : 0.0;
  const qreal magenta = span > 0.0 ? (1.0 - color.greenF() - key) / span : 0.0;
  const qreal yellow = span > 0.0 ? (1.0 - color.blueF() - key) / span : 0.0;
  return QStringLiteral("%1, %2, %3, %4")
      .arg(percent(roundPercent(cyan)), percent(roundPercent(magenta)), percent(roundPercent(yellow)),
           percent(roundPercent(key)));
}

}  // namespace

QString formatColor(const QColor &color, ColorFormat format) {
  if (!color.isValid()) return QString();
  switch (format) {
    case ColorFormat::Hex:
      return color.name(QColor::HexRgb).toUpper();
    case ColorFormat::HexAlpha:
      // name() drops alpha, so RRGGBBAA is built from the RGB name and the
      // alpha byte rather than from Qt's ARGB ordering.
      return color.name(QColor::HexRgb).toUpper() +
             QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0')).toUpper();
    case ColorFormat::Rgb:
      return QStringLiteral("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
    case ColorFormat::Rgba:
      return QStringLiteral("%1, %2, %3, %4")
          .arg(color.red())
          .arg(color.green())
          .arg(color.blue())
          .arg(QString::number(color.alphaF(), 'f', 2));
    case ColorFormat::Hsb:
      return QStringLiteral("%1, %2, %3")
          .arg(degrees(hueDegrees(color.hsvHueF())), percent(roundPercent(color.hsvSaturationF())),
               percent(roundPercent(color.valueF())));
    case ColorFormat::Hsl:
      return QStringLiteral("%1, %2, %3")
          .arg(degrees(hueDegrees(color.hslHueF())), percent(roundPercent(color.hslSaturationF())),
               percent(roundPercent(color.lightnessF())));
    case ColorFormat::Cmyk:
      return cmyk(color);
    case ColorFormat::Decimal:
      return QString::number((color.red() << 16) | (color.green() << 8) | color.blue());
  }
  return QString();
}

QStringList formatAll(const QColor &color) {
  QStringList lines;
  lines.reserve(kFormatCount);
  for (int index = 0; index < kFormatCount; ++index) {
    const auto format = static_cast<ColorFormat>(index);
    lines.append(QStringLiteral("%1: %2").arg(formatName(format), formatColor(color, format)));
  }
  return lines;
}

QString formatName(ColorFormat format) {
  const int index = static_cast<int>(format);
  if (index < 0 || index >= kFormatCount) return QString();
  return QString::fromLatin1(kFormatNames[index]);
}

std::optional<QColor> parseHexColor(const QString &text) {
  QString digits = text.trimmed();
  if (digits.startsWith(QLatin1Char('#'))) digits = digits.mid(1);
  const int size = digits.size();
  if (size != 3 && size != 6 && size != 8) return std::nullopt;
  for (const QChar character : digits) {
    if (!isHexDigit(character)) return std::nullopt;
  }
  if (size == 8) {
    return QColor(digits.mid(0, 2).toInt(nullptr, 16), digits.mid(2, 2).toInt(nullptr, 16),
                  digits.mid(4, 2).toInt(nullptr, 16), digits.mid(6, 2).toInt(nullptr, 16));
  }
  if (size == 3) {
    // #RGB widens each digit, so #abc becomes #aabbcc.
    QString expanded;
    expanded.reserve(6);
    for (const QChar character : digits) expanded.append(character).append(character);
    digits = expanded;
  }
  return QColor(QLatin1Char('#') + digits);
}

}  // namespace xerahs::tools