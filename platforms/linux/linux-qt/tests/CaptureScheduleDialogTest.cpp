// CORE-016: delay countdown cancellation and automatic-capture dialog bounds.
#include "AutoCaptureDialog.h"
#include "DelayedCapture.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QJsonObject>
#include <QLabel>
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
  void arm(int intervalMs) override { armed = intervalMs > 0; }
  bool armed = false;
};

QWidget *countdown() {
  for (auto *widget : QApplication::topLevelWidgets())
    if (widget->objectName() == QLatin1String("captureCountdown")) return widget;
  return nullptr;
}

struct FakeStore {
  QJsonObject values = xerahs::settings::defaultSettings(QStringLiteral("/tmp"));
  QString error;
  int saves = 0;
  xerahs::settings::CapturePreferencesPort port() {
    return {[this] { return values; }, [this](const QJsonObject &next) {
      ++saves;
      if (!error.isEmpty()) return error;
      values = next;
      return QString();
    }};
  }
};

}  // namespace

class CaptureScheduleDialogTest : public QObject {
  Q_OBJECT
  ManualClock clock{1'000'000};
  ManualTimer timer;
private slots:
  void dialogRequiresABoundAndRejectsAnEndBeyondADay() {
    const qint64 now = 1'761'217'200'000;
    AutoCaptureDialog dialog([now] { return now; }, false);
    dialog.show();
    auto *start = dialog.findChild<QPushButton *>(QStringLiteral("autoCaptureStart"));
    auto *stop = dialog.findChild<QPushButton *>(QStringLiteral("autoCaptureStop"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("autoCaptureStatus"));
    auto *source = dialog.findChild<QComboBox *>(QStringLiteral("autoCaptureSource"));
    auto *interval = dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureInterval"));
    QVERIFY(start && stop && status && source && interval);
    QCOMPARE(source->count(), 3);
    QVERIFY(source->findData(QStringLiteral("region")) < 0);
    QVERIFY(!start->isEnabled());
    QVERIFY(!stop->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("bound")));
    dialog.accept();
    QVERIFY(dialog.isVisible());
    QVERIFY(!dialog.takeRequest());

    interval->setValue(0);
    QCOMPARE(interval->value(), 1);
    dialog.findChild<QRadioButton *>(QStringLiteral("autoCaptureCountBound"))->setChecked(true);
    dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureCount"))->setValue(4);
    QVERIFY(start->isEnabled());
    QCOMPARE(status->text(), QStringLiteral("Ready."));

    auto *timeBound = dialog.findChild<QRadioButton *>(QStringLiteral("autoCaptureTimeBound"));
    auto *end = dialog.findChild<QDateTimeEdit *>(QStringLiteral("autoCaptureEnd"));
    timeBound->setChecked(true);
    end->setDateTime(QDateTime::fromMSecsSinceEpoch(now + 25LL * 60 * 60 * 1000));
    QVERIFY(!start->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("24")));
    dialog.accept();
    QVERIFY(dialog.isVisible());
    end->setDateTime(QDateTime::fromMSecsSinceEpoch(now - 1000));
    QVERIFY(status->text().contains(QStringLiteral("after the current time")));
    end->setDateTime(QDateTime::fromMSecsSinceEpoch(now + 60 * 60 * 1000));
    QVERIFY(start->isEnabled());
    start->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    const auto request = dialog.takeRequest();
    QVERIFY(request);
    QCOMPARE(request->source, CaptureSource::FullScreen);
    QCOMPARE(request->intervalMs, 1000);
    QVERIFY(std::holds_alternative<TimeBound>(request->bound));
    QCOMPARE(std::get<TimeBound>(request->bound).endMs, now + 60 * 60 * 1000);

    AutoCaptureDialog running([now] { return now; }, true);
    running.show();
    QVERIFY(!running.findChild<QPushButton *>(QStringLiteral("autoCaptureStart"))->isEnabled());
    auto *runningStop = running.findChild<QPushButton *>(QStringLiteral("autoCaptureStop"));
    QVERIFY(runningStop->isEnabled());
    QVERIFY(running.findChild<QLabel *>(QStringLiteral("autoCaptureStatus"))->text().contains(QStringLiteral("running")));
    runningStop->click();
    QCOMPARE(running.result(), AutoCaptureDialog::StopResult);

