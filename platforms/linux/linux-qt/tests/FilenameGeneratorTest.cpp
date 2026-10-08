// Production-path checks that the deterministic conformance vectors cannot
// cover: the system random source and the default capture patterns.

#include "naming/FilenameGenerator.h"

#include <QRegularExpression>
#include <QTest>

using namespace xerahs::naming;

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
