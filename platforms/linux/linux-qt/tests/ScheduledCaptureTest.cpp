// CORE-016: scheduled capture reports skipped, failed and captured attempts through its ports.
#include "ScheduledCapture.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <memory>

using namespace xerahs::app;
using namespace xerahs::capture;

namespace {

class ManualTimer final : public Timer {
public:
  void arm(int intervalMs) override { armed = intervalMs > 0; interval = intervalMs; }
  bool armed = false;
  int interval = 0;
};

struct Note { QString title; QString body; bool ok = false; bool raise = false; };

struct Fixture {
  ManualClock clock{1'000'000};
  ManualTimer timer;
  bool blocked = false;
  QString startCode;
  int starts = 0;
  int cancels = 0;
  AutoCondition condition = AutoCondition::Clear;
  QList<Note> notes;
  QStringList tips;
  ScheduledCapture schedule;
  Fixture() : schedule(ports(), clock, timer) {}
  ScheduledCapturePorts ports() {
    return {
      [this] { return blocked; },
      [this](CaptureSource) { ++starts; return startCode; },
      [this](const QString &title, const QString &body, bool ok, bool raise) { notes.append({title, body, ok, raise}); },
      [this](const QString &tooltip) { tips.append(tooltip); },
      [this] { return condition; },
      [this] { ++cancels; },
    };
  }
};

void acceptAuto(int count) {
  auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
  if (!active) return;
  active->findChild<QRadioButton *>(QStringLiteral("autoCaptureCountBound"))->setChecked(true);
  active->findChild<QSpinBox *>(QStringLiteral("autoCaptureCount"))->setValue(count);
  active->findChild<QSpinBox *>(QStringLiteral("autoCaptureInterval"))->setValue(1);
  active->accept();
}

QWidget *countdown() {
  for (auto *widget : QApplication::topLevelWidgets())
    if (widget->objectName() == QLatin1String("captureCountdown") && widget->isVisible()) return widget;
  return nullptr;
}

}  // namespace

class ScheduledCaptureTest : public QObject {
  Q_OBJECT
private slots:
  void lockCancellationCanDestroyTheScheduleWithoutPublishingALateNotice() {
    ManualClock clock{1000};
    ManualTimer timer;
    int notices = 0;
    std::unique_ptr<ScheduledCapture> schedule;
    ScheduledCapturePorts ports;
    ports.start = [](CaptureSource) { return QString(); };
    ports.notify = [&](const QString &, const QString &, bool, bool) { ++notices; };
    ports.cancelOwnedCapture = [&] { schedule.reset(); };
    schedule = std::make_unique<ScheduledCapture>(ports, clock, timer);
    QTimer::singleShot(0, [] { acceptAuto(3); });
    schedule->autoAction()->trigger();
    schedule->captureBusyChanged(true);
    schedule->screenLockedChanged(true);
    QVERIFY(!schedule);
    QCOMPARE(notices, 0);
  }

