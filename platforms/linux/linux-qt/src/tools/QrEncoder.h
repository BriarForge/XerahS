// EDITOR-UTILITIES-001 / EU-003: ISO/IEC 18004 QR Code Model 2 encoder (byte
// mode, UTF-8, versions 1-40, ECC L/M/Q/H). Deterministic: the same input always
// yields the same matrix; mask ties go to the lowest mask number.
#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace xerahs::tools {

enum class QrEcc { L, M, Q, H };

struct QrMatrix {
  int version = 0, mask = 0, size = 0;
  std::vector<std::uint8_t> modules;  // row-major, 1 = dark
  bool dark(int x, int y) const { return modules[static_cast<std::size_t>(y) * size + x] != 0; }
};

// error is empty on success, or "qr-empty-content" / "qr-content-too-long" /
// "qr-invalid-version".
struct QrResult {
  bool ok = false;
  QrMatrix matrix;
  QString error;
};

// minVersion (1-40) lets a caller force a larger symbol; the smallest version
// that fits at least that large is chosen.
QrResult encodeQr(const QByteArray &bytes, QrEcc ecc, int minVersion = 1);
QrResult encodeQr(const QString &text, QrEcc ecc, int minVersion = 1);  // UTF-8

// Largest byte-mode payload one version holds at one level; 0 for a bad version.
int qrByteCapacity(int version, QrEcc ecc);

// Building blocks, exposed for conformance tests.
std::vector<std::uint8_t> qrReedSolomonDivisor(int degree);
std::vector<std::uint8_t> qrReedSolomonRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &divisor);
std::uint32_t qrFormatBits(QrEcc ecc, int mask);  // 15 bits, after the 0x5412 mask
std::uint32_t qrVersionBits(int version);         // 18 bits, versions 7-40

QString qrEccName(QrEcc ecc);  // "L" "M" "Q" "H"

}  // namespace xerahs::tools
