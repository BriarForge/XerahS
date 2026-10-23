// CORE-016: bounded schedules, skipped ticks, and delay cancellation.
#include "capture/CaptureSchedule.h"

#include <QTest>

#include <optional>

using namespace xerahs::capture;

namespace {

class ManualTimer final : public Timer {
public:
  void arm(int intervalMs) override {
    interval = intervalMs;
    armed = intervalMs > 0;
  }
  bool armed = false;
  int interval = 0;
};

struct Harness {
  explicit Harness(qint64 now = 0) : clock(now), schedule(clock, timer) {
    QObject::connect(&schedule, &CaptureSchedule::captureRequested, &schedule, [this](CaptureSource) { ++requests; });
    QObject::connect(&schedule, &CaptureSchedule::delayElapsed, &schedule, [this](CaptureSource) { ++elapsed; });
    QObject::connect(&schedule, &CaptureSchedule::delayCancelled, &schedule, [this] { ++cancelled; });
    QObject::connect(&schedule, &CaptureSchedule::autoFinished, &schedule, [this](const AutoSummary &result) {
      ++finished;
      summary = result;
    });
  }
  ManualClock clock;
  ManualTimer timer;
  CaptureSchedule schedule;
  int requests = 0;
  int elapsed = 0;
  int cancelled = 0;
  int finished = 0;
  AutoSummary summary;
};

AutoRequest counted(int count, int intervalMs = 1000, CaptureSource source = CaptureSource::FullScreen) {
  return {source, intervalMs, CountBound{count}};
}

}  // namespace

class CaptureScheduleTest : public QObject {
  Q_OBJECT
private slots:
  void unboundedScheduleIsRejected() {
    const qint64 now = 5000;
    QVERIFY(validateAuto(CaptureSource::FullScreen, 1000, std::nullopt, now));
    QCOMPARE(validateAuto(CaptureSource::FullScreen, 1000, std::nullopt, now)->code, QStringLiteral("bound-required"));
    QVERIFY(validateAuto(CaptureSource::Region, 1000, CountBound{1}, now));
    QCOMPARE(validateAuto(CaptureSource::Region, 1000, CountBound{1}, now)->code, QStringLiteral("source-unavailable"));
    for (int interval : {0, 999, -1, kMaxIntervalMs + 1})
      QCOMPARE(validateAuto(CaptureSource::Monitor, interval, CountBound{1}, now)->code, QStringLiteral("interval-out-of-range"));
    for (int count : {0, -5, kMaxCaptureCount + 1})
      QCOMPARE(validateAuto(CaptureSource::ActiveWindow, 1000, CountBound{count}, now)->code, QStringLiteral("count-out-of-range"));
    QCOMPARE(validateAuto(CaptureSource::FullScreen, 1000, TimeBound{now}, now)->code, QStringLiteral("end-not-after-start"));
    QCOMPARE(validateAuto(CaptureSource::FullScreen, 1000, TimeBound{now - 1}, now)->code, QStringLiteral("end-not-after-start"));
    QCOMPARE(validateAuto(CaptureSource::FullScreen, 1000, TimeBound{now + kMaxScheduleWindowMs + 1}, now)->code, QStringLiteral("end-beyond-limit"));
    QVERIFY(!validateAuto(CaptureSource::FullScreen, 1000, CountBound{1}, now));
    QVERIFY(!validateAuto(CaptureSource::Monitor, kMaxIntervalMs, CountBound{kMaxCaptureCount}, now));
    QVERIFY(!validateAuto(CaptureSource::ActiveWindow, 1000, TimeBound{now + kMaxScheduleWindowMs}, now));
    for (int seconds : {0, -1, 61, 120})
      QCOMPARE(validateDelay({CaptureSource::Region, seconds})->code, QStringLiteral("delay-seconds-out-of-range"));
    QVERIFY(!validateDelay({CaptureSource::Region, 1}));
    QVERIFY(!validateDelay({CaptureSource::ActiveWindow, 60}));

    Harness harness(now);
    QCOMPARE(harness.schedule.startAuto({CaptureSource::FullScreen, 500, CountBound{1}}), QStringLiteral("interval-out-of-range"));
    QCOMPARE(harness.schedule.startAuto({CaptureSource::Region, 1000, CountBound{1}}), QStringLiteral("source-unavailable"));
    QCOMPARE(harness.schedule.startAuto({CaptureSource::FullScreen, 1000, CountBound{0}}), QStringLiteral("count-out-of-range"));
    QCOMPARE(harness.schedule.startAuto({CaptureSource::FullScreen, 1000, TimeBound{now}}), QStringLiteral("end-not-after-start"));
    QCOMPARE(harness.schedule.startDelay({CaptureSource::Region, 0}), QStringLiteral("delay-seconds-out-of-range"));
    QVERIFY(!harness.timer.armed);
    QCOMPARE(harness.requests, 0);
    QCOMPARE(harness.elapsed, 0);
    QVERIFY(!harness.schedule.autoActive());
    QVERIFY(!harness.schedule.delayActive());
  }