  void blockedTickIsSkippedWithoutStarting() {
    Fixture fixture;
    fixture.blocked = true;
    QCOMPARE(fixture.tips.first(), QStringLiteral("XerahS"));
    QTimer::singleShot(0, [] { acceptAuto(1); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 0);
    QCOMPARE(fixture.notes.size(), 1);
    QCOMPARE(fixture.notes.first().title, QStringLiteral("Auto capture finished"));
    QCOMPARE(fixture.notes.first().body, QStringLiteral("Captured 0, skipped 1, failed 0."));
    QVERIFY(fixture.notes.first().ok);
    QCOMPARE(fixture.tips.last(), QStringLiteral("XerahS"));
  }

  void busyAndOtherStartResults() {
    Fixture busy;
    busy.startCode = QStringLiteral("busy");
    QTimer::singleShot(0, [] { acceptAuto(1); });
    busy.schedule.autoAction()->trigger();
    QCOMPARE(busy.starts, 1);
    QCOMPARE(busy.notes.first().body, QStringLiteral("Captured 0, skipped 1, failed 0."));

    Fixture failed;
    failed.startCode = QStringLiteral("capture-not-started");
    QTimer::singleShot(0, [] { acceptAuto(1); });
    failed.schedule.autoAction()->trigger();
    QCOMPARE(failed.starts, 1);
    QCOMPARE(failed.notes.first().title, QStringLiteral("Auto capture finished"));
    QCOMPARE(failed.notes.first().body, QStringLiteral("Captured 0, skipped 0, failed 1."));
    QVERIFY(!failed.notes.first().ok);
  }

  void busyEdgeRecordsCapturedOrFailed() {
    Fixture captured;
    QTimer::singleShot(0, [] { acceptAuto(1); });
    captured.schedule.autoAction()->trigger();
    QCOMPARE(captured.notes.size(), 0);
    QVERIFY(captured.schedule.stopAction()->isEnabled());
    QVERIFY(captured.tips.contains(QStringLiteral("Auto capture: 1 of 1")));
    captured.schedule.captureBusyChanged(true);
    captured.schedule.captureFinished(true);
    captured.schedule.captureBusyChanged(false);
    QCOMPARE(captured.notes.first().body, QStringLiteral("Captured 1, skipped 0, failed 0."));
    QVERIFY(captured.notes.first().ok);
    QVERIFY(!captured.schedule.stopAction()->isEnabled());
    QCOMPARE(captured.tips.last(), QStringLiteral("XerahS"));

    Fixture failed;
    QTimer::singleShot(0, [] { acceptAuto(1); });
    failed.schedule.autoAction()->trigger();
    failed.schedule.captureBusyChanged(true);
    failed.schedule.captureFinished(false);
    failed.schedule.captureBusyChanged(false);
    QCOMPARE(failed.notes.first().body, QStringLiteral("Captured 0, skipped 0, failed 1."));
    QVERIFY(!failed.notes.first().ok);
  }

  void permissionDenialStopsFutureTicksAndRequiresAnExplicitNewSchedule() {
    Fixture fixture;
    QTimer::singleShot(0, [] { acceptAuto(5); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 1);
    fixture.schedule.captureBusyChanged(true);
    fixture.schedule.captureFinished(false);
    fixture.schedule.capturePermissionDenied();
    QVERIFY(!fixture.timer.armed);
    QCOMPARE(fixture.notes.size(), 1);
    QVERIFY(fixture.notes.first().body.startsWith(QStringLiteral("capture-permission-denied:")));
    QVERIFY(!fixture.notes.first().ok);
    fixture.schedule.captureBusyChanged(false);
    QVERIFY(!fixture.schedule.stopAction()->isEnabled());
    QCOMPARE(fixture.notes.size(), 2);
    QCOMPARE(fixture.notes.last().body, QStringLiteral("Captured 0, skipped 0, failed 1."));
    QVERIFY(!fixture.notes.last().ok);
    fixture.clock.advance(100000);
    fixture.schedule.onTimer();
    QCOMPARE(fixture.starts, 1);
    QCOMPARE(fixture.tips.last(), QStringLiteral("XerahS"));

    QTimer::singleShot(0, [] { acceptAuto(1); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 2);
    fixture.schedule.captureBusyChanged(true);
    fixture.schedule.captureFinished(true);
    fixture.schedule.captureBusyChanged(false);
    QCOMPARE(fixture.notes.last().body, QStringLiteral("Captured 1, skipped 0, failed 0."));
    QVERIFY(fixture.notes.last().ok);
  }

  void unrelatedPermissionDenialDoesNotChangeAnIdleOrWaitingSchedule() {
    Fixture fixture;
    fixture.schedule.capturePermissionDenied();
    QVERIFY(fixture.notes.isEmpty());
    fixture.blocked = true;
    QTimer::singleShot(0, [] { acceptAuto(3); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 0);
    QVERIFY(fixture.schedule.stopAction()->isEnabled());
    fixture.schedule.capturePermissionDenied();
    QVERIFY(fixture.notes.isEmpty());
    QVERIFY(fixture.schedule.stopAction()->isEnabled());
    fixture.blocked = false;
    fixture.clock.advance(1000);
    fixture.schedule.onTimer();
    QCOMPARE(fixture.starts, 1);
    fixture.schedule.captureBusyChanged(true);
    fixture.schedule.captureFinished(true);
    fixture.schedule.captureBusyChanged(false);
    fixture.schedule.stopAction()->trigger();
    QVERIFY(!fixture.schedule.stopAction()->isEnabled());
    QCOMPARE(fixture.notes.last().body, QStringLiteral("Captured 1, skipped 1, failed 0."));
  }

  void lockAfterAdmissionCancelsOnlyTheOwnedAttemptAndPreventsLateSuccess() {
    Fixture fixture;
    QTimer::singleShot(0, [] { acceptAuto(5); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 1);
    fixture.schedule.captureBusyChanged(true);
    fixture.schedule.screenLockedChanged(true);
    QCOMPARE(fixture.cancels, 1);
    QVERIFY(!fixture.timer.armed);
    QVERIFY(fixture.notes.first().body.startsWith(QStringLiteral("screen-locked:")));
    fixture.schedule.captureFinished(true);  // Completion racing with cancellation cannot undo revocation.
    fixture.schedule.captureBusyChanged(false);
    QCOMPARE(fixture.notes.last().body, QStringLiteral("Captured 0, skipped 0, failed 1."));
    QVERIFY(!fixture.notes.last().ok);
    fixture.schedule.screenLockedChanged(false);
    fixture.clock.advance(100000);
    fixture.schedule.onTimer();
    QCOMPARE(fixture.starts, 1);
    QVERIFY(!fixture.schedule.stopAction()->isEnabled());
  }

  void lockNeverCancelsUnrelatedManualWorkOrRestartsAWaitingRun() {
    Fixture fixture;
    fixture.schedule.captureBusyChanged(true);
    fixture.schedule.screenLockedChanged(true);
    QCOMPARE(fixture.cancels, 0);
    QVERIFY(fixture.notes.isEmpty());
    fixture.schedule.captureBusyChanged(false);
    fixture.condition = AutoCondition::ScreenLocked;
    QTimer::singleShot(0, [] { acceptAuto(3); });
    fixture.schedule.autoAction()->trigger();
    fixture.schedule.screenLockedChanged(true);
    QCOMPARE(fixture.starts, 0);
    QCOMPARE(fixture.cancels, 0);
    QVERIFY(fixture.timer.armed);
    fixture.condition = AutoCondition::Clear;
    fixture.schedule.screenLockedChanged(false);
    fixture.clock.advance(1000);
    fixture.schedule.onTimer();
    QCOMPARE(fixture.starts, 1);
    fixture.schedule.stopAction()->trigger();
    fixture.schedule.captureBusyChanged(false);
  }

  void storageStopStaysARefusalAndDoesNotAnnounceSuccess_data() {
    QTest::addColumn<int>("limit");
    QTest::newRow("final-attempt") << 1;
    QTest::newRow("more-attempts-remaining") << 3;
  }
  void storageStopStaysARefusalAndDoesNotAnnounceSuccess() {
    QFETCH(int, limit);
    Fixture duringDialog;
    QTimer::singleShot(0, [&duringDialog, limit] {
      duringDialog.condition = AutoCondition::StorageLow;
      acceptAuto(limit);
    });
    duringDialog.schedule.autoAction()->trigger();
    QCOMPARE(duringDialog.starts, 0);
    QVERIFY(!duringDialog.schedule.stopAction()->isEnabled());
    QVERIFY(!duringDialog.timer.armed);
    QCOMPARE(duringDialog.notes.size(), 1);
    QCOMPARE(duringDialog.notes.first().title, QStringLiteral("Auto capture stopped"));
    QVERIFY(duringDialog.notes.first().body.startsWith(QStringLiteral("storage-low: ")));
    QVERIFY(!duringDialog.notes.first().ok);
    QVERIFY(duringDialog.notes.first().raise);
    duringDialog.condition = AutoCondition::Clear;
    duringDialog.clock.advance(10000);
    duringDialog.schedule.onTimer();
    QCOMPARE(duringDialog.starts, 0);
    QCOMPARE(duringDialog.notes.size(), 1);

    Fixture running;
    QTimer::singleShot(0, [limit] { acceptAuto(limit + 1); });
    running.schedule.autoAction()->trigger();
    QCOMPARE(running.starts, 1);
    running.schedule.captureBusyChanged(true);
    running.schedule.captureFinished(true);
    running.schedule.captureBusyChanged(false);
    QCOMPARE(running.notes.size(), 0);
    running.condition = AutoCondition::StorageLow;
    running.clock.advance(1000);
    running.schedule.onTimer();
    QCOMPARE(running.starts, 1);
    QVERIFY(!running.schedule.stopAction()->isEnabled());
    QVERIFY(!running.timer.armed);
    QCOMPARE(running.notes.size(), 1);
    QVERIFY(running.notes.first().body.startsWith(QStringLiteral("storage-low: ")));
    QVERIFY(!running.notes.first().ok);
    running.condition = AutoCondition::Clear;
    running.clock.advance(10000);
    running.schedule.onTimer();
    QCOMPARE(running.starts, 1);
    QCOMPARE(running.notes.size(), 1);
    QCOMPARE(running.tips.last(), QStringLiteral("XerahS"));
  }

  void unresolvedWorkflowRefusesAndStopsWithoutStartingAnotherCapture() {
    Fixture refused;
    refused.condition = AutoCondition::WorkflowUnavailable;
    refused.schedule.autoAction()->trigger();
    QCOMPARE(refused.starts, 0);
    QVERIFY(!QApplication::activeModalWidget());
    QVERIFY(refused.notes.first().body.startsWith(QStringLiteral("workflow-unavailable:")));

    Fixture running;
    QTimer::singleShot(0, [] { acceptAuto(5); });
    running.schedule.autoAction()->trigger();
    running.schedule.captureBusyChanged(true);
    running.schedule.captureFinished(true);
    running.schedule.captureBusyChanged(false);
    running.condition = AutoCondition::WorkflowUnavailable;
    running.clock.advance(1000);
    running.schedule.onTimer();
    QCOMPARE(running.starts, 1);
    QVERIFY(!running.schedule.stopAction()->isEnabled());
    QVERIFY(!running.timer.armed);
    QVERIFY(running.notes.last().body.startsWith(QStringLiteral("workflow-unavailable:")));
  }

  void delayAndAutoRefuseEachOtherAndTheTooltipFollowsTheRun() {
    Fixture fixture;
    QCOMPARE(fixture.schedule.delayAction()->text(), QStringLiteral("Capture after delay…"));
    QCOMPARE(fixture.schedule.autoAction()->text(), QStringLiteral("Auto capture…"));
    QCOMPARE(fixture.schedule.stopAction()->text(), QStringLiteral("Stop auto capture"));
    QVERIFY(!fixture.schedule.stopAction()->isEnabled());
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->setValue(9);
      active->accept();
    });
    fixture.schedule.delayAction()->trigger();
    QCOMPARE(fixture.tips.last(), QStringLiteral("Capturing in 9 s"));
    QCOMPARE(fixture.starts, 0);
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.notes.last().title, QStringLiteral("Auto capture"));
    QCOMPARE(fixture.notes.last().body, QStringLiteral("Cancel the delayed capture before starting automatic capture."));
    QVERIFY(!fixture.notes.last().ok);
    QVERIFY(!fixture.notes.last().raise);
    QVERIFY(!QApplication::activeModalWidget());

    auto *window = countdown();
    QVERIFY(window);
    window->findChild<QPushButton *>(QStringLiteral("countdownCancel"))->click();
    QCOMPARE(fixture.tips.last(), QStringLiteral("XerahS"));

    QTimer::singleShot(0, [] { acceptAuto(2); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 1);
    QCOMPARE(fixture.tips.last(), QStringLiteral("Auto capture: 1 of 2"));
    QVERIFY(fixture.schedule.stopAction()->isEnabled());
    fixture.schedule.delayAction()->trigger();
    QCOMPARE(fixture.notes.last().title, QStringLiteral("Capture after delay"));
    QCOMPARE(fixture.notes.last().body, QStringLiteral("Stop automatic capture before starting a delayed capture."));
    QVERIFY(!fixture.notes.last().ok);
    QVERIFY(fixture.notes.last().raise);
    QVERIFY(!QApplication::activeModalWidget());
  }
};

QTEST_MAIN(ScheduledCaptureTest)
#include "ScheduledCaptureTest.moc"
