#include "QrEncoder.h"

#include <algorithm>
#include <cstdlib>

namespace xerahs::tools {
namespace {

// Index by QrEcc (L M Q H) then version 1..40 (slot 0 unused).
const int kEccPerBlock[4][41] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}};
const int kBlocks[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81}};

int rawModules(int ver) {
  int result = (16 * ver + 128) * ver + 64;
  if (ver >= 2) {
    const int align = ver / 7 + 2;
    result -= (25 * align - 10) * align - 55;
    if (ver >= 7) result -= 36;
  }
  return result;
}

int dataCodewords(int ver, QrEcc ecc) {
  const int e = static_cast<int>(ecc);
  return rawModules(ver) / 8 - kEccPerBlock[e][ver] * kBlocks[e][ver];
}

int gfMul(int x, int y) {
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return z;
}

bool bit(std::uint32_t x, int i) { return ((x >> i) & 1) != 0; }

class Builder {
public:
  Builder(int ver, QrEcc ecc) : m_ver(ver), m_ecc(ecc), m_size(ver * 4 + 17),
    m_mod(static_cast<std::size_t>(m_size) * m_size, 0), m_fn(m_mod.size(), 0) {}

  QrMatrix build(const std::vector<std::uint8_t> &data) {
    drawFunctionPatterns();
    placeCodewords(interleave(data));
    int best = 0;
    long bestPenalty = -1;
    for (int m = 0; m < 8; ++m) {
      applyMask(m);
      drawFormat(m);
      const long p = penalty();
      if (bestPenalty < 0 || p < bestPenalty) { best = m; bestPenalty = p; }
      applyMask(m);  // undo
    }
    applyMask(best);
    drawFormat(best);
    return {m_ver, best, m_size, m_mod};
  }

private:
  std::uint8_t &at(int x, int y) { return m_mod[static_cast<std::size_t>(y) * m_size + x]; }
  bool get(int x, int y) const { return m_mod[static_cast<std::size_t>(y) * m_size + x] != 0; }
  void setFn(int x, int y, bool dark) {
    at(x, y) = dark; m_fn[static_cast<std::size_t>(y) * m_size + x] = 1;
  }
  bool isFn(int x, int y) const { return m_fn[static_cast<std::size_t>(y) * m_size + x] != 0; }

  std::vector<int> alignPositions() const {
    if (m_ver == 1) return {};
    const int n = m_ver / 7 + 2;
    const int step = m_ver == 32 ? 26 : (m_ver * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
    std::vector<int> r;
    for (int i = 0, pos = m_size - 7; i < n - 1; ++i, pos -= step) r.insert(r.begin(), pos);
    r.insert(r.begin(), 6);
    return r;
  }

  void drawFinder(int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const int dist = std::max(std::abs(dx), std::abs(dy)), x = cx + dx, y = cy + dy;
        if (x >= 0 && x < m_size && y >= 0 && y < m_size) setFn(x, y, dist != 2 && dist != 4);
      }
  }

  void drawAlign(int cx, int cy) {
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) setFn(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
  }