  void busyTickIsSkippedAndNotQueued() {
    Harness harness;
    QCOMPARE(harness.schedule.startAuto(counted(4)), QString());
    QCOMPARE(harness.requests, 1);
    QCOMPARE(harness.schedule.statusText(), QStringLiteral("Auto capture: 1 of 4"));
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    harness.schedule.setBusy(true);
    harness.clock.advance(1000);
    harness.schedule.onTimer();
    harness.clock.advance(1000);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 1);
    QCOMPARE(harness.schedule.summary().skipped, 2);
    QCOMPARE(harness.schedule.statusText(), QStringLiteral("Auto capture: 3 of 4"));
    harness.schedule.setBusy(false);
    harness.clock.advance(1000);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 2);
    QVERIFY(harness.schedule.attemptInFlight());
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    QCOMPARE(harness.finished, 1);
    QCOMPARE(harness.summary.captured, 2);
    QCOMPARE(harness.summary.skipped, 2);
    QCOMPARE(harness.summary.failed, 0);
    QCOMPARE(harness.schedule.statusText(), QString());
    harness.clock.advance(10000);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 2);
    QCOMPARE(harness.finished, 1);
  }

  void busyAtStartStillEndsAtTheBound() {
    Harness harness;
    harness.schedule.setBusy(true);
    QCOMPARE(harness.schedule.startAuto(counted(1)), QString());
    QCOMPARE(harness.requests, 0);
    QCOMPARE(harness.finished, 1);
    QCOMPARE(harness.summary.skipped, 1);
    QCOMPARE(harness.summary.captured, 0);
    QVERIFY(!harness.timer.armed);
    QVERIFY(!harness.schedule.autoActive());
  }

  void endsByCountWithInjectedClock() {
    Harness harness(100);
    QCOMPARE(harness.schedule.startAuto(counted(3, 2500, CaptureSource::Monitor)), QString());
    QCOMPARE(harness.requests, 1);
    QCOMPARE(harness.schedule.plannedAttempts(), 3);
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    QVERIFY(harness.timer.armed);
    QCOMPARE(harness.timer.interval, 2500);
    harness.clock.advance(2499);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 1);
    harness.clock.advance(1);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 2);
    harness.schedule.finishAttempt(AttemptDisposition::Failed);
    harness.clock.advance(2500);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 3);
    QCOMPARE(harness.finished, 0);
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    QCOMPARE(harness.finished, 1);
    QCOMPARE(harness.summary.captured, 2);
    QCOMPARE(harness.summary.failed, 1);
    QCOMPARE(harness.summary.skipped, 0);
    QVERIFY(!harness.summary.stoppedByUser);
    QCOMPARE(autoCaptureFinishedTitle(harness.summary), QStringLiteral("Auto capture finished"));
    QCOMPARE(autoCaptureSummaryText(harness.summary), QStringLiteral("Captured 2, skipped 0, failed 1."));
    QVERIFY(!harness.schedule.autoActive());
  }

  void endsByTimeWithInjectedClock() {
    Harness harness(10000);
    const qint64 start = harness.clock.nowMs();
    QCOMPARE(harness.schedule.startAuto({CaptureSource::ActiveWindow, 1000, TimeBound{start + 2500}}), QString());
    QCOMPARE(harness.schedule.plannedAttempts(), 3);
    QCOMPARE(harness.schedule.statusText(), QStringLiteral("Auto capture: 1 of 3"));
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    harness.clock.advance(1000);
    harness.schedule.onTimer();
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    harness.clock.advance(1000);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 3);
    harness.schedule.finishAttempt(AttemptDisposition::Failed);
    QCOMPARE(harness.finished, 1);
    QCOMPARE(harness.summary.captured, 2);
    QCOMPARE(harness.summary.failed, 1);
    QVERIFY(!harness.timer.armed);

    Harness late(start);
    QCOMPARE(late.schedule.startAuto({CaptureSource::FullScreen, 1000, TimeBound{start + 1500}}), QString());
    late.schedule.finishAttempt(AttemptDisposition::Captured);
    late.clock.advance(5000);
    late.schedule.onTimer();
    QCOMPARE(late.requests, 1);
    QCOMPARE(late.finished, 1);
    QCOMPARE(late.summary.captured, 1);
    QCOMPARE(late.summary.skipped, 0);
  }

  void userStopWaitsForTheInFlightAttempt() {
    Harness harness;
    QCOMPARE(harness.schedule.startAuto(counted(5)), QString());
    harness.schedule.stopAuto();
    QVERIFY(harness.schedule.autoActive());
    QVERIFY(!harness.timer.armed);
    QCOMPARE(harness.finished, 0);
    harness.schedule.finishAttempt(AttemptDisposition::Captured);
    QCOMPARE(harness.finished, 1);
    QVERIFY(harness.summary.stoppedByUser);
    QCOMPARE(harness.summary.captured, 1);
    QCOMPARE(autoCaptureFinishedTitle(harness.summary), QStringLiteral("Auto capture stopped"));
    harness.clock.advance(10000);
    harness.schedule.onTimer();
    QCOMPARE(harness.requests, 1);
    QCOMPARE(harness.finished, 1);
  }

  void delayCountdownCancelDoesNotCapture() {
    Harness harness(1000);
    QCOMPARE(harness.schedule.startDelay({CaptureSource::Region, 3}), QString());
    QCOMPARE(harness.schedule.delayRemainingSeconds(), 3);
    QCOMPARE(harness.schedule.statusText(), QStringLiteral("Capturing in 3 s"));
    QVERIFY(harness.timer.armed);
    harness.clock.advance(2000);
    harness.schedule.onTimer();
    QCOMPARE(harness.schedule.delayRemainingSeconds(), 1);
    QCOMPARE(harness.elapsed, 0);
    harness.schedule.cancelDelay();
    QCOMPARE(harness.cancelled, 1);
    QCOMPARE(harness.elapsed, 0);
    QVERIFY(!harness.schedule.delayActive());
    QVERIFY(!harness.timer.armed);
    QCOMPARE(harness.schedule.statusText(), QString());
    harness.clock.advance(10000);
    harness.schedule.onTimer();
    QCOMPARE(harness.elapsed, 0);

    Harness fired;
    QCOMPARE(fired.schedule.startDelay({CaptureSource::FullScreen, 2}), QString());
    fired.clock.advance(2000);
    fired.schedule.onTimer();
    QCOMPARE(fired.elapsed, 1);
    QVERIFY(!fired.schedule.delayActive());
    QCOMPARE(fired.schedule.startAuto(counted(1)), QString());
    QCOMPARE(fired.schedule.startDelay({CaptureSource::Monitor, 1}), QStringLiteral("schedule-busy"));
  }
};

QTEST_GUILESS_MAIN(CaptureScheduleTest)
#include "CaptureScheduleTest.moc"
