// EDITOR-UTILITIES-001 / EU-003: the QR encoder against hand-checked vectors,
// the standard format/version tables, capacity boundaries, determinism, and (when
// zbarimg is installed) a real decoder as the scannability oracle.
#include "QrRender.h"
#include "tools/QrEncoder.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::tools;
using xerahs::app::renderQr;

namespace {

std::uint32_t fromBits(const char *s) {
  std::uint32_t v = 0;
  for (; *s; ++s) v = v << 1 | static_cast<std::uint32_t>(*s - '0');
  return v;
}

QByteArray filler(int n, unsigned seed) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?#[]@!$&'()*+,;=%";
  QByteArray out;
  for (int i = 0; i < n; ++i) { seed = seed * 1103515245u + 12345u; out.append(alphabet[(seed >> 16) % (sizeof alphabet - 1)]); }
  return out;
}

}  // namespace

class QrEncoderTest : public QObject {
  Q_OBJECT
private slots:
  void reedSolomonDivisorMatchesHandChecked() {
    QCOMPARE(qrReedSolomonDivisor(2), (std::vector<std::uint8_t>{3, 2}));  // (x+1)(x+2) over GF(256)
    QCOMPARE(qrReedSolomonDivisor(7), (std::vector<std::uint8_t>{0x7F, 0x7A, 0x9A, 0xA4, 0x0B, 0x44, 0x75}));  // x^7+127x^6+...+117
  }
  void reedSolomonRemainderKnownVector() {
    // "HELLO WORLD" version 1-M data codewords and their ten ECC codewords.
    const std::vector<std::uint8_t> data{32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17};
    const std::vector<std::uint8_t> ecc{196, 35, 39, 119, 235, 215, 231, 226, 93, 23};
    QCOMPARE(qrReedSolomonRemainder(data, qrReedSolomonDivisor(10)), ecc);
  }
  void formatBitsMatchStandardTable() {
    static const char *table[4][8] = {
        {"111011111000100", "111001011110011", "111110110101010", "111100010011101", "110011000101111", "110001100011000", "110110001000001", "110100101110110"},
        {"101010000010010", "101000100100101", "101111001111100", "101101101001011", "100010111111001", "100000011001110", "100111110010111", "100101010100000"},
        {"011010101011111", "011000001101000", "011111100110001", "011101000000110", "010010010110100", "010000110000011", "010111011011010", "010101111101101"},
        {"001011010001001", "001001110111110", "001110011100111", "001100111010000", "000011101100010", "000001001010101", "000110100001100", "000100000111011"}};
    for (int e = 0; e < 4; ++e)
      for (int m = 0; m < 8; ++m) QCOMPARE(qrFormatBits(static_cast<QrEcc>(e), m), fromBits(table[e][m]));
  }
  void versionBitsMatchStandardTable() {
    static const std::uint32_t table[] = {0x07C94, 0x085BC, 0x09A99, 0x0A4D3, 0x0BBF6, 0x0C762, 0x0D847, 0x0E60D, 0x0F928, 0x10B78, 0x1145D, 0x12A17, 0x13532, 0x149A6,
                                          0x15683, 0x168C9, 0x177EC, 0x18EC4, 0x191E1, 0x1AFAB, 0x1B08E, 0x1CC1A, 0x1D33F, 0x1ED75, 0x1F250, 0x209D5, 0x216F0, 0x228BA,
                                          0x2379F, 0x24B0B, 0x2542E, 0x26A64, 0x27541, 0x28C69};
    for (int v = 7; v <= 40; ++v) QCOMPARE(qrVersionBits(v), table[v - 7]);
  }
  void capacityTable() {
    const int known[][5] = {{1, 17, 14, 11, 7}, {7, 154, 122, 86, 64}, {10, 271, 213, 151, 119}, {40, 2953, 2331, 1663, 1273}};
    for (const auto &row : known)
      for (int e = 0; e < 4; ++e) QCOMPARE(qrByteCapacity(row[0], static_cast<QrEcc>(e)), row[e + 1]);
    QCOMPARE(qrByteCapacity(0, QrEcc::L), 0);
    QCOMPARE(qrByteCapacity(41, QrEcc::L), 0);
  }
  void capacityBoundariesBumpTheVersion() {
    for (int e = 0; e < 4; ++e) {
      const auto ecc = static_cast<QrEcc>(e);
      for (int v : {1, 2, 9, 10, 26, 39}) {
        const int cap = qrByteCapacity(v, ecc);
        const auto fits = encodeQr(QByteArray(cap, 'a'), ecc);
        QVERIFY(fits.ok);
        QCOMPARE(fits.matrix.version, v);
        const auto bumped = encodeQr(QByteArray(cap + 1, 'a'), ecc);
        QVERIFY(bumped.ok);
        QCOMPARE(bumped.matrix.version, v + 1);
        QCOMPARE(bumped.matrix.size, (v + 1) * 4 + 17);
      }
      const int top = qrByteCapacity(40, ecc);
      QVERIFY(encodeQr(QByteArray(top, 'a'), ecc).ok);
      const auto tooLong = encodeQr(QByteArray(top + 1, 'a'), ecc);
      QVERIFY(!tooLong.ok);
      QCOMPARE(tooLong.error, QStringLiteral("qr-content-too-long"));
    }
  }
  void typedErrors() {
    QCOMPARE(encodeQr(QByteArray(), QrEcc::M).error, QStringLiteral("qr-empty-content"));
    QCOMPARE(encodeQr(QString(), QrEcc::M).error, QStringLiteral("qr-empty-content"));
    QCOMPARE(encodeQr(QByteArray("a"), QrEcc::M, 0).error, QStringLiteral("qr-invalid-version"));
    QCOMPARE(encodeQr(QByteArray("a"), QrEcc::M, 41).error, QStringLiteral("qr-invalid-version"));
    QCOMPARE(encodeQr(QByteArray(100, 'a'), QrEcc::H, 40).matrix.version, 40);  // requested minimum
  }
  void deterministic() {
    for (int e = 0; e < 4; ++e) {
      const auto a = encodeQr(QStringLiteral("https://example.com/éあ"), static_cast<QrEcc>(e));
      const auto b = encodeQr(QStringLiteral("https://example.com/éあ"), static_cast<QrEcc>(e));
      QVERIFY(a.ok);
      QCOMPARE(a.matrix.modules, b.matrix.modules);
      QCOMPARE(a.matrix.mask, b.matrix.mask);
    }
  }
  void structureIsSound() {
    const auto r = encodeQr(QByteArray("HELLO WORLD"), QrEcc::M);
    QVERIFY(r.ok);
    QCOMPARE(r.matrix.version, 1);
    const auto &m = r.matrix;
    QVERIFY(m.dark(8, m.size - 8));                 // dark module
    QVERIFY(m.dark(0, 0) && m.dark(6, 6) && !m.dark(1, 1) && m.dark(2, 2));  // finder
    QCOMPARE(m.dark(8, 6), true);
    QCOMPARE(m.dark(10, 6), true);
  }
  void renderValidatesSize() {
    const auto m = encodeQr(QByteArray("a"), QrEcc::M).matrix;  // 21 modules + 8 quiet
    QCOMPARE(renderQr(m, 28).error, QStringLiteral("qr-invalid-size"));
    QCOMPARE(renderQr(m, 4097).error, QStringLiteral("qr-invalid-size"));
    const auto ok = renderQr(m, 29);
    QVERIFY(ok.ok);
    QCOMPARE(ok.image.width(), 29);
    QCOMPARE(renderQr(m, 100).image.width(), 87);  // 3 px per module
    QCOMPARE(renderQr(m, 4096).image.width(), 29 * 141);
    const auto img = renderQr(m, 58).image;  // quiet zone is white, finder corner is black
    QCOMPARE(img.pixelColor(0, 0), QColor(Qt::white));
    QCOMPARE(img.pixelColor(2 * 4, 2 * 4), QColor(Qt::black));
  }

