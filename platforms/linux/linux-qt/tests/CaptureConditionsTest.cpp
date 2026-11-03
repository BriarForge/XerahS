// CORE-016: automatic capture pauses while the screen is locked and refuses/stops below the storage floor.
#include "ScheduledCapture.h"
#include "ScreenLockWatcher.h"

#include <QAction>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDialog>
#include <QRadioButton>
#include <QSignalSpy>
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
  AutoCondition condition = AutoCondition::Clear;
  int starts = 0;
  QList<Note> notes;
  ScheduledCapture schedule;
  Fixture() : schedule(ports(), clock, timer) {}
  ScheduledCapturePorts ports() {
    return {
      [] { return false; },
      [this](CaptureSource) { ++starts; return QString(); },
      [this](const QString &title, const QString &body, bool ok, bool raise) { notes.append({title, body, ok, raise}); },
      [](const QString &) {},
      [this] { return condition; },
    };
  }
  void tick() { clock.advance(1000); schedule.onTimer(); }
  void finishAttempt(bool ok) { schedule.captureBusyChanged(true); schedule.captureFinished(ok); schedule.captureBusyChanged(false); }
};

void acceptAuto(int count) {
  auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
  if (!active) return;
  active->findChild<QRadioButton *>(QStringLiteral("autoCaptureCountBound"))->setChecked(true);
  active->findChild<QSpinBox *>(QStringLiteral("autoCaptureCount"))->setValue(count);
  active->findChild<QSpinBox *>(QStringLiteral("autoCaptureInterval"))->setValue(1);
  active->accept();
}

}  // namespace

class CaptureConditionsTest : public QObject {
  Q_OBJECT
private slots:
  void evaluationPrefersStorageOverLockAndTreatsUnknownAsClear() {
    ConditionSources sources;
    QCOMPARE(evaluate(sources), AutoCondition::Clear);
    sources.screenLocked = [] { return true; };
    QCOMPARE(evaluate(sources), AutoCondition::ScreenLocked);
    sources.freeBytes = [] { return qint64(-1); };
    QCOMPARE(evaluate(sources), AutoCondition::ScreenLocked);
    sources.freeBytes = [] { return kMinFreeStorageBytes - 1; };
    QCOMPARE(evaluate(sources), AutoCondition::StorageLow);
    sources.freeBytes = [] { return kMinFreeStorageBytes; };
    QCOMPARE(evaluate(sources), AutoCondition::ScreenLocked);
    sources.screenLocked = [] { return false; };
    QCOMPARE(evaluate(sources), AutoCondition::Clear);
    QCOMPARE(conditionCode(AutoCondition::ScreenLocked), QStringLiteral("screen-locked"));
    QCOMPARE(conditionCode(AutoCondition::StorageLow), QStringLiteral("storage-low"));
    QVERIFY(conditionCode(AutoCondition::Clear).isEmpty());
  }

  void lockedScreenPausesTheRunAndItResumesAfterUnlock() {
    Fixture fixture;
    fixture.condition = AutoCondition::ScreenLocked;
    QTimer::singleShot(0, [] { acceptAuto(3); });
    fixture.schedule.autoAction()->trigger();
    QCOMPARE(fixture.starts, 0);
    QVERIFY(fixture.schedule.stopAction()->isEnabled());
    fixture.tick();
    QCOMPARE(fixture.starts, 0);
    fixture.condition = AutoCondition::Clear;
    fixture.tick();
    QCOMPARE(fixture.starts, 1);
    fixture.finishAttempt(true);
    QVERIFY(fixture.notes.size() == 1);
    QCOMPARE(fixture.notes.first().body, QStringLiteral("Captured 1, skipped 2, failed 0."));
    QVERIFY(fixture.notes.first().ok);
  }

  void lowStorageRefusesToStartAndStopsARunningCapture() {
    Fixture refused;
    refused.condition = AutoCondition::StorageLow;
    refused.schedule.autoAction()->trigger();
    QVERIFY(!QApplication::activeModalWidget());
    QCOMPARE(refused.starts, 0);
    QCOMPARE(refused.notes.size(), 1);
    QVERIFY(refused.notes.first().body.startsWith(QStringLiteral("storage-low: ")));
    QVERIFY(!refused.notes.first().ok);
    QVERIFY(!refused.schedule.stopAction()->isEnabled());

    Fixture running;
    QTimer::singleShot(0, [] { acceptAuto(5); });
    running.schedule.autoAction()->trigger();
    QCOMPARE(running.starts, 1);
    running.finishAttempt(true);
    running.condition = AutoCondition::StorageLow;
    running.tick();
    QCOMPARE(running.starts, 1);
    QVERIFY(!running.schedule.stopAction()->isEnabled());
    QVERIFY(!running.timer.armed);
    QCOMPARE(running.notes.first().title, QStringLiteral("Auto capture stopped"));
    QVERIFY(running.notes.first().body.startsWith(QStringLiteral("storage-low: ")));
    running.condition = AutoCondition::Clear;
    running.tick();
    QCOMPARE(running.starts, 1);
  }

  void desktopSignalsDriveTheLockState() {
    auto sender = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("lock-sender"));
    auto receiver = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("lock-receiver"));
    QVERIFY(sender.isConnected() && receiver.isConnected());
    QVERIFY(sender.registerService(QStringLiteral("org.freedesktop.login1")));
    ScreenLockState state;
    ScreenLockWatcher watcher(state, receiver, receiver);
    QVERIFY(watcher.connected());
    QSignalSpy changes(&state, &ScreenLockState::lockedChanged);
    const auto emitSignal = [&](const QString &path, const QString &interface, const QString &name, const QVariantList &args = {}) {
      auto message = QDBusMessage::createSignal(path, interface, name);
      message.setArguments(args);
      QVERIFY(sender.send(message));
    };
    const QString logind = QStringLiteral("org.freedesktop.login1.Session"), logindPath = QStringLiteral("/org/freedesktop/login1/session/_1");
    emitSignal(logindPath, logind, QStringLiteral("Lock"));
    QTRY_VERIFY(state.locked());
    emitSignal(logindPath, logind, QStringLiteral("Unlock"));
    QTRY_VERIFY(!state.locked());
    emitSignal(QStringLiteral("/org/freedesktop/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"), {true});
    QTRY_VERIFY(state.locked());
    emitSignal(QStringLiteral("/org/gnome/ScreenSaver"), QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("ActiveChanged"), {false});
    QTRY_VERIFY(!state.locked());
    QCOMPARE(changes.count(), 4);
    QDBusConnection::disconnectFromBus(QStringLiteral("lock-sender"));
    QDBusConnection::disconnectFromBus(QStringLiteral("lock-receiver"));
  }
};

QTEST_MAIN(CaptureConditionsTest)
#include "CaptureConditionsTest.moc"