  void drawFunctionPatterns() {
    for (int i = 0; i < m_size; ++i) { setFn(6, i, i % 2 == 0); setFn(i, 6, i % 2 == 0); }
    drawFinder(3, 3); drawFinder(m_size - 4, 3); drawFinder(3, m_size - 4);
    const auto pos = alignPositions();
    const int n = static_cast<int>(pos.size());
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j)
        if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) drawAlign(pos[i], pos[j]);
    drawFormat(0);  // reserve the format areas
    drawVersion();
  }

  void drawFormat(int mask) {
    const auto bits = qrFormatBits(m_ecc, mask);
    for (int i = 0; i <= 5; ++i) setFn(8, i, bit(bits, i));
    setFn(8, 7, bit(bits, 6)); setFn(8, 8, bit(bits, 7)); setFn(7, 8, bit(bits, 8));
    for (int i = 9; i < 15; ++i) setFn(14 - i, 8, bit(bits, i));
    for (int i = 0; i < 8; ++i) setFn(m_size - 1 - i, 8, bit(bits, i));
    for (int i = 8; i < 15; ++i) setFn(8, m_size - 15 + i, bit(bits, i));
    setFn(8, m_size - 8, true);  // the always-dark module
  }

  void drawVersion() {
    if (m_ver < 7) return;
    const auto bits = qrVersionBits(m_ver);
    for (int i = 0; i < 18; ++i) {
      const int a = m_size - 11 + i % 3, b = i / 3;
      setFn(a, b, bit(bits, i)); setFn(b, a, bit(bits, i));
    }
  }

  std::vector<std::uint8_t> interleave(const std::vector<std::uint8_t> &data) const {
    const int e = static_cast<int>(m_ecc), blocks = kBlocks[e][m_ver], eccLen = kEccPerBlock[e][m_ver];
    const int raw = rawModules(m_ver) / 8, shortCount = blocks - raw % blocks, shortLen = raw / blocks;
    std::vector<std::vector<std::uint8_t>> parts;
    const auto divisor = qrReedSolomonDivisor(eccLen);
    for (int i = 0, k = 0; i < blocks; ++i) {
      const int len = shortLen - eccLen + (i < shortCount ? 0 : 1);
      std::vector<std::uint8_t> block(data.begin() + k, data.begin() + k + len);
      k += len;
      const auto ec = qrReedSolomonRemainder(block, divisor);
      if (i < shortCount) block.push_back(0);  // placeholder keeps blocks equal length
      block.insert(block.end(), ec.begin(), ec.end());
      parts.push_back(std::move(block));
    }
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < parts[0].size(); ++i)
      for (int j = 0; j < blocks; ++j)
        if (static_cast<int>(i) != shortLen - eccLen || j >= shortCount) out.push_back(parts[j][i]);
    return out;
  }

  void placeCodewords(const std::vector<std::uint8_t> &data) {
    std::size_t i = 0;
    for (int right = m_size - 1; right >= 1; right -= 2) {
      if (right == 6) right = 5;
      for (int vert = 0; vert < m_size; ++vert)
        for (int j = 0; j < 2; ++j) {
          const int x = right - j;
          const bool upward = ((right + 1) & 2) == 0;
          const int y = upward ? m_size - 1 - vert : vert;
          if (!isFn(x, y) && i < data.size() * 8) { at(x, y) = bit(data[i >> 3], 7 - static_cast<int>(i & 7)); ++i; }
        }
    }
  }

  void applyMask(int m) {
    for (int y = 0; y < m_size; ++y)
      for (int x = 0; x < m_size; ++x) {
        bool invert = false;
        switch (m) {
          case 0: invert = (x + y) % 2 == 0; break;
          case 1: invert = y % 2 == 0; break;
          case 2: invert = x % 3 == 0; break;
          case 3: invert = (x + y) % 3 == 0; break;
          case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
          case 5: invert = x * y % 2 + x * y % 3 == 0; break;
          case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
          default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
        }
        if (invert && !isFn(x, y)) at(x, y) ^= 1;
      }
  }

  // ISO 18004 8.8.2: the four penalty rules.
  long penalty() const {
    long p = 0;
    const int n = m_size;
    for (int pass = 0; pass < 2; ++pass) {
      auto cell = [&](int a, int b) { return pass == 0 ? get(b, a) : get(a, b); };  // a = line, b = position
      for (int a = 0; a < n; ++a) {
        int run = 1;
        for (int b = 1; b < n; ++b) {
          if (cell(a, b) == cell(a, b - 1)) ++run;
          else { if (run >= 5) p += 3 + (run - 5); run = 1; }
        }
        if (run >= 5) p += 3 + (run - 5);
        static const int pat[7] = {1, 0, 1, 1, 1, 0, 1};
        for (int b = 0; b + 7 <= n; ++b) {
          bool match = true;
          for (int k = 0; k < 7 && match; ++k) match = cell(a, b + k) == (pat[k] != 0);
          if (!match) continue;
          // Light run of four on at least one side; off-edge counts as light.
          bool lightBefore = true, lightAfter = true;
          for (int k = 1; k <= 4; ++k) {
            if (b - k >= 0 && cell(a, b - k)) lightBefore = false;
            if (b + 6 + k < n && cell(a, b + 6 + k)) lightAfter = false;
          }
          if (lightBefore) p += 40;
          if (lightAfter) p += 40;
        }
      }
    }
    for (int y = 0; y + 1 < n; ++y)
      for (int x = 0; x + 1 < n; ++x) {
        const bool c = get(x, y);
        if (c == get(x + 1, y) && c == get(x, y + 1) && c == get(x + 1, y + 1)) p += 3;
      }
    long dark = 0;
    for (auto v : m_mod) dark += v;
    const long total = static_cast<long>(n) * n;
    p += ((std::labs(dark * 20 - total * 10) + total - 1) / total - 1) * 10;
    return p;
  }

  int m_ver;
  QrEcc m_ecc;
  int m_size;
  std::vector<std::uint8_t> m_mod, m_fn;
};

}  // namespace

std::vector<std::uint8_t> qrReedSolomonDivisor(int degree) {
  std::vector<std::uint8_t> r(static_cast<std::size_t>(degree), 0);
  r[degree - 1] = 1;
  int root = 1;
  for (int i = 0; i < degree; ++i) {
    for (int j = 0; j < degree; ++j) {
      r[j] = static_cast<std::uint8_t>(gfMul(r[j], root));
      if (j + 1 < degree) r[j] ^= r[j + 1];
    }
    root = gfMul(root, 2);
  }
  return r;
}

std::vector<std::uint8_t> qrReedSolomonRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &divisor) {
  std::vector<std::uint8_t> r(divisor.size(), 0);
  for (auto b : data) {
    const int factor = b ^ r.front();
    r.erase(r.begin()); r.push_back(0);
    for (std::size_t i = 0; i < r.size(); ++i) r[i] ^= static_cast<std::uint8_t>(gfMul(divisor[i], factor));
  }
  return r;
}

std::uint32_t qrFormatBits(QrEcc ecc, int mask) {
  static const int formatIndicator[4] = {1, 0, 3, 2};  // L M Q H
  const std::uint32_t data = static_cast<std::uint32_t>(formatIndicator[static_cast<int>(ecc)] << 3 | mask);
  std::uint32_t rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  return (data << 10 | rem) ^ 0x5412;
}

std::uint32_t qrVersionBits(int version) {
  std::uint32_t rem = static_cast<std::uint32_t>(version);
  for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
  return static_cast<std::uint32_t>(version) << 12 | rem;
}

int qrByteCapacity(int version, QrEcc ecc) {
  if (version < 1 || version > 40) return 0;
  const int countBits = version <= 9 ? 8 : 16;
  const int bits = dataCodewords(version, ecc) * 8 - 4 - countBits;
  return bits < 0 ? 0 : std::min(bits / 8, countBits == 8 ? 255 : 65535);
}

QString qrEccName(QrEcc ecc) {
  switch (ecc) { case QrEcc::L: return QStringLiteral("L"); case QrEcc::M: return QStringLiteral("M"); case QrEcc::Q: return QStringLiteral("Q"); default: return QStringLiteral("H"); }
}

QrResult encodeQr(const QByteArray &bytes, QrEcc ecc, int minVersion) {
  if (minVersion < 1 || minVersion > 40) return {false, {}, QStringLiteral("qr-invalid-version")};
  if (bytes.isEmpty()) return {false, {}, QStringLiteral("qr-empty-content")};
  int ver = minVersion;
  while (ver <= 40 && qrByteCapacity(ver, ecc) < bytes.size()) ++ver;
  if (ver > 40) return {false, {}, QStringLiteral("qr-content-too-long")};

  std::vector<std::uint8_t> bits;  // one entry per bit, packed below
  auto append = [&](std::uint32_t value, int count) { for (int i = count - 1; i >= 0; --i) bits.push_back(bit(value, i)); };
  append(0x4, 4);
  append(static_cast<std::uint32_t>(bytes.size()), ver <= 9 ? 8 : 16);
  for (char c : bytes) append(static_cast<std::uint8_t>(c), 8);
  const std::size_t capBits = static_cast<std::size_t>(dataCodewords(ver, ecc)) * 8;
  append(0, static_cast<int>(std::min<std::size_t>(4, capBits - bits.size())));
  while (bits.size() % 8) bits.push_back(0);
  std::vector<std::uint8_t> data(bits.size() / 8, 0);
  for (std::size_t i = 0; i < bits.size(); ++i) data[i >> 3] |= static_cast<std::uint8_t>(bits[i] << (7 - (i & 7)));
  for (std::uint8_t pad = 0xEC; data.size() < capBits / 8; pad ^= 0xEC ^ 0x11) data.push_back(pad);

  return {true, Builder(ver, ecc).build(data), {}};
}

QrResult encodeQr(const QString &text, QrEcc ecc, int minVersion) { return encodeQr(text.toUtf8(), ecc, minVersion); }

}  // namespace xerahs::tools