  void zbarOracle_data() {
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("ecc");
    QTest::addColumn<int>("expectVersion");  // 0 = any
    for (int e = 0; e < 4; ++e) {
      const QByteArray n = qrEccName(static_cast<QrEcc>(e)).toLatin1();
      QTest::newRow(("ascii-" + n).constData()) << QByteArray("Hello, XerahS! https://example.com/a?b=c") << e << 0;
      QTest::newRow(("one-byte-" + n).constData()) << QByteArray("A") << e << 1;
      QTest::newRow(("utf8-" + n).constData()) << QStringLiteral("café 日本語 русский \U0001F600").toUtf8() << e << 0;
      for (int v : {1, 7, 10, 25, 40})
        QTest::newRow(("v" + std::to_string(v) + "-" + n.toStdString()).c_str()) << filler(qrByteCapacity(v, static_cast<QrEcc>(e)), 7u * v + e) << e << v;
    }
  }
  void zbarOracle() {
    const auto zbar = QStandardPaths::findExecutable(QStringLiteral("zbarimg"));
    if (zbar.isEmpty()) QSKIP("zbarimg is not installed");
    QFETCH(QByteArray, content);
    QFETCH(int, ecc);
    QFETCH(int, expectVersion);
    const auto r = encodeQr(content, static_cast<QrEcc>(ecc));
    QVERIFY(r.ok);
    if (expectVersion) QCOMPARE(r.matrix.version, expectVersion);
    const auto img = renderQr(r.matrix, xerahs::app::qrMinimumPixels(r.matrix) * 4);
    QVERIFY(img.ok);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto png = dir.filePath(QStringLiteral("qr.png"));
    QVERIFY(img.image.save(png, "PNG"));
    QProcess p;
    p.start(zbar, {QStringLiteral("--raw"), QStringLiteral("-q"), png});
    QVERIFY(p.waitForFinished(30000));
    QCOMPARE(p.exitCode(), 0);
    auto out = p.readAllStandardOutput();
    if (out.endsWith('\n')) out.chop(1);
    QCOMPARE(out, content);
  }
};

QTEST_MAIN(QrEncoderTest)
#include "QrEncoderTest.moc"
