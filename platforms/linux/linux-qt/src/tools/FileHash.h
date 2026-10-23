// EDITOR-UTILITIES-001 / EU-005: the hash checker tool reports a file digest.
// Files are streamed in fixed chunks, so any size hashes without growing the
// reader's memory, and the read is cancellable at a chunk boundary.
#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace xerahs::tools {

enum class HashAlgorithm { Crc32, Md5, Sha1, Sha256, Sha384, Sha512 };

// error is empty on success, or "file-unreadable" / "cancelled". hex is the
// lowercase digest and is empty unless ok.
struct HashResult {
  bool ok = false;
  QString hex;
  QString error;
};

// Bytes hashed so far, then the file size. total is -1 when the size is not
// known in advance, so a caller shows an indeterminate bar.
using HashProgress = std::function<void(qint64 hashed, qint64 total)>;

// Reads path in 1 MiB chunks and returns its digest. A non-null cancelled is
// polled at every chunk boundary, so a cancelled read stops without hashing the
// rest of the file and reports "cancelled" rather than a partial digest.
HashResult hashFile(const QString &path, HashAlgorithm algorithm,
                    const std::atomic_bool *cancelled = nullptr,
                    const HashProgress &progress = {});

// EU-006: MD5, SHA-1 and CRC32 must not be used to decide authenticity.
bool hashAlgorithmIsWeak(HashAlgorithm algorithm);

// Display name of one algorithm, for a chooser or a result heading.
QString hashAlgorithmName(HashAlgorithm algorithm);

// Display names in enum order, so a chooser never has to know the order.
QStringList hashAlgorithmNames();

// Compares a computed digest with an expected value pasted from elsewhere.
// Case is ignored and surrounding whitespace is trimmed, because a digest is
// copied from terminals and issue reports in either case.
bool digestMatches(const QString &digest, const QString &expected);

}  // namespace xerahs::tools