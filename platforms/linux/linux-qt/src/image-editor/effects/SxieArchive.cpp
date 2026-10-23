// COMPAT-SXIE-001, EE-015, ROOT-SECURITY-001.
#include "SxieArchive.h"
#include <QtEndian>
#include <zlib.h>
namespace xerahs::effects {
namespace {
quint16 u16(const QByteArray &b, qsizetype p) { return qFromLittleEndian<quint16>(b.constData() + p); }
quint32 u32(const QByteArray &b, qsizetype p) { return qFromLittleEndian<quint32>(b.constData() + p); }
bool range(const QByteArray &b, quint64 p, quint64 n) { return p <= quint64(b.size()) && n <= quint64(b.size()) - p; }
bool safeName(const QString &s) {
  if (s.isEmpty() || s.startsWith('/') || s.contains('\\') || s.contains(':') || s.contains(QChar(0))) return false;
  for (const auto &part : s.split('/')) if (part == ".." || part == ".") return false;
  return true;
}
bool extraValid(const QByteArray &b, qsizetype p, qsizetype n) {
  const auto end = p + n;
  while (p < end) {
    if (end - p < 4 || u16(b, p) == 1) return false;
    const auto size = u16(b, p + 2); p += 4;
    if (size > end - p) return false;
    p += size;
  }
  return p == end;
}
}
SxieArchive readSxieArchive(const QByteArray &b) {
  const auto fail = [](const char *code) { return SxieArchive{{}, QString::fromLatin1(code)}; };
  if (b.size() < 22 || b.size() > 160 * 1024 * 1024) return fail("sxie-zip-size");
  qsizetype end = -1;
  for (qsizetype p = b.size() - 22; p >= qMax<qsizetype>(0, b.size() - 65557); --p)
    if (u32(b, p) == 0x06054b50 && p + 22 + u16(b, p + 20) == b.size()) { end = p; break; }
  if (end < 0) return fail("sxie-zip-directory");
  if (u16(b, end + 4) || u16(b, end + 6) || u16(b, end + 8) != u16(b, end + 10)) return fail("sxie-zip-multidisk");
  const auto count = u16(b, end + 10); const auto size = u32(b, end + 12); const auto offset = u32(b, end + 16);
  if (count == 65535 || size == 0xffffffff || offset == 0xffffffff || (end >= 20 && u32(b, end - 20) == 0x07064b50)) return fail("sxie-zip64");
  if (count > 256) return fail("sxie-zip-entry-limit");
  if (!range(b, offset, size) || quint64(offset) + size != quint64(end)) return fail("sxie-zip-directory");
  SxieArchive result; quint64 total = 0; qsizetype p = offset;
  QVector<QPair<quint64, quint64>> regions;
  for (int i = 0; i < count; ++i) {
    if (!range(b, p, 46) || p + 46 > end || u32(b, p) != 0x02014b50) return fail("sxie-zip-directory");
    const auto flags = u16(b, p + 8), method = u16(b, p + 10);
    const auto crc = u32(b, p + 16), packed = u32(b, p + 20), unpacked = u32(b, p + 24);
    const auto nl = u16(b, p + 28), el = u16(b, p + 30), cl = u16(b, p + 32);
    const auto local = u32(b, p + 42);
    if (u16(b, p + 6) >= 45 || packed == 0xffffffff || unpacked == 0xffffffff || local == 0xffffffff || u16(b, p + 34) == 65535) return fail("sxie-zip64");
    if (u16(b, p + 34)) return fail("sxie-zip-multidisk");
    if (flags & (1 | 64 | 8192)) return fail("sxie-zip-encrypted");
    if (flags & ~(8 | 2048 | 2 | 4)) return fail("sxie-zip-flags");
    if (method != 0 && method != 8) return fail("sxie-zip-compression");
    if (!range(b, p + 46, quint64(nl) + el + cl) || p + 46 + nl + el + cl > end) return fail("sxie-zip-directory");
    if (!extraValid(b, p + 46 + nl, el)) return fail("sxie-zip64");
    const auto rawName = b.mid(p + 46, nl); const auto name = QString::fromUtf8(rawName);
    if (name.toUtf8() != rawName || !safeName(name)) return fail("sxie-zip-path");
    if (result.entries.contains(name)) return fail("sxie-zip-duplicate");
    total += unpacked;
    if (unpacked > 32 * 1024 * 1024 || total > 128 * 1024 * 1024 || quint64(unpacked) > quint64(packed) * 200) return fail("sxie-zip-resource-limit");
    if (!range(b, local, 30) || quint64(local) + 30 > offset || u32(b, local) != 0x04034b50) return fail("sxie-zip-local");
    if (u16(b, local + 4) >= 45 || u32(b, local + 18) == 0xffffffff || u32(b, local + 22) == 0xffffffff) return fail("sxie-zip64");
    const auto ln = u16(b, local + 26), le = u16(b, local + 28);
    const quint64 data = quint64(local) + 30 + ln + le;
    if (!range(b, local + 30, quint64(ln) + le) || data + packed > offset ||
        u16(b, local + 6) != flags || u16(b, local + 8) != method || b.mid(local + 30, ln) != rawName ||
        !extraValid(b, local + 30 + ln, le)) return fail("sxie-zip-local");
    if (!(flags & 8) && (u32(b, local + 14) != crc || u32(b, local + 18) != packed || u32(b, local + 22) != unpacked)) return fail("sxie-zip-local");
    quint64 dataEnd = data + packed;
    if (flags & 8) {
      if (!range(b, dataEnd, 12)) return fail("sxie-zip-descriptor");
      if (u32(b, dataEnd) == 0x08074b50) dataEnd += 4;
      if (!range(b, dataEnd, 12) || dataEnd + 12 > offset || u32(b, dataEnd) != crc ||
          u32(b, dataEnd + 4) != packed || u32(b, dataEnd + 8) != unpacked) return fail("sxie-zip-descriptor");
      dataEnd += 12;
    }
    for (const auto &region : regions) if (local < region.second && dataEnd > region.first) return fail("sxie-zip-overlap");
    regions.append({local, dataEnd});
    QByteArray output(unpacked ? unpacked : 1, Qt::Uninitialized);
    if (method == 0) {
      if (packed != unpacked) return fail("sxie-zip-size");
      output = b.mid(data, packed);
    } else {
      z_stream stream{}; stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(b.constData() + data)); stream.avail_in = packed;
      stream.next_out = reinterpret_cast<Bytef *>(output.data()); stream.avail_out = output.size();
      if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return fail("sxie-zip-inflate");
      const int status = inflate(&stream, Z_FINISH); inflateEnd(&stream);
      if (status != Z_STREAM_END || stream.total_out != unpacked || stream.total_in != packed) return fail("sxie-zip-inflate");
      output.resize(unpacked);
    }
    if (crc32(0, reinterpret_cast<const Bytef *>(output.constData()), output.size()) != crc) return fail("sxie-zip-crc");
    result.entries.insert(name, output); p += 46 + nl + el + cl;
  }
  if (p != end) return fail("sxie-zip-directory");
  return result;
}
}
