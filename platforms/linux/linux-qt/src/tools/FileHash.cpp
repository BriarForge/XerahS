#include "tools/FileHash.h"

#include <QCryptographicHash>
#include <QFile>

#include <zlib.h>

namespace xerahs::tools {

namespace {

// EU-005: stream the file, so peak memory is one chunk rather than the file.
constexpr qint64 kChunkBytes = 1024 * 1024;

QString errorFileUnreadable() { return QStringLiteral("file-unreadable"); }
QString errorCancelled() { return QStringLiteral("cancelled"); }

HashResult cancelledResult() { return {false, {}, errorCancelled()}; }
HashResult unreadableResult() { return {false, {}, errorFileUnreadable()}; }

QCryptographicHash::Algorithm qtAlgorithm(HashAlgorithm algorithm) {
  switch (algorithm) {
  case HashAlgorithm::Crc32: break;  // zlib, not QCryptographicHash
  case HashAlgorithm::Md5: return QCryptographicHash::Md5;
  case HashAlgorithm::Sha1: return QCryptographicHash::Sha1;
  case HashAlgorithm::Sha256: return QCryptographicHash::Sha256;
  case HashAlgorithm::Sha384: return QCryptographicHash::Sha384;
  case HashAlgorithm::Sha512: return QCryptographicHash::Sha512;
  }
  return QCryptographicHash::Sha256;
}

QString name(HashAlgorithm algorithm) {
  switch (algorithm) {
  case HashAlgorithm::Crc32: return QStringLiteral("CRC32");
  case HashAlgorithm::Md5: return QStringLiteral("MD5");
  case HashAlgorithm::Sha1: return QStringLiteral("SHA-1");
  case HashAlgorithm::Sha256: return QStringLiteral("SHA-256");
  case HashAlgorithm::Sha384: return QStringLiteral("SHA-384");
  case HashAlgorithm::Sha512: return QStringLiteral("SHA-512");
  }
  return {};
}

}  // namespace

HashResult hashFile(const QString &path, HashAlgorithm algorithm,
                    const std::atomic_bool *cancelled, const HashProgress &progress) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return unreadableResult();
  QCryptographicHash hash(qtAlgorithm(algorithm));
  uLong crc = crc32(0L, Z_NULL, 0);
  const qint64 total = file.size();
  qint64 hashed = 0;
  while (true) {
    if (cancelled && cancelled->load()) return cancelledResult();
    const auto chunk = file.read(kChunkBytes);
    if (chunk.isEmpty()) break;
    if (algorithm == HashAlgorithm::Crc32) crc = crc32(crc, reinterpret_cast<const Bytef *>(chunk.constData()), uInt(chunk.size()));
    else hash.addData(chunk);
    hashed += chunk.size();
    if (progress) progress(hashed, total);
  }
  // A read that failed part way through leaves a digest over part of the file,
  // which is worse than reporting that the file could not be read.
  if (file.error() != QFileDevice::NoError) return unreadableResult();
  if (cancelled && cancelled->load()) return cancelledResult();
  if (algorithm == HashAlgorithm::Crc32) return {true, QStringLiteral("%1").arg(quint32(crc), 8, 16, QLatin1Char('0')), {}};
  return {true, QString::fromLatin1(hash.result().toHex()), {}};
}

QString hashAlgorithmName(HashAlgorithm algorithm) { return name(algorithm); }

bool hashAlgorithmIsWeak(HashAlgorithm algorithm) {
  return algorithm == HashAlgorithm::Crc32 || algorithm == HashAlgorithm::Md5 || algorithm == HashAlgorithm::Sha1;
}

QStringList hashAlgorithmNames() {
  QStringList names;
  for (const auto algorithm : {HashAlgorithm::Crc32, HashAlgorithm::Md5, HashAlgorithm::Sha1, HashAlgorithm::Sha256,
                               HashAlgorithm::Sha384, HashAlgorithm::Sha512})
    names << name(algorithm);
  return names;
}

bool digestMatches(const QString &digest, const QString &expected) {
  const auto left = digest.trimmed().toLower();
  const auto right = expected.trimmed().toLower();
  // No digest means nothing was hashed, which is not a match against an empty
  // field the user has not filled in yet.
  if (left.isEmpty() || right.isEmpty()) return false;
  // The length follows from the algorithm, so an early return here leaks nothing.
  if (left.size() != right.size()) return false;
  // EU-005: compare every digit rather than stopping at the first difference,
  // so the result does not depend on where two values parted.
  unsigned char difference = 0;
  for (qsizetype index = 0; index < left.size(); ++index)
    difference = static_cast<unsigned char>(difference |
                                           (static_cast<unsigned char>(left.at(index).toLatin1()) ^
                                            static_cast<unsigned char>(right.at(index).toLatin1())));
  return difference == 0;
}

}  // namespace xerahs::tools