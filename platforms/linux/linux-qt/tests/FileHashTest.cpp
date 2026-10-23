// EDITOR-UTILITIES-001 / EU-005: streamed digests of known content, of a file
// larger than one chunk, and of reads that cannot finish.
#include "tools/FileHash.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace xerahs::tools;

namespace {

// "abc" is the published test vector for every algorithm the tool offers.
QString writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  return file.write(bytes) == bytes.size() ? path : QString();
}

// An independent oracle: the test maps the enum itself and hashes the bytes in
// one call, so the streamed read is checked against a path production does not
// take rather than against itself.
QCryptographicHash::Algorithm oracleAlgorithm(HashAlgorithm algorithm) {
  switch (algorithm) {
  case HashAlgorithm::Md5: return QCryptographicHash::Md5;
  case HashAlgorithm::Sha1: return QCryptographicHash::Sha1;
  case HashAlgorithm::Sha256: return QCryptographicHash::Sha256;
  case HashAlgorithm::Sha512: return QCryptographicHash::Sha512;
  }
  return QCryptographicHash::Sha256;
}

QString oneShot(const QByteArray &bytes, HashAlgorithm algorithm) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, oracleAlgorithm(algorithm)).toHex());
}

}  // namespace

class FileHashTest : public QObject {
  Q_OBJECT

private slots:
  void knownDigestsOfAbc_data() {
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<QString>("expected");
    QTest::newRow("crc32") << int(HashAlgorithm::Crc32) << QStringLiteral("352441c2");
    QTest::newRow("md5") << int(HashAlgorithm::Md5) << QStringLiteral("900150983cd24fb0d6963f7d28e17f72");
    QTest::newRow("sha1") << int(HashAlgorithm::Sha1) << QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d");
    QTest::newRow("sha256") << int(HashAlgorithm::Sha256) << QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    QTest::newRow("sha384") << int(HashAlgorithm::Sha384) << QStringLiteral("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
    QTest::newRow("sha512") << int(HashAlgorithm::Sha512) << QStringLiteral("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  }
  void knownDigestsOfAbc() {
    QFETCH(int, algorithm);
    QFETCH(QString, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    const auto result = hashFile(path, HashAlgorithm(algorithm));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.hex, expected);
  }

  void emptyFileHashes_data() {
    QTest::addColumn<int>("algorithm");
    QTest::newRow("md5") << int(HashAlgorithm::Md5);
    QTest::newRow("sha1") << int(HashAlgorithm::Sha1);
    QTest::newRow("sha256") << int(HashAlgorithm::Sha256);
    QTest::newRow("sha512") << int(HashAlgorithm::Sha512);
  }
  void emptyFileHashes() {
    QFETCH(int, algorithm);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("empty"), QByteArray());
    QVERIFY(!path.isEmpty());
    QCOMPARE(QFileInfo(path).size(), 0LL);
    const auto result = hashFile(path, HashAlgorithm(algorithm));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.hex, oneShot(QByteArray(), HashAlgorithm(algorithm)));
  }

  // EU-005: streaming must not change the digest, so a file spanning several
  // chunks has to agree with the same bytes hashed in one call.
  void largeFileMatchesOneShotHash_data() {
    QTest::addColumn<int>("algorithm");
    QTest::newRow("md5") << int(HashAlgorithm::Md5);
    QTest::newRow("sha1") << int(HashAlgorithm::Sha1);
    QTest::newRow("sha256") << int(HashAlgorithm::Sha256);
    QTest::newRow("sha512") << int(HashAlgorithm::Sha512);
  }
  void largeFileMatchesOneShotHash() {
    QFETCH(int, algorithm);
    const auto hash = HashAlgorithm(algorithm);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Three chunks plus a partial one, so the last read is a short tail.
    QByteArray bytes(3 * 1024 * 1024 + 517, 'x');
    for (int i = 0; i < bytes.size(); i += 4096) bytes[i] = char('a' + (i / 4096) % 26);
    const auto path = writeFile(dir.filePath("large"), bytes);
    QVERIFY(!path.isEmpty());
    QCOMPARE(QFileInfo(path).size(), qint64(bytes.size()));

    qint64 reportedHashed = 0, reportedTotal = -1;
    int reports = 0;
    const auto result = hashFile(path, hash, nullptr, [&](qint64 hashed, qint64 total) {
      ++reports;
      reportedHashed = hashed;
      reportedTotal = total;
    });
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.hex, oneShot(bytes, hash));
    QVERIFY(reports > 1);
    QCOMPARE(reportedHashed, qint64(bytes.size()));
    QCOMPARE(reportedTotal, qint64(bytes.size()));
  }

