// CORE-016: delay countdown cancellation and automatic-capture dialog bounds.
#include "AutoCaptureDialog.h"
#include "DelayedCapture.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
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
