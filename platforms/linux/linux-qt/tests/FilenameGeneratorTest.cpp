// Production-path checks that the deterministic conformance vectors cannot
// cover: the system random source and the default capture patterns.

#include "naming/FilenameGenerator.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

using namespace xerahs::naming;

namespace {
// Injected grant: the caller decides which paths may be read (FN-009).
class FakeFiles final : public RandomFileAccess {
public:
  QHash<QString, RandomFileRead> files;
  QStringList reads;
  RandomFileRead read(const QString &path) override {
    reads.append(path);
    return files.value(path);  // unknown paths are Missing
  }
};
RandomFileRead granted(const QByteArray &content) { return {RandomFileRead::Status::Granted, content}; }
}  // namespace

class FilenameGeneratorTest : public QObject {
  Q_OBJECT

private:
  static ExpansionContext context(RandomSource *random) {
    ExpansionContext c;
    c.date = QDate(2026, 10, 8);
    c.time = QTime(9, 5, 3, 7);
    c.locale = QLocale(QStringLiteral("en-AU"));
    c.counter = 41;
    c.processName = QStringLiteral("firefox");
    c.random = random;
    return c;
  }

private slots:
  void defaultCapturePatternUsesSystemRandom() {
    auto random = makeSystemRandomSource();
    const ExpansionResult result = expand({QStringLiteral("%y%mo%dT%h%mi_%ra{10}"), ParseMode::Filename,
                                           QStringLiteral("png"), std::nullopt},
                                          context(random.get()));
    QVERIFY(result.ok());
    QVERIFY(QRegularExpression(QStringLiteral("^20261008T0905_[0-9A-Za-z]{10}\\.png$"))
                .match(result.value())
                .hasMatch());
    QCOMPARE(result.nextCounter, 41);
  }

  void activeWindowDefaultPattern() {
    auto random = makeSystemRandomSource();
    const ExpansionResult result = expand({QStringLiteral("%y%mo%dT%h%mi_%pn_%ra{10}"),
                                           ParseMode::Filename, std::nullopt, std::nullopt},
                                          context(random.get()));
    QVERIFY(result.ok());
    QVERIFY(result.value().startsWith(QStringLiteral("20261008T0905_firefox_")));
  }

  // FN-009: with the grant, one trimmed non-empty line is selected (LF, CRLF and
  // blank lines are separators) by the supplied random byte (FN-021, FN-026).
  void randomFileReadsOneTrimmedLineWhenGranted() {
    FakeFiles files; files.files["words.txt"] = granted("alpha\r\n  beta  \n\n\u00e9clair\n");
    for (const auto &[byte, expected] : {std::pair{'\x00', "alpha"}, std::pair{'\x01', "beta"}, std::pair{'\x02', "\u00e9clair"}}) {
      auto random = makeFixedRandomSource(QByteArray(1, byte)); auto c = context(random.get()); c.files = &files;
      const auto result = expand({QStringLiteral("%rf{words.txt}"), ParseMode::Filename, QStringLiteral("png"), std::nullopt}, c);
      QVERIFY(result.ok()); QCOMPARE(result.value(), QString::fromUtf8(expected) + QStringLiteral(".png")); QCOMPARE(result.nextCounter, 41);
    }
    QCOMPARE(files.reads.size(), 3); QCOMPARE(files.reads.first(), QStringLiteral("words.txt"));
  }

  // FN-009/FN-026: a denied, missing, non-text or empty file is a typed error at the
  // token, the counter is not advanced, and no file content reaches the error.
  void randomFileFailuresAreTypedAndExposeNothing_data() {
    QTest::addColumn<int>("kind"); QTest::addColumn<QByteArray>("content"); QTest::addColumn<QString>("code");
    using S = RandomFileRead::Status;
    QTest::newRow("denied") << int(S::Denied) << QByteArray("secret-line\n") << QStringLiteral("file-permission-denied");
    QTest::newRow("missing") << int(S::Missing) << QByteArray() << QStringLiteral("file-unavailable");
    QTest::newRow("non-utf8") << int(S::Granted) << QByteArray("good\n\xff\xfe\n") << QStringLiteral("file-unavailable");
    QTest::newRow("truncated-utf8") << int(S::Granted) << QByteArray("good\n\xe2\x82") << QStringLiteral("file-unavailable");
    QTest::newRow("nul-byte") << int(S::Granted) << QByteArray("good\0line\n", 10) << QStringLiteral("file-unavailable");
    QTest::newRow("empty-file") << int(S::Granted) << QByteArray() << QStringLiteral("file-unavailable");
    QTest::newRow("blank-lines-only") << int(S::Granted) << QByteArray(" \n\r\n\t\n") << QStringLiteral("file-unavailable");
  }
  void randomFileFailuresAreTypedAndExposeNothing() {
    QFETCH(int, kind); QFETCH(QByteArray, content); QFETCH(QString, code);
    FakeFiles files; files.files["f.txt"] = {RandomFileRead::Status(kind), content};
    auto random = makeFixedRandomSource(QByteArray(1, '\x00')); auto c = context(random.get()); c.files = &files;
    const auto result = expand({QStringLiteral("ab%rf{f.txt}"), ParseMode::Filename, std::nullopt, std::nullopt}, c);
    QVERIFY(!result.ok()); QCOMPARE(errorCodeName(result.error().code), code);
    QCOMPARE(result.error().token, std::optional<QString>(QStringLiteral("%rf"))); QCOMPARE(result.error().offset, qint64(2));
    QCOMPARE(result.nextCounter, c.counter);
  }

  // FN-009: without any file access object (no grant) nothing is read.
  void randomFileWithoutAGrantIsDeniedAndReadsNothing() {
    auto random = makeFixedRandomSource(QByteArray(1, '\x00')); auto c = context(random.get());
    const auto result = expand({QStringLiteral("%rf{/etc/hostname}"), ParseMode::Filename, std::nullopt, std::nullopt}, c);
    QVERIFY(!result.ok()); QCOMPARE(errorCodeName(result.error().code), QStringLiteral("file-permission-denied"));
    QCOMPARE(result.error().offset, qint64(0));
  }

  void previewNeverNeedsInjectedRandomness() {
    const ExpansionResult result =
        preview({QStringLiteral("%i_%rx{4}"), ParseMode::Filename, std::nullopt, std::nullopt},
                context(nullptr));
    QVERIFY(result.ok());
    QCOMPARE(result.value(), QStringLiteral("42_0123"));
    QCOMPARE(result.nextCounter, 42);
  }
};

QTEST_GUILESS_MAIN(FilenameGeneratorTest)
#include "FilenameGeneratorTest.moc"
