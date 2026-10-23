// CORE-004, EU-017: the shared worker helper's delivery, lifetime and cancellation rules.
#include "OwnedWork.h"

#include <QCoreApplication>
#include <QTest>

#include <atomic>

using xerahs::app::OwnedWork;

class OwnedWorkTest : public QObject {
  Q_OBJECT
private slots:
  void deliversResultOnTheOwnerThread() {
    QObject owner;
    std::atomic<Qt::HANDLE> workThread{nullptr};
    Qt::HANDLE doneThread = nullptr;
    int result = 0;
    const auto work = OwnedWork::run<int>(&owner, [&](const OwnedWork::Context &) { workThread = QThread::currentThreadId(); return 42; },
                                          [&](int value) { result = value; doneThread = QThread::currentThreadId(); });
    QTRY_COMPARE(result, 42);
    QVERIFY(workThread.load() != QThread::currentThreadId());
    QCOMPARE(doneThread, QThread::currentThreadId());
    QTRY_VERIFY(!work.running());
  }

  void destroyedOwnerNeverSeesTheResult() {
    std::atomic_bool release{false}, finished{false};
    bool delivered = false;
    {
      QObject owner;
      OwnedWork::run<int>(&owner, [&](const OwnedWork::Context &) {
        while (!release.load()) QThread::msleep(1);
        finished = true; return 1;
      }, [&](int) { delivered = true; });
    }
    release = true;
    QTRY_VERIFY(finished.load());
    QTest::qWait(50);
    QVERIFY(!delivered);
  }

  void cancellationIsCooperativeAndStillDelivers() {
    QObject owner;
    std::atomic_bool started{false};
    std::optional<QString> outcome;
    const auto work = OwnedWork::run<QString>(&owner, [&](const OwnedWork::Context &context) {
      started = true;
      while (!context.cancelled()) QThread::msleep(1);
      return QStringLiteral("cancelled");
    }, [&](QString value) { outcome = value; });
    QTRY_VERIFY(started.load());
    QVERIFY(work.running());
    work.cancel();
    QVERIFY(work.cancelled());
    QTRY_COMPARE(outcome, std::optional<QString>(QStringLiteral("cancelled")));
  }

  void progressIsCoalescedAndDeliveredOnTheOwnerThread() {
    QObject owner;
    int calls = 0; qint64 last = -1; bool onOwner = true; bool done = false;
    OwnedWork::run<int>(&owner, [](const OwnedWork::Context &context) {
      for (qint64 i = 1; i <= 10000; ++i) context.progress(i, 10000);
      return 0;
    }, [&](int) { done = true; }, [&](qint64 hashed, qint64 total) {
      ++calls; last = hashed; onOwner = onOwner && QThread::currentThread() == qApp->thread(); QCOMPARE(total, 10000);
    });
    QTRY_VERIFY(done);
    QTRY_COMPARE(last, 10000);
    QVERIFY(onOwner);
    QVERIFY(calls >= 1 && calls < 10000);
  }

  void throwingWorkSkipsDone() {
    QObject owner;
    bool delivered = false;
    const auto work = OwnedWork::run<int>(&owner, [](const OwnedWork::Context &) -> int { throw std::runtime_error("boom"); },
                                          [&](int) { delivered = true; });
    QTRY_VERIFY(!work.running());
    QVERIFY(!delivered);
  }
};

QTEST_GUILESS_MAIN(OwnedWorkTest)
#include "OwnedWorkTest.moc"
