// Real Qt actions/keyboard/accessibility; no capture, upload or settings I/O.
#include "ApplicationWindow.h"
#include "ColorPickerDialog.h"
#include "CommandRegistry.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "features/ToolsFeature.h"
#ifdef XERAHS_VIDEO_CONVERTER
#include "VideoConverterDialog.h"
#endif
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
#include <QGridLayout>
using namespace xerahs::app;

namespace {
CommandSpec command(const char *id, const char *label, CommandGroup group, CommandSurfaces surfaces) {
  return CommandSpec{QString::fromLatin1(id), QString::fromLatin1(label), group, surfaces};
}
QStringList menuEntries(QMenu *menu) {
  QStringList entries;
  for (auto *action : menu->actions())
    entries.append(action->isSeparator() ? QStringLiteral("---") : action->objectName());
  return entries;
}
QMenu *menuTitled(QMenuBar *bar, const QString &title) {
  for (auto *action : bar->actions())
    if (action->menu() && action->menu()->title() == title) return action->menu();
  return nullptr;
}
}  // namespace
class ApplicationWindowTest final : public QObject {
  Q_OBJECT
private slots:
  void chooseActionsUsesTheSharedMainAndMenuActionAndAvailability() {
    QAction choose(QStringLiteral("Capture region and choose actions…"));
    ApplicationWindow window({{"capture-region-choose-actions", &choose}}); window.activate();
    auto *button = window.findChild<QToolButton *>("capture-region-choose-actions"); QVERIFY(button);
    QSignalSpy invoked(&choose, &QAction::triggered); button->setFocus(); QTest::keyClick(button, Qt::Key_Space); QCOMPARE(invoked.count(), 1);
    bool found = false;
    for (auto *action : window.menuBar()->actions()) if (auto *menu = action->menu()) found |= menu->actions().contains(&choose);
    QVERIFY(found); choose.setEnabled(false); QVERIFY(!button->isEnabled()); QTest::keyClick(button, Qt::Key_Space); QCOMPARE(invoked.count(), 1);
  }
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
  void escapeCancelsTheWindowOperationAndConsentDialogsKeepTheirOwnEscape() {
    QAction cancel(QStringLiteral("Cancel capture")); ApplicationWindow window({{"cancel-capture", &cancel}}); window.activate();
    QSignalSpy cancelled(&cancel, &QAction::triggered); QTRY_VERIFY(window.isActiveWindow());
    QTest::keyClick(&window, Qt::Key_Escape); QCOMPARE(cancelled.count(), 1);
    QDialog consent(&window); consent.setModal(true); consent.show(); QTRY_COMPARE(QApplication::activeModalWidget(), &consent);
    QTest::keyClick(&consent, Qt::Key_Escape); QTRY_VERIFY(!consent.isVisible()); QCOMPARE(cancelled.count(), 1);
    cancel.setEnabled(false); window.activate(); QTest::keyClick(&window, Qt::Key_Escape); QCOMPARE(cancelled.count(), 1);
  }
  void durableHistoryUsesTheSharedActionFromTheWindowAndMenu() {
    QAction history(QStringLiteral("Capture result history…"));
    ApplicationWindow window({{"capture-history", &history}}); window.activate();
    auto *button = window.findChild<QToolButton *>("capture-history"); QVERIFY(button);
    QSignalSpy invoked(&history, &QAction::triggered); button->setFocus(); QTest::keyClick(button, Qt::Key_Space); QCOMPARE(invoked.count(), 1);
    bool found = false;
    for (auto *action : window.menuBar()->actions()) if (auto *menu = action->menu()) found |= menu->actions().contains(&history);
    QVERIFY(found); history.setEnabled(false); QVERIFY(!button->isEnabled());
  }
  void windowAndMenusFollowTheRegistrySurfaces() {
    CommandRegistry commands;
    const CommandSurfaces capture = CommandSurface::Tray | CommandSurface::Window | CommandSurface::CaptureMenu;
    const CommandSurfaces files = CommandSurface::Tray | CommandSurface::Window | CommandSurface::FileMenu;
    commands.add(command("quit", "Quit XerahS", CommandGroup::Application, CommandSurface::Tray | CommandSurface::FileMenu));
    commands.add(command("editor-appearance", "Editor appearance…", CommandGroup::Preferences,
        CommandSurface::Tray | CommandSurface::Window | CommandSurface::PreferencesMenu));
    commands.add(command("settings", "Settings…", CommandGroup::Preferences, CommandSurface::Tray | CommandSurface::PreferencesMenu));
    commands.add(command("watch-folders", "Watch folders…", CommandGroup::Upload, CommandSurface::Tray));
    commands.add(command("upload-destinations", "Upload destinations…", CommandGroup::Upload,
        CommandSurface::Tray | CommandSurface::Window | CommandSurface::PreferencesMenu));
    commands.add(command("upload-file", "Upload file…", CommandGroup::Upload, files));
    commands.add(command("capture-history", "Capture history…", CommandGroup::Files, files));
    commands.add(command("captures-folder", "Open captures folder", CommandGroup::Files, files));
    commands.add(command("open-image", "Open image…", CommandGroup::Files, files));
    commands.add(command("new-image", "New image…", CommandGroup::Files, files));
    commands.add(command("pick-colour", "Pick colour…", CommandGroup::Tools,
        CommandSurface::Tray | CommandSurface::Window | CommandSurface::ToolsMenu | CommandSurface::FileMenu));
    commands.add(command("hash-checker", "Hash checker…", CommandGroup::Tools,
        CommandSurface::Tray | CommandSurface::Window | CommandSurface::ToolsMenu));
    commands.add(command("capture-workflows", "Capture workflows…", CommandGroup::Workflows, capture));
    commands.add(command("cancel-capture", "Cancel capture", CommandGroup::Capture, capture));
    commands.add(command("capture-region", "Capture region", CommandGroup::Capture, capture));
    commands.add(command("capture-after-delay", "Capture after delay…", CommandGroup::Schedule,
        CommandSurface::Tray | CommandSurface::CaptureMenu));
    commands.add(command("open-xerahs", "Open XerahS", CommandGroup::Capture, CommandSurface::Tray));
    ApplicationWindow window(commands);
    auto *grid = qobject_cast<QGridLayout *>(window.centralWidget()->layout()->itemAt(1)->layout());
    QVERIFY(grid);
    QStringList buttons;
    for (int i = 0; i < grid->count(); ++i) buttons.append(qobject_cast<QToolButton *>(grid->itemAt(i)->widget())->objectName());
    QCOMPARE(buttons, commands.ids(CommandSurface::Window));
    QCOMPARE(buttons, QStringList({QStringLiteral("cancel-capture"), QStringLiteral("capture-region"),
        QStringLiteral("capture-workflows"), QStringLiteral("pick-colour"), QStringLiteral("hash-checker"),
        QStringLiteral("capture-history"), QStringLiteral("captures-folder"), QStringLiteral("open-image"),
        QStringLiteral("new-image"), QStringLiteral("upload-destinations"), QStringLiteral("upload-file"),
        QStringLiteral("editor-appearance")}));
    QCOMPARE(menuEntries(menuTitled(window.menuBar(), QStringLiteral("&File"))),
        QStringList({QStringLiteral("pick-colour"), QStringLiteral("---"), QStringLiteral("capture-history"),
            QStringLiteral("captures-folder"), QStringLiteral("open-image"), QStringLiteral("new-image"),
            QStringLiteral("---"), QStringLiteral("upload-file"), QStringLiteral("---"), QStringLiteral("quit")}));
    QCOMPARE(menuEntries(menuTitled(window.menuBar(), QStringLiteral("&Capture"))),
        QStringList({QStringLiteral("cancel-capture"), QStringLiteral("capture-region"), QStringLiteral("---"),
            QStringLiteral("capture-after-delay"), QStringLiteral("---"), QStringLiteral("capture-workflows")}));
    QCOMPARE(menuEntries(menuTitled(window.menuBar(), QStringLiteral("&Tools"))),
        QStringList({QStringLiteral("pick-colour"), QStringLiteral("hash-checker")}));
    QCOMPARE(menuEntries(menuTitled(window.menuBar(), QStringLiteral("&Preferences"))),
        QStringList({QStringLiteral("upload-destinations"), QStringLiteral("---"), QStringLiteral("editor-appearance"),
            QStringLiteral("settings")}));
    QMenu tray;
    commands.populate(&tray, CommandSurface::Tray);
    QCOMPARE(menuEntries(&tray), QStringList({
        QStringLiteral("cancel-capture"), QStringLiteral("capture-region"), QStringLiteral("open-xerahs"), QStringLiteral("---"),
        QStringLiteral("capture-after-delay"), QStringLiteral("---"),
        QStringLiteral("capture-workflows"), QStringLiteral("---"),
        QStringLiteral("pick-colour"), QStringLiteral("hash-checker"), QStringLiteral("---"),
        QStringLiteral("capture-history"), QStringLiteral("captures-folder"), QStringLiteral("open-image"), QStringLiteral("new-image"), QStringLiteral("---"),
        QStringLiteral("watch-folders"), QStringLiteral("upload-destinations"), QStringLiteral("upload-file"), QStringLiteral("---"),
        QStringLiteral("editor-appearance"), QStringLiteral("settings"), QStringLiteral("---"),
        QStringLiteral("quit")}));
  }
  void toolCommandsOpenOneDialogAndRaiseItAgain() {
    QApplication::setQuitOnLastWindowClosed(false);
    CommandRegistry commands;
    QWidget owner;
    installToolDialogs(commands, [&owner] { return &owner; });
    const auto check = [](QAction *action, const auto &listed) {
      if (!action) return false;
      action->trigger();
      if (listed().size() != 1 || !listed().first()->isVisible()) return false;
      auto *dialog = listed().first();
      dialog->hide();
      action->trigger();
      return listed().size() == 1 && listed().first() == dialog && dialog->isVisible();
    };
    QVERIFY(check(commands.action(QStringLiteral("pick-colour")), [&owner] { return owner.findChildren<ColorPickerDialog *>(); }));
    QVERIFY(check(commands.action(QStringLiteral("hash-checker")), [&owner] { return owner.findChildren<HashCheckerDialog *>(); }));
    QVERIFY(check(commands.action(QStringLiteral("icon-converter")), [&owner] { return owner.findChildren<IconConverterDialog *>(); }));
#ifdef XERAHS_VIDEO_CONVERTER
    QVERIFY(commands.action(QStringLiteral("video-converter")));
    QVERIFY(check(commands.action(QStringLiteral("video-converter")), [&owner] { return owner.findChildren<VideoConverterDialog *>(); }));
#else
    QVERIFY(!commands.action(QStringLiteral("video-converter")));
#endif
  }
};
QTEST_MAIN(ApplicationWindowTest)
#include "ApplicationWindowTest.moc"
