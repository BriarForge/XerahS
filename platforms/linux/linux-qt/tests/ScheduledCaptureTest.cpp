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
