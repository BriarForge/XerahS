// Real Qt actions/keyboard/accessibility; no capture, upload or settings I/O.
#include "ApplicationWindow.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
using namespace xerahs::app;
class ApplicationWindowTest final : public QObject {
  Q_OBJECT
private slots:
  void preparingProgressIsAccessibleAndCancellationDoesNotReplacePublishedResults() {
    ApplicationWindow window({}); window.activate();
    auto *progress = window.findChild<QProgressBar *>("operationProgress");
    auto *status = window.findChild<QLabel *>("operationStatus"); QVERIFY(progress); QVERIFY(status);
    QVERIFY(progress->isHidden()); window.showProgress("Region capture", 50);
    QVERIFY(!progress->isHidden()); QCOMPARE(progress->value(), 50);
    auto *accessible = QAccessible::queryAccessibleInterface(progress); QVERIFY(accessible);
    QVERIFY(accessible->valueInterface()); QCOMPARE(accessible->valueInterface()->currentValue().toInt(), 50);
    window.cancelPendingProgress(); QVERIFY(progress->isHidden());
    QCOMPARE(status->text(), QString("Region capture: Cancelled"));
    window.showProgress("Region capture", 100); window.showResult("Capture", "Saved", true);
    window.cancelPendingProgress(); QVERIFY(progress->isHidden()); QCOMPARE(status->text(), QString("Capture: Saved"));
    window.showProgress("Region capture", 10); window.showResult("Capture", "Failed", false);
    window.cancelPendingProgress(); QCOMPARE(status->text(), QString("Capture: Failed"));
    window.showActivity("Capture actions", "Saving capture"); QVERIFY(!progress->isHidden());
    QCOMPARE(progress->minimum(), 0); QCOMPARE(progress->maximum(), 0);
    QCOMPARE(status->text(), QString("Capture actions: Saving capture"));
    window.showProgress("Region capture", 50); QCOMPARE(progress->maximum(), 100); QCOMPARE(progress->value(), 50);
  }
  void nativeKeyboardAndMenuUseTheActualSharedOperationAndAvailability() {
    QAction capture(QStringLiteral("Capture region")), cancel(QStringLiteral("Cancel capture"));
    cancel.setEnabled(false);
    ApplicationWindow window({{"capture-region", &capture}, {"cancel-capture", &cancel}});
    window.activate(); QVERIFY(window.isVisible());
    auto *start = window.findChild<QToolButton *>("capture-region");
    auto *stop = window.findChild<QToolButton *>("cancel-capture"); QVERIFY(start); QVERIFY(stop);
    QSignalSpy started(&capture, &QAction::triggered), cancelled(&cancel, &QAction::triggered);
    start->setFocus(); QTest::keyClick(start, Qt::Key_Space); QCOMPARE(started.count(), 1);
    QTest::keyClick(stop, Qt::Key_Space); QCOMPARE(cancelled.count(), 0);
    capture.setEnabled(false); capture.setText(QStringLiteral("Capture region (in progress)")); cancel.setEnabled(true);
    QVERIFY(!start->isEnabled()); QVERIFY(stop->isEnabled()); QCOMPARE(start->text(), capture.text());
    QTest::keyClick(start, Qt::Key_Space); QCOMPARE(started.count(), 1);
    stop->setFocus(); QTest::keyClick(stop, Qt::Key_Space); QCOMPARE(cancelled.count(), 1);
    auto *accessible = QAccessible::queryAccessibleInterface(stop); QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), cancel.text()); QVERIFY(!accessible->state().disabled);
    QVERIFY(accessible->actionInterface());
    accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction()); QCOMPARE(cancelled.count(), 2);
    bool found = false;
    for (auto *menuAction : window.menuBar()->actions()) if (auto *menu = menuAction->menu()) {
      if (menu->actions().contains(&cancel)) { found = true; cancel.trigger(); }
    }
    QVERIFY(found); QCOMPARE(cancelled.count(), 3);
  }
  void operationResultsStayPlainAccessibleAndAvailableAfterReactivation() {
    ApplicationWindow window({}); auto *status = window.findChild<QLabel *>("operationStatus"); QVERIFY(status);
    window.showResult("Capture", "<a href='private'>Denied</a>", false);
    QCOMPARE(status->textFormat(), Qt::PlainText); QCOMPARE(status->text(), QString("Capture: <a href='private'>Denied</a>"));
    auto *accessible = QAccessible::queryAccessibleInterface(status); QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Description), status->text());
    window.hide(); window.activate(); QVERIFY(window.isVisible());
    QCOMPARE(status->text(), QString("Capture: <a href='private'>Denied</a>"));
    window.showMaximized(); window.showMinimized(); QVERIFY(window.isMinimized());
    window.activate(); QVERIFY(!window.isMinimized()); QVERIFY(window.isMaximized());
  }
  void activationKeepsAnExistingConsentDialogModal() {
    ApplicationWindow window({}); QDialog consent(&window); consent.setModal(true); consent.show();
    QTRY_COMPARE(QApplication::activeModalWidget(), &consent);
    window.activate(); QVERIFY(window.isVisible()); QVERIFY(consent.isVisible());
    QCOMPARE(QApplication::activeModalWidget(), &consent);
  }
};
QTEST_MAIN(ApplicationWindowTest)
#include "ApplicationWindowTest.moc"