  void missingFileIsUnreadable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto result = hashFile(dir.filePath("absent"), HashAlgorithm::Sha256);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("file-unreadable"));
    QVERIFY(result.hex.isEmpty());
  }

  void unreadableFileIsUnreadable_data() {
    QTest::addColumn<int>("algorithm");
    QTest::newRow("md5") << int(HashAlgorithm::Md5);
    QTest::newRow("sha512") << int(HashAlgorithm::Sha512);
  }
  void unreadableFileIsUnreadable() {
    QFETCH(int, algorithm);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("locked"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions()));
    // Root reads any file, so this only proves the error when the mode is honoured.
    if (!QFileInfo(path).isReadable()) {
      const auto result = hashFile(path, HashAlgorithm(algorithm));
      QVERIFY(!result.ok);
      QCOMPARE(result.error, QStringLiteral("file-unreadable"));
      QVERIFY(result.hex.isEmpty());
    }
  }

  void directoryIsUnreadable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto result = hashFile(dir.path(), HashAlgorithm::Sha256);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("file-unreadable"));
    QVERIFY(result.hex.isEmpty());
  }

  void cancelledBeforeReadingReportsCancelled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    std::atomic_bool cancelled{true};
    const auto result = hashFile(path, HashAlgorithm::Sha256, &cancelled);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("cancelled"));
    QVERIFY(result.hex.isEmpty());
  }

  // A cancel that arrives part way through must stop the read rather than
  // return a digest over part of the file.
  void cancelledWhileReadingStopsAndReportsCancelled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray bytes(8 * 1024 * 1024, 'z');
    const auto path = writeFile(dir.filePath("big"), bytes);
    QVERIFY(!path.isEmpty());
    std::atomic_bool cancelled{false};
    int reports = 0;
    const auto result = hashFile(path, HashAlgorithm::Sha512, &cancelled, [&](qint64, qint64) {
      if (++reports == 1) cancelled.store(true);
    });
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("cancelled"));
    QVERIFY(result.hex.isEmpty());
    // One chunk was read, then the flag was seen at the next chunk boundary.
    QCOMPARE(reports, 1);
  }

  void digestIsLowercaseHex_data() {
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<int>("length");
    QTest::newRow("md5") << int(HashAlgorithm::Md5) << 32;
    QTest::newRow("sha1") << int(HashAlgorithm::Sha1) << 40;
    QTest::newRow("sha256") << int(HashAlgorithm::Sha256) << 64;
    QTest::newRow("sha512") << int(HashAlgorithm::Sha512) << 128;
  }
  void digestIsLowercaseHex() {
    QFETCH(int, algorithm);
    QFETCH(int, length);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    const auto result = hashFile(path, HashAlgorithm(algorithm));
    QVERIFY(result.ok);
    QCOMPARE(result.hex.size(), length);
    QCOMPARE(result.hex, result.hex.toLower());
    QVERIFY(result.hex.contains(QRegularExpression(QStringLiteral("^[0-9a-f]+$"))));
  }

  void algorithmNamesAreOrderedAndDistinct() {
    const auto names = hashAlgorithmNames();
    // EU-005: CRC32, MD5, SHA-1, SHA-256, SHA-384 and SHA-512, in enum order.
    QCOMPARE(names, (QStringList{"CRC32", "MD5", "SHA-1", "SHA-256", "SHA-384", "SHA-512"}));
    // Each algorithm needs its own entry, or a chooser would show a duplicate.
    for (int first = 0; first < names.size(); ++first)
      for (int second = first + 1; second < names.size(); ++second) QVERIFY(names.at(first) != names.at(second));
    QCOMPARE(hashAlgorithmName(HashAlgorithm::Md5), QStringLiteral("MD5"));
    QCOMPARE(hashAlgorithmName(HashAlgorithm::Sha1), QStringLiteral("SHA-1"));
    QCOMPARE(hashAlgorithmName(HashAlgorithm::Sha256), QStringLiteral("SHA-256"));
    QCOMPARE(hashAlgorithmName(HashAlgorithm::Sha512), QStringLiteral("SHA-512"));
  }

  void digestMatchesIgnoresCaseAndSurroundingSpace_data() {
    QTest::addColumn<QString>("expected");
    QTest::addColumn<bool>("matches");
    const auto digest = QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    QTest::newRow("exact") << digest << true;
    QTest::newRow("uppercase") << digest.toUpper() << true;
    QTest::newRow("mixed case") << QStringLiteral("bA7816BF8f01cfEA414140de5dAe2223b00361a396177A9cb410ff61f20015AD") << true;
    QTest::newRow("leading space") << QStringLiteral("  ") + digest << true;
    QTest::newRow("trailing space") << digest + QStringLiteral("  ") << true;
    QTest::newRow("padded and uppercase") << QStringLiteral("  ") + digest.toUpper() + QStringLiteral("\n") << true;
    QTest::newRow("different digit") << QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae") << false;
    QTest::newRow("prefix only") << digest.left(60) << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("only space") << QStringLiteral("   ") << false;
  }
  void digestMatchesIgnoresCaseAndSurroundingSpace() {
    QFETCH(QString, expected);
    QFETCH(bool, matches);
    const auto digest = QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    QCOMPARE(digestMatches(digest, expected), matches);
    QCOMPARE(digestMatches(digest.trimmed(), expected.trimmed()), matches);
  }

  void digestMatchesAnEmptyDigest() {
    QCOMPARE(digestMatches(QString(), QString()), false);
    QCOMPARE(digestMatches(QString(), QStringLiteral("  ")), false);
  }
};

QTEST_GUILESS_MAIN(FileHashTest)
#include "FileHashTest.moc"