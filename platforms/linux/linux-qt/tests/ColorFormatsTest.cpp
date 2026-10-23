#include "tools/ColorFormats.h"

#include <QTest>

using namespace xerahs::tools;

namespace {

const QColor kBlack(0, 0, 0);
const QColor kWhite(255, 255, 255);
const QColor kRed(255, 0, 0);
const QColor kOrange(255, 128, 0);
const QColor kHalfAlpha(255, 128, 0, 128);
const QColor kGrey(128, 128, 128);
// Default constructed, so isValid() is false.
const QColor kInvalid;

}  // namespace

class ColorFormatsTest : public QObject {
  Q_OBJECT

private slots:
  void hex_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("#000000");
    QTest::newRow("white") << kWhite << QStringLiteral("#FFFFFF");
    QTest::newRow("red") << kRed << QStringLiteral("#FF0000");
    QTest::newRow("orange") << kOrange << QStringLiteral("#FF8000");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("#FF8000");
    QTest::newRow("grey") << kGrey << QStringLiteral("#808080");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void hex() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Hex), expected);
  }

  void hexAlpha_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("#000000FF");
    QTest::newRow("white") << kWhite << QStringLiteral("#FFFFFFFF");
    QTest::newRow("red") << kRed << QStringLiteral("#FF0000FF");
    QTest::newRow("orange") << kOrange << QStringLiteral("#FF8000FF");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("#FF800080");
    QTest::newRow("grey") << kGrey << QStringLiteral("#808080FF");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void hexAlpha() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::HexAlpha), expected);
  }

  void rgb_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("0, 0, 0");
    QTest::newRow("white") << kWhite << QStringLiteral("255, 255, 255");
    QTest::newRow("red") << kRed << QStringLiteral("255, 0, 0");
    QTest::newRow("orange") << kOrange << QStringLiteral("255, 128, 0");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("255, 128, 0");
    QTest::newRow("grey") << kGrey << QStringLiteral("128, 128, 128");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void rgb() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Rgb), expected);
  }

  void rgba_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("0, 0, 0, 1.00");
    QTest::newRow("white") << kWhite << QStringLiteral("255, 255, 255, 1.00");
    QTest::newRow("red") << kRed << QStringLiteral("255, 0, 0, 1.00");
    QTest::newRow("orange") << kOrange << QStringLiteral("255, 128, 0, 1.00");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("255, 128, 0, 0.50");
    QTest::newRow("grey") << kGrey << QStringLiteral("128, 128, 128, 1.00");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void rgba() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Rgba), expected);
  }

  void hsb_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("0\u00b0, 0%, 0%");
    QTest::newRow("white") << kWhite << QStringLiteral("0\u00b0, 0%, 100%");
    QTest::newRow("red") << kRed << QStringLiteral("0\u00b0, 100%, 100%");
    QTest::newRow("orange") << kOrange << QStringLiteral("30\u00b0, 100%, 100%");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("30\u00b0, 100%, 100%");
    QTest::newRow("grey") << kGrey << QStringLiteral("0\u00b0, 0%, 50%");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void hsb() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Hsb), expected);
  }

  void hsl_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("0\u00b0, 0%, 0%");
    QTest::newRow("white") << kWhite << QStringLiteral("0\u00b0, 0%, 100%");
    QTest::newRow("red") << kRed << QStringLiteral("0\u00b0, 100%, 50%");
    QTest::newRow("orange") << kOrange << QStringLiteral("30\u00b0, 100%, 50%");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("30\u00b0, 100%, 50%");
    QTest::newRow("grey") << kGrey << QStringLiteral("0\u00b0, 0%, 50%");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void hsl() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Hsl), expected);
  }

  void cmyk_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("0%, 0%, 0%, 100%");
    QTest::newRow("white") << kWhite << QStringLiteral("0%, 0%, 0%, 0%");
    QTest::newRow("red") << kRed << QStringLiteral("0%, 100%, 100%, 0%");
    QTest::newRow("orange") << kOrange << QStringLiteral("0%, 50%, 100%, 0%");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("0%, 50%, 100%, 0%");
    QTest::newRow("grey") << kGrey << QStringLiteral("0%, 0%, 0%, 50%");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void cmyk() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Cmyk), expected);
  }

  void decimal_data() {
    QTest::addColumn<QColor>("color");
    QTest::addColumn<QString>("expected");
    QTest::newRow("black") << kBlack << QStringLiteral("0");
    QTest::newRow("white") << kWhite << QStringLiteral("16777215");
    QTest::newRow("red") << kRed << QStringLiteral("16711680");
    QTest::newRow("orange") << kOrange << QStringLiteral("16744448");
    QTest::newRow("half-alpha") << kHalfAlpha << QStringLiteral("16744448");
    QTest::newRow("grey") << kGrey << QStringLiteral("8421504");
    QTest::newRow("invalid") << kInvalid << QString();
  }
  void decimal() {
    QFETCH(QColor, color);
    QFETCH(QString, expected);
    QCOMPARE(formatColor(color, ColorFormat::Decimal), expected);
  }

  void formatAllListsOneNamedLinePerFormat() {
    const QStringList lines = formatAll(kOrange);
    QCOMPARE(lines.size(), 8);
    QCOMPARE(lines.at(0), QStringLiteral("HEX: #FF8000"));
    QCOMPARE(lines.at(1), QStringLiteral("HEX (alpha): #FF8000FF"));
    QCOMPARE(lines.at(2), QStringLiteral("RGB: 255, 128, 0"));
    QCOMPARE(lines.at(3), QStringLiteral("RGBA: 255, 128, 0, 1.00"));
    QCOMPARE(lines.at(4), QStringLiteral("HSB: 30\u00b0, 100%, 100%"));
    QCOMPARE(lines.at(5), QStringLiteral("HSL: 30\u00b0, 100%, 50%"));
    QCOMPARE(lines.at(6), QStringLiteral("CMYK: 0%, 50%, 100%, 0%"));
    QCOMPARE(lines.at(7), QStringLiteral("Decimal: 16744448"));
  }

  void formatAllKeepsOneLinePerFormatForAnInvalidColor() {
    const QStringList lines = formatAll(kInvalid);
    QCOMPARE(lines.size(), 8);
    QCOMPARE(lines.at(0), QStringLiteral("HEX: "));
    QCOMPARE(lines.at(7), QStringLiteral("Decimal: "));
  }

  void formatNameMatchesEnumOrder() {
    QCOMPARE(formatName(ColorFormat::Hex), QStringLiteral("HEX"));
    QCOMPARE(formatName(ColorFormat::HexAlpha), QStringLiteral("HEX (alpha)"));
    QCOMPARE(formatName(ColorFormat::Rgb), QStringLiteral("RGB"));
    QCOMPARE(formatName(ColorFormat::Rgba), QStringLiteral("RGBA"));
    QCOMPARE(formatName(ColorFormat::Hsb), QStringLiteral("HSB"));
    QCOMPARE(formatName(ColorFormat::Hsl), QStringLiteral("HSL"));
    QCOMPARE(formatName(ColorFormat::Cmyk), QStringLiteral("CMYK"));
    QCOMPARE(formatName(ColorFormat::Decimal), QStringLiteral("Decimal"));
  }

  void parseHexColorAcceptsValidForms_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("red");
    QTest::addColumn<int>("green");
    QTest::addColumn<int>("blue");
    QTest::addColumn<int>("alpha");
    QTest::newRow("#RRGGBB") << QStringLiteral("#FF8000") << 255 << 128 << 0 << 255;
    QTest::newRow("RRGGBB") << QStringLiteral("FF8000") << 255 << 128 << 0 << 255;
    QTest::newRow("#RGB") << QStringLiteral("#ABC") << 170 << 187 << 204 << 255;
    QTest::newRow("#RRGGBBAA") << QStringLiteral("#FF800080") << 255 << 128 << 0 << 128;
    QTest::newRow("lowercase") << QStringLiteral("#ff8000") << 255 << 128 << 0 << 255;
    QTest::newRow("uppercase") << QStringLiteral("#FF8000") << 255 << 128 << 0 << 255;
    QTest::newRow("surrounding spaces") << QStringLiteral("  #FF8000 ") << 255 << 128 << 0 << 255;
    QTest::newRow("black") << QStringLiteral("#000000") << 0 << 0 << 0 << 255;
    QTest::newRow("white") << QStringLiteral("#FFFFFF") << 255 << 255 << 255 << 255;
  }
  void parseHexColorAcceptsValidForms() {
    QFETCH(QString, text);
    QFETCH(int, red);
    QFETCH(int, green);
    QFETCH(int, blue);
    QFETCH(int, alpha);
    const auto parsed = parseHexColor(text);
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->isValid());
    QCOMPARE(parsed->red(), red);
    QCOMPARE(parsed->green(), green);
    QCOMPARE(parsed->blue(), blue);
    QCOMPARE(parsed->alpha(), alpha);
  }

  void parseHexColorRejectsUnusableInput_data() {
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << QString();
    QTest::newRow("only spaces") << QStringLiteral("   ");
    QTest::newRow("bare hash") << QStringLiteral("#");
    QTest::newRow("one digit") << QStringLiteral("#F");
    QTest::newRow("four digits") << QStringLiteral("#F80F");
    QTest::newRow("seven digits") << QStringLiteral("#FF80000");
    QTest::newRow("nine digits") << QStringLiteral("#FF8000000");
    QTest::newRow("non-hex letters") << QStringLiteral("#GG8000");
    QTest::newRow("inner separator") << QStringLiteral("#FF 8000");
    QTest::newRow("rgb() text") << QStringLiteral("rgb(255, 128, 0)");
    QTest::newRow("named colour") << QStringLiteral("orange");
  }
  void parseHexColorRejectsUnusableInput() {
    QFETCH(QString, text);
    QVERIFY(!parseHexColor(text).has_value());
  }

  void parseHexColorRoundTripsHexAlphaOutput() {
    const auto parsed = parseHexColor(formatColor(kHalfAlpha, ColorFormat::HexAlpha));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->rgb(), kHalfAlpha.rgb());
    QCOMPARE(parsed->alpha(), kHalfAlpha.alpha());
  }
};

QTEST_GUILESS_MAIN(ColorFormatsTest)
#include "ColorFormatsTest.moc"