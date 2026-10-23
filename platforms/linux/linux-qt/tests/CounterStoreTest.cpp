// FILENAME-GENERATION-001 FN-016/FN-025: persisted counter reservation.
#include "naming/CounterStore.h"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <limits>
using namespace xerahs::naming;
namespace {
QByteArray bytes(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
void write(const QString &p, const QByteArray &b) { QFile f(p); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(b); }
const QByteArray kHead = R"({"format":"com.xerahs.app/linux-qt/filename-counters",)";
qint64 take(const CounterStore &s, const QString &profile, bool commit = true) {
  auto r = s.reserve(profile); if (r.error != CounterError::None) return -1;
  const auto v = r.reservation.value(); if (commit) s.commit(r.reservation, v + 1); return v;
}
}
class CounterStoreTest final : public QObject {
  Q_OBJECT
private slots:
  void sequenceSurvivesReopenAndFileIsPrivate() {
    QTemporaryDir dir; const auto path = dir.filePath("sub/counters.json");
    { CounterStore s(path); QCOMPARE(take(s, "a"), qint64(0)); QCOMPARE(take(s, "a"), qint64(1)); }
    CounterStore s(path); QCOMPARE(s.peek("a").value, qint64(2)); QCOMPARE(take(s, "a"), qint64(2));
    QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther), QFile::Permissions{});
  }
  void uncommittedReservationDoesNotConsume() {
    QTemporaryDir dir; CounterStore s(dir.filePath("c.json"));
    QCOMPARE(take(s, "a", false), qint64(0)); QVERIFY(!QFile::exists(dir.filePath("c.json")));
    QCOMPARE(take(s, "a"), qint64(0)); QCOMPARE(take(s, "a", false), qint64(1)); QCOMPARE(s.peek("a").value, qint64(1));
  }
  void profilesAreIndependent() {
    QTemporaryDir dir; CounterStore s(dir.filePath("c.json"));
    QCOMPARE(take(s, "a"), qint64(0)); QCOMPARE(take(s, "a"), qint64(1)); QCOMPARE(take(s, "b"), qint64(0)); QCOMPARE(s.peek("a").value, qint64(2)); QCOMPARE(s.peek("b").value, qint64(1));
  }
  void concurrentReservationsNeverShareAValue() {
    QTemporaryDir dir; const auto path = dir.filePath("c.json"); CounterStore first(path, 0), second(path, 0);
    auto a = first.reserve("p"); QCOMPARE(a.error, CounterError::None);
    auto b = second.reserve("p"); QCOMPARE(b.error, CounterError::Busy); QVERIFY(!b.reservation);   // interleaved
    QCOMPARE(first.commit(a.reservation, a.reservation.value() + 1), CounterError::None);
    b = second.reserve("p"); QCOMPARE(b.error, CounterError::None); QCOMPARE(b.reservation.value(), qint64(1));
    b.reservation = {};  // rollback releases the lock
    QCOMPARE(take(first, "p"), qint64(1));
  }
  void overflowIsTypedAndFileUntouched() {
    QTemporaryDir dir; const auto path = dir.filePath("c.json"); const auto max = QByteArray::number(std::numeric_limits<qint64>::max());
    write(path, kHead + R"("version":1,"counters":{"p":")" + max + R"("}})"); const auto before = bytes(path);
    CounterStore s(path); QCOMPARE(s.reserve("p").error, CounterError::Overflow); QCOMPARE(bytes(path), before);
    write(path, kHead + R"("version":1,"counters":{"p":")" + QByteArray::number(std::numeric_limits<qint64>::max() - 1) + R"("}})");
    auto r = s.reserve("p"); QCOMPARE(r.error, CounterError::None); QCOMPARE(s.commit(r.reservation, std::numeric_limits<qint64>::max()), CounterError::None);
    QCOMPARE(s.reserve("p").error, CounterError::Overflow);
  }
  void corruptAndNewerFilesErrorWithoutOverwrite_data() {
    QTest::addColumn<QByteArray>("content"); QTest::addColumn<int>("error");
    QTest::newRow("garbage") << QByteArray("not json") << int(CounterError::Corrupt);
    QTest::newRow("wrong-format") << QByteArray(R"({"format":"x","version":1,"counters":{}})") << int(CounterError::Corrupt);
    QTest::newRow("negative") << (kHead + R"("version":1,"counters":{"p":"-3"}})") << int(CounterError::Corrupt);
    QTest::newRow("number-not-string") << (kHead + R"("version":1,"counters":{"p":3}})") << int(CounterError::Corrupt);
    QTest::newRow("newer") << (kHead + R"("version":2,"counters":{"p":"4"},"future":true})") << int(CounterError::NewerVersion);
  }
  void corruptAndNewerFilesErrorWithoutOverwrite() {
    QFETCH(QByteArray, content); QFETCH(int, error); QTemporaryDir dir; const auto path = dir.filePath("c.json"); write(path, content);
    CounterStore s(path); QCOMPARE(int(s.reserve("p").error), error); QCOMPARE(int(s.peek("p").error), error); QCOMPARE(bytes(path), content);
  }
  void changedFileBetweenReserveAndCommitIsRefused() {
    QTemporaryDir dir; const auto path = dir.filePath("c.json"); CounterStore s(path); QCOMPARE(take(s, "p"), qint64(0));
    auto r = s.reserve("p"); QCOMPARE(r.error, CounterError::None);
    write(path, kHead + R"("version":1,"counters":{"p":"9"}})"); const auto before = bytes(path);
    QCOMPARE(s.commit(r.reservation, 2), CounterError::Changed); QCOMPARE(bytes(path), before);
  }
};
QTEST_GUILESS_MAIN(CounterStoreTest)
#include "CounterStoreTest.moc"