    AutoCaptureDialog escape([now] { return now; }, false);
    QTimer::singleShot(0, [] {
      auto *active = QApplication::activeModalWidget();
      QVERIFY(active);
      QTest::keyClick(active, Qt::Key_Escape);
    });
    QCOMPARE(escape.exec(), int(QDialog::Rejected));
    QVERIFY(!escape.takeRequest());
  }

  void rememberedDelayAndCountdownCancel() {
    int captures = 0;
    DelayedCapture delay(clock, timer);
    QObject::connect(&delay, &DelayedCapture::captureRequested, &delay, [&](CaptureSource source) {
      ++captures;
      QCOMPARE(source, CaptureSource::Region);
    });
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(active);
      QCOMPARE(active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->value(), 3);
      active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->setValue(9);
      active->findChild<QComboBox *>(QStringLiteral("delaySource"))->setCurrentIndex(3);
      active->accept();
    });
    QVERIFY(delay.prompt(nullptr));
    QCOMPARE(delay.rememberedSeconds(), 9);
    QCOMPARE(delay.rememberedSource(), CaptureSource::Region);
    auto *window = countdown();
    QVERIFY(window);
    QVERIFY(window->isVisible());
    QVERIFY(window->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    QCOMPARE(window->findChild<QLabel *>(QStringLiteral("countdownSeconds"))->text(), QStringLiteral("9"));
    QCOMPARE(delay.statusText(), QStringLiteral("Capturing in 9 s"));
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!delay.delayActive());
    QCOMPARE(delay.statusText(), QString());
    clock.advance(20000);
    delay.onTimer();
    QCoreApplication::processEvents();
    QCOMPARE(captures, 0);

    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QCOMPARE(active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->value(), 9);
      QCOMPARE(active->findChild<QComboBox *>(QStringLiteral("delaySource"))->currentData().toString(), QStringLiteral("region"));
      active->reject();
    });
    QVERIFY(!delay.prompt(nullptr));
    QCOMPARE(delay.rememberedSeconds(), 9);
    QVERIFY(!delay.delayActive());

    DelayedCapture fresh(clock, timer);
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QCOMPARE(active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->value(), 3);
      QCOMPARE(active->findChild<QComboBox *>(QStringLiteral("delaySource"))->currentData().toString(), QStringLiteral("full-screen"));
      active->reject();
    });
    QVERIFY(!fresh.prompt(nullptr));
    QCOMPARE(fresh.rememberedSeconds(), 3);
    QCOMPARE(fresh.rememberedSource(), CaptureSource::FullScreen);
  }

  void delayPromptReadsAndSavesTheStoredChoice() {
    FakeStore store;
    store.values.insert(QStringLiteral("delayedCapture.seconds"), 12);
    store.values.insert(QStringLiteral("delayedCapture.source"), QStringLiteral("active-window"));
    DelayedCapture delay(clock, timer);
    delay.setPreferences(store.port());
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(active);
      QCOMPARE(active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->value(), 12);
      QCOMPARE(active->findChild<QComboBox *>(QStringLiteral("delaySource"))->currentData().toString(), QStringLiteral("active-window"));
      active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->setValue(20);
      active->findChild<QComboBox *>(QStringLiteral("delaySource"))->setCurrentIndex(1);
      active->reject();
    });
    QVERIFY(!delay.prompt(nullptr));
    QCOMPARE(store.saves, 0);
    QCOMPARE(store.values.value(QStringLiteral("delayedCapture.seconds")).toInt(), 12);

    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->setValue(20);
      active->findChild<QComboBox *>(QStringLiteral("delaySource"))->setCurrentIndex(1);
      active->accept();
    });
    QVERIFY(delay.prompt(nullptr));
    QCOMPARE(store.saves, 1);
    QCOMPARE(store.values.value(QStringLiteral("delayedCapture.seconds")).toInt(), 20);
    QCOMPARE(store.values.value(QStringLiteral("delayedCapture.source")).toString(), QStringLiteral("monitor"));
    delay.cancel();

    // A new session starts from the store.
    DelayedCapture next(clock, timer);
    next.setPreferences(store.port());
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QCOMPARE(active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->value(), 20);
      active->reject();
    });
    QVERIFY(!next.prompt(nullptr));
  }

  void delaySaveFailureIsShownAndTheCaptureStillStarts() {
    FakeStore store;
    store.error = QStringLiteral("settings-write-failed");
    DelayedCapture delay(clock, timer);
    delay.setPreferences(store.port());
    QStringList failures;
    QObject::connect(&delay, &DelayedCapture::preferencesSaveFailed, &delay, [&](const QString &code) { failures << code; });
    QString shown;
    QTimer::singleShot(0, [&] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->setValue(7);
      active->accept();
      shown = active->findChild<QLabel *>(QStringLiteral("delayStatus"))->text();
    });
    QVERIFY(delay.prompt(nullptr));
    QVERIFY(delay.delayActive());
    QVERIFY(shown.contains(QStringLiteral("settings-write-failed")));
    QCOMPARE(failures, QStringList{QStringLiteral("settings-write-failed")});
    delay.cancel();
    // The unsaved choice still wins for the rest of the session.
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QCOMPARE(active->findChild<QSpinBox *>(QStringLiteral("delaySeconds"))->value(), 7);
      active->reject();
    });
    QVERIFY(!delay.prompt(nullptr));
  }

  void autoDialogReadsAndSavesTheStoredChoice() {
    const qint64 now = 1'761'217'200'000;
    FakeStore store;
    store.values.insert(QStringLiteral("autoCapture.source"), QStringLiteral("monitor"));
    store.values.insert(QStringLiteral("autoCapture.intervalMs"), 7000);
    store.values.insert(QStringLiteral("autoCapture.boundKind"), QStringLiteral("time"));
    store.values.insert(QStringLiteral("autoCapture.durationMinutes"), 30);
    store.values.insert(QStringLiteral("autoCapture.count"), 25);
    {
      AutoCaptureDialog dialog([now] { return now; }, false, store.port());
      QCOMPARE(dialog.findChild<QComboBox *>(QStringLiteral("autoCaptureSource"))->currentData().toString(), QStringLiteral("monitor"));
      QCOMPARE(dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureInterval"))->value(), 7);
      QCOMPARE(dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureCount"))->value(), 25);
      QVERIFY(dialog.findChild<QRadioButton *>(QStringLiteral("autoCaptureTimeBound"))->isChecked());
      QCOMPARE(dialog.findChild<QDateTimeEdit *>(QStringLiteral("autoCaptureEnd"))->dateTime().toMSecsSinceEpoch(), now + 30 * 60 * 1000);
      dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureInterval"))->setValue(9);
      dialog.reject();
      QCOMPARE(store.saves, 0);
    }
    {
      AutoCaptureDialog dialog([now] { return now; }, false, store.port());
      dialog.findChild<QDateTimeEdit *>(QStringLiteral("autoCaptureEnd"))->setDateTime(QDateTime::fromMSecsSinceEpoch(now + 90 * 60 * 1000));
      dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureInterval"))->setValue(9);
      dialog.findChild<QPushButton *>(QStringLiteral("autoCaptureStart"))->click();
      QVERIFY(dialog.takeRequest());
      QCOMPARE(store.saves, 1);
    }
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.boundKind")).toString(), QStringLiteral("time"));
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.durationMinutes")).toInt(), 90);
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.intervalMs")).toInt(), 9000);
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.count")).toInt(), 25);
    QVERIFY(!store.values.contains(QStringLiteral("autoCapture.endMs")));
    {
      AutoCaptureDialog dialog([now] { return now; }, false, store.port());
      dialog.findChild<QRadioButton *>(QStringLiteral("autoCaptureCountBound"))->setChecked(true);
      dialog.findChild<QSpinBox *>(QStringLiteral("autoCaptureCount"))->setValue(3);
      dialog.findChild<QPushButton *>(QStringLiteral("autoCaptureStart"))->click();
    }
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.boundKind")).toString(), QStringLiteral("count"));
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.count")).toInt(), 3);
    QCOMPARE(store.values.value(QStringLiteral("autoCapture.durationMinutes")).toInt(), 90);
  }

  void autoSaveFailureIsShownAndTheCaptureStillStarts() {
    const qint64 now = 1'761'217'200'000;
    FakeStore store;
    store.error = QStringLiteral("settings-busy");
    AutoCaptureDialog dialog([now] { return now; }, false, store.port());
    dialog.findChild<QPushButton *>(QStringLiteral("autoCaptureStart"))->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(dialog.takeRequest());
    QCOMPARE(dialog.saveError(), QStringLiteral("settings-busy"));
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("autoCaptureStatus"))->text().contains(QStringLiteral("settings-busy")));

    AutoCaptureSession session(clock, timer);
    session.setPreferences(store.port());
    QStringList failures;
    QObject::connect(&session, &AutoCaptureSession::preferencesSaveFailed, &session, [&](const QString &code) { failures << code; });
    QTimer::singleShot(0, [] {
      auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      active->findChild<QPushButton *>(QStringLiteral("autoCaptureStart"))->click();
    });
    QVERIFY(session.prompt(nullptr));
    QVERIFY(session.active());
    QCOMPARE(failures, QStringList{QStringLiteral("settings-busy")});
    session.stop();
  }

  void cancelButtonAndLateCancelSkipTheCapture() {
    int captures = 0;
    DelayedCapture delay(clock, timer);
    QObject::connect(&delay, &DelayedCapture::captureRequested, &delay, [&] { ++captures; });
    QCOMPARE(delay.start({CaptureSource::Monitor, 4}), QString());
    auto *window = countdown();
    QVERIFY(window);
    window->findChild<QPushButton *>(QStringLiteral("countdownCancel"))->click();
    QVERIFY(!delay.delayActive());
    QVERIFY(!window->isVisible());

    QCOMPARE(delay.start({CaptureSource::FullScreen, 1}), QString());
    window = countdown();
    QCOMPARE(window->findChild<QLabel *>(QStringLiteral("countdownSeconds"))->text(), QStringLiteral("1"));
    clock.advance(1000);
    delay.onTimer();
    delay.cancel();
    QCoreApplication::processEvents();
    QCOMPARE(captures, 0);
    QVERIFY(!delay.delayActive());

    QCOMPARE(delay.start({CaptureSource::ActiveWindow, 1}), QString());
    clock.advance(1000);
    delay.onTimer();
    QCoreApplication::processEvents();
    QCOMPARE(captures, 1);
    QVERIFY(!countdown()->isVisible());
  }
};

QTEST_MAIN(CaptureScheduleDialogTest)
#include "CaptureScheduleDialogTest.moc"
