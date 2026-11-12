// CORE-007: command palette over a real CommandRegistry and its QActions.
//
// Open the palette, then type. Rows use CommandPalette::CommandIdRole (the stable
// id) and CommandLabelRole (the live label without mnemonics). Search is a trimmed
// case-insensitive substring of that label, the registered label, or the id.
// Hidden commands are not offered. Disabled ones stay listed and cannot run.
// Return, Enter, and Run call the shared QAction once, only after the palette has
// left its modal state. Escape and reject() do not run anything.
#include "CommandPalette.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWidget>
#include <memory>

using namespace xerahs::app;

namespace {

struct Palette {
  QLineEdit *search = nullptr;
  QListWidget *commands = nullptr;
  QPushButton *run = nullptr;
  QLabel *status = nullptr;
};

struct Probe {
  QWidget *palette = nullptr;
  bool insideExec = false;
  int calls = 0;
  bool ranDuringExec = false;
  bool ranWhileModal = false;
};

Palette bind(QWidget &palette) {
  return {palette.findChild<QLineEdit *>(QStringLiteral("commandPaletteSearch")),
      palette.findChild<QListWidget *>(QStringLiteral("commandPaletteCommands")),
      palette.findChild<QPushButton *>(QStringLiteral("runCommand")),
      palette.findChild<QLabel *>(QStringLiteral("commandPaletteStatus"))};
}

bool ready(const Palette &ui) { return ui.search && ui.commands && ui.run && ui.status; }

QString rowId(const QListWidgetItem *item) {
  return item ? item->data(CommandPalette::CommandIdRole).toString() : QString();
}

QStringList visibleIds(const QListWidget *list) {
  QStringList ids;
  for (int row = 0; row < list->count(); ++row) {
    if (!list->isRowHidden(row)) ids.append(rowId(list->item(row)));
  }
  return ids;
}

QString currentId(const QListWidget *list) { return rowId(list->currentItem()); }

QListWidgetItem *itemFor(QListWidget *list, const QString &id) {
  for (int row = 0; row < list->count(); ++row)
    if (rowId(list->item(row)) == id) return list->item(row);
  return nullptr;
}

void flushDeferred() { QTest::qWait(50); }

void arm(QAction *action, QObject *context, Probe *probe) {
  QObject::connect(action, &QAction::triggered, context, [probe] {
    ++probe->calls;
    probe->ranDuringExec = probe->insideExec;
    probe->ranWhileModal = probe->palette->isVisible() || probe->palette->isModal()
        || probe->palette->windowModality() != Qt::NonModal
        || probe->palette->testAttribute(Qt::WA_ShowModal)
        || QApplication::activeModalWidget() == probe->palette;
  });
}

int execReturn(CommandPalette &palette, Probe *probe) {
  QTimer safety;
  safety.setSingleShot(true);
  QObject::connect(&safety, &QTimer::timeout, &palette, [&palette] {
    if (palette.isVisible()) palette.reject();
  });
  QTimer::singleShot(0, &palette, [&palette] {
    if (auto *search = palette.findChild<QLineEdit *>(QStringLiteral("commandPaletteSearch"))) search->setFocus();
    QTest::keyClick(palette.findChild<QLineEdit *>(QStringLiteral("commandPaletteSearch")), Qt::Key_Return);
  });
  safety.start(1500);
  if (probe) probe->insideExec = true;
  const int code = palette.exec();
  if (probe) probe->insideExec = false;
  safety.stop();
  flushDeferred();
  return code;
}

CommandSpec command(const char *id, const QString &label, CommandGroup group = CommandGroup::Tools) {
  return CommandSpec{QString::fromLatin1(id), label, group, CommandSurface::Window};
}

}  // namespace

class CommandPaletteTest final : public QObject {
  Q_OBJECT
private slots:
  void emptyRegistryHasNothingToRun() {
    CommandRegistry registry;
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY2(ready(ui), "commandPaletteSearch, commandPaletteCommands, runCommand, commandPaletteStatus");
    QCOMPARE(palette.windowTitle(), QStringLiteral("Command palette"));
    palette.show();
    palette.activateWindow();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(ui.search));
    QCOMPARE(ui.search->placeholderText(), QStringLiteral("Search commands"));
    QCOMPARE(ui.search->accessibleName(), QStringLiteral("Search commands"));
    QCOMPARE(ui.run->text(), QStringLiteral("Run"));
    QVERIFY(visibleIds(ui.commands).isEmpty());
    QVERIFY(!ui.run->isEnabled());
    QVERIFY(ui.status->text().contains(QStringLiteral("registered"), Qt::CaseInsensitive));
    QTest::keyClick(ui.search, Qt::Key_Return);
    flushDeferred();
    QVERIFY(palette.isVisible());
    QCOMPARE(palette.result(), int(QDialog::Rejected));
  }

  void visibleCommandsFollowGroupOrderAndStayUniqueAcrossSurfaces() {
    QWidget anchor;
    CommandRegistry registry;
    registry.add(command("hash-checker", QStringLiteral("Hash checker")));
    registry.add(CommandSpec{QStringLiteral("capture-region"), QStringLiteral("&Capture region"), CommandGroup::Capture,
        CommandSurface::Tray | CommandSurface::Window | CommandSurface::CaptureMenu});
    registry.add(command("capture-window", QStringLiteral("Capture window"), CommandGroup::Capture));
    registry.add(command("quit-app", QStringLiteral("Quit"), CommandGroup::Application));
    CommandPalette palette(registry, &anchor);
    QCOMPARE(palette.parentWidget(), &anchor);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("capture-region"), QStringLiteral("capture-window"),
        QStringLiteral("hash-checker"), QStringLiteral("quit-app")}));
    QCOMPARE(itemFor(ui.commands, QStringLiteral("capture-region"))->text(), QStringLiteral("Capture region"));
    QCOMPARE(itemFor(ui.commands, QStringLiteral("capture-region"))->data(CommandPalette::CommandLabelRole).toString(),
        QStringLiteral("Capture region"));
    ui.search->setText(QStringLiteral("quit-app"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("quit-app")}));
    ui.search->clear();
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("capture-region"), QStringLiteral("capture-window"),
        QStringLiteral("hash-checker"), QStringLiteral("quit-app")}));
  }

  void searchMatchesLabelMnemonicStableIdAndUnicode() {
    CommandRegistry registry;
    registry.add(command("capture-region", QString::fromUtf8("Öffnen Bild"), CommandGroup::Capture));
    registry.add(CommandSpec{QStringLiteral("save-image"), QString::fromUtf8("Save && copy"), CommandGroup::Files, CommandSurface::Window});
    registry.add(command("export-image", QString::fromUtf8("保存"), CommandGroup::Files));
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    QCOMPARE(itemFor(ui.commands, QStringLiteral("save-image"))->text(), QStringLiteral("Save & copy"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("capture-region"), QStringLiteral("save-image"), QStringLiteral("export-image")}));

    ui.search->setText(QString::fromUtf8("öff"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("capture-region")}));
    QCOMPARE(currentId(ui.commands), QStringLiteral("capture-region"));

    ui.search->setText(QString::fromUtf8("保存"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("export-image")}));

    ui.search->setText(QStringLiteral("  SAVE-IMAGE  "));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("save-image")}));

    ui.search->setText(QStringLiteral("save & copy"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("save-image")}));

    ui.search->setText(QStringLiteral("IMAGE"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("save-image"), QStringLiteral("export-image")}));
    QCOMPARE(currentId(ui.commands), QStringLiteral("save-image"));
  }

  void liveTextVisibilityAndEnabledState() {
    CommandRegistry registry;
    registry.add(command("alpha-live", QStringLiteral("Alpha")));
    auto *beta = registry.add(command("beta-live", QStringLiteral("Beta command")));
    beta->setToolTip(QStringLiteral("screen capture is not permitted"));
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("alpha-live"), QStringLiteral("beta-live")}));

    beta->setVisible(false);
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("alpha-live")}));
    beta->setVisible(true);
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("alpha-live"), QStringLiteral("beta-live")}));

    beta->setText(QString::fromUtf8("截取区域"));
    auto *betaRow = itemFor(ui.commands, QStringLiteral("beta-live"));
    QVERIFY(betaRow);
    QCOMPARE(betaRow->text(), QString::fromUtf8("截取区域"));
    QCOMPARE(betaRow->data(CommandPalette::CommandLabelRole).toString(), QString::fromUtf8("截取区域"));
    ui.search->setText(QString::fromUtf8("区域"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("beta-live")}));
    ui.search->setText(QStringLiteral("Beta command"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("beta-live")}));

    ui.search->setText(QStringLiteral("beta-live"));
    QCOMPARE(currentId(ui.commands), QStringLiteral("beta-live"));
    beta->setEnabled(false);
    QVERIFY(visibleIds(ui.commands).contains(QStringLiteral("beta-live")));
    QVERIFY(!ui.run->isEnabled());
    QVERIFY(ui.status->text().contains(QStringLiteral("is unavailable")));
    QVERIFY(ui.status->text().contains(QStringLiteral("screen capture is not permitted")));
    QSignalSpy ran(beta, &QAction::triggered);
    QTest::keyClick(ui.search, Qt::Key_Return);
    ui.run->click();
    flushDeferred();
    QCOMPARE(ran.count(), 0);
    QVERIFY(palette.isVisible());

    beta->setEnabled(true);
    QVERIFY(ui.run->isEnabled());
    QVERIFY(!ui.status->text().contains(QStringLiteral("is unavailable")));
    QTest::keyClick(ui.search, Qt::Key_Return);
    flushDeferred();
    QCOMPARE(ran.count(), 1);
    QVERIFY(!palette.isVisible());
    QCOMPARE(palette.result(), int(QDialog::Accepted));
  }

  void keyboardSelectionAndEnterRunsOnlyTheChosenCommand() {
    CommandRegistry registry;
    auto *first = registry.add(command("alpha-one", QStringLiteral("Alpha one")));
    auto *second = registry.add(command("alpha-two", QStringLiteral("Alpha two")));
    auto *beta = registry.add(command("beta-one", QStringLiteral("Beta")));
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    ui.search->setFocus();
    QTest::keyClicks(ui.search, QStringLiteral("alpha"));
    QCOMPARE(ui.search->text(), QStringLiteral("alpha"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("alpha-one"), QStringLiteral("alpha-two")}));
    QCOMPARE(currentId(ui.commands), QStringLiteral("alpha-one"));
    QTest::keyClick(ui.search, Qt::Key_Down);
    QCOMPARE(currentId(ui.commands), QStringLiteral("alpha-two"));
    QTest::keyClick(ui.search, Qt::Key_Up);
    QCOMPARE(currentId(ui.commands), QStringLiteral("alpha-one"));
    QTest::keyClick(ui.search, Qt::Key_Down);
    QCOMPARE(currentId(ui.commands), QStringLiteral("alpha-two"));

    QSignalSpy firstSpy(first, &QAction::triggered);
    QSignalSpy secondSpy(second, &QAction::triggered);
    QSignalSpy betaSpy(beta, &QAction::triggered);
    Probe probe;
    probe.palette = &palette;
    QObject watcher;
    arm(second, &watcher, &probe);
    QTest::keyClick(ui.search, Qt::Key_Return);
    flushDeferred();
    QCOMPARE(firstSpy.count(), 0);
    QCOMPARE(secondSpy.count(), 1);
    QCOMPARE(betaSpy.count(), 0);
    QCOMPARE(probe.calls, 1);
    QVERIFY(!probe.ranDuringExec);
    QVERIFY(!probe.ranWhileModal);
    QCOMPARE(palette.result(), int(QDialog::Accepted));

    CommandRegistry keypad;
    auto *only = keypad.add(command("only-keypad", QStringLiteral("Only")));
    CommandPalette keypadPalette(keypad);
    const auto keys = bind(keypadPalette);
    QVERIFY(ready(keys));
    keypadPalette.show();
    QSignalSpy keypadSpy(only, &QAction::triggered);
    keys.search->setFocus();
    QTest::keyClick(keys.search, Qt::Key_Enter);
    flushDeferred();
    QCOMPARE(keypadSpy.count(), 1);
    QCOMPARE(keypadPalette.result(), int(QDialog::Accepted));
  }

  void escapeAndRejectDoNotRunACommand() {
    CommandRegistry registry;
    auto *action = registry.add(command("save-image", QStringLiteral("Save image")));
    QSignalSpy ran(action, &QAction::triggered);
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    ui.search->setText(QStringLiteral("save-image"));
    QCOMPARE(currentId(ui.commands), QStringLiteral("save-image"));
    ui.search->setFocus();
    QTest::keyClick(ui.search, Qt::Key_Escape);
    flushDeferred();
    QCOMPARE(ran.count(), 0);
    QVERIFY(!palette.isVisible());
    QCOMPARE(palette.result(), int(QDialog::Rejected));
    QVERIFY(ui.status->text().contains(QStringLiteral("No command was started")));
    QVERIFY(!ui.status->text().contains(QStringLiteral("success"), Qt::CaseInsensitive));

    CommandPalette dismissed(registry);
    dismissed.show();
    dismissed.reject();
    flushDeferred();
    QCOMPARE(ran.count(), 0);
    QCOMPARE(dismissed.result(), int(QDialog::Rejected));
    QVERIFY(dismissed.findChild<QLabel *>(QStringLiteral("commandPaletteStatus"))->text().contains(QStringLiteral("No command was started")));
  }

  void disabledOrEmptySelectionCannotRun() {
    CommandRegistry registry;
    auto *alpha = registry.add(command("alpha-run", QStringLiteral("Alpha")));
    auto *beta = registry.add(command("beta-run", QStringLiteral("Beta")));
    beta->setToolTip(QStringLiteral("screen capture is not permitted"));
    beta->setEnabled(false);
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    QCOMPARE(currentId(ui.commands), QStringLiteral("alpha-run"));
    QTest::keyClick(ui.search, Qt::Key_Down);
    QCOMPARE(currentId(ui.commands), QStringLiteral("beta-run"));
    QVERIFY(!ui.run->isEnabled());
    QVERIFY(ui.status->text().contains(QStringLiteral("is unavailable")));
    QVERIFY(ui.status->text().contains(QStringLiteral("screen capture is not permitted")));
    QSignalSpy betaSpy(beta, &QAction::triggered);
    QSignalSpy alphaSpy(alpha, &QAction::triggered);
    ui.run->click();
    QTest::keyClick(ui.search, Qt::Key_Return);
    QTest::keyClick(ui.search, Qt::Key_Enter);
    flushDeferred();
    QCOMPARE(betaSpy.count(), 0);
    QCOMPARE(alphaSpy.count(), 0);
    QVERIFY(palette.isVisible());
    QVERIFY(!ui.status->text().contains(QStringLiteral("success"), Qt::CaseInsensitive));

    ui.search->setText(QStringLiteral("zz-no-such"));
    QVERIFY(visibleIds(ui.commands).isEmpty());
    QVERIFY(!ui.run->isEnabled());
    QVERIFY(ui.status->text().contains(QStringLiteral("zz-no-such")));
    ui.run->click();
    QTest::keyClick(ui.search, Qt::Key_Return);
    flushDeferred();
    QCOMPARE(alphaSpy.count(), 0);
    QCOMPARE(betaSpy.count(), 0);
    QVERIFY(palette.isVisible());
  }

  void runButtonInvokesTheSharedActionOnce() {
    QObject owner;
    CommandRegistry registry;
    auto *action = new QAction(QStringLiteral("Save image"), &owner);
    QVERIFY(registry.adopt(command("save-image", QStringLiteral("Save image")), action));
    auto *other = registry.add(command("export-image", QStringLiteral("Export")));
    QMenu menu;
    menu.addAction(action);
    CommandPalette palette(registry);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    ui.search->setText(QStringLiteral("save-image"));
    QCOMPARE(currentId(ui.commands), QStringLiteral("save-image"));
    QVERIFY(ui.run->isEnabled());
    QSignalSpy ran(action, &QAction::triggered);
    QSignalSpy otherRan(other, &QAction::triggered);
    Probe probe;
    probe.palette = &palette;
    QObject watcher;
    arm(action, &watcher, &probe);
    ui.run->click();
    flushDeferred();
    QCOMPARE(ran.count(), 1);
    QCOMPARE(otherRan.count(), 0);
    QCOMPARE(probe.calls, 1);
    QVERIFY(!probe.ranWhileModal);
    QVERIFY(!probe.ranDuringExec);
    QCOMPARE(palette.result(), int(QDialog::Accepted));
    QCOMPARE(menu.actions().first(), action);
    QCOMPARE(registry.action(QStringLiteral("save-image")), action);
    QCOMPARE(action->parent(), &owner);
  }

  void modalLoopIsReleasedBeforeTheHandler() {
    CommandRegistry registry;
    auto *action = registry.add(command("save-image", QStringLiteral("Save image")));
    CommandPalette palette(registry);
    QVERIFY(ready(bind(palette)));
    QSignalSpy ran(action, &QAction::triggered);
    Probe probe;
    probe.palette = &palette;
    QObject watcher;
    arm(action, &watcher, &probe);
    QCOMPARE(execReturn(palette, &probe), int(QDialog::Accepted));
    QCOMPARE(ran.count(), 1);
    QCOMPARE(probe.calls, 1);
    QVERIFY(!probe.ranDuringExec);
    QVERIFY(!probe.ranWhileModal);
  }

  void disablingBeforeDeferredDispatchPreventsTheTrigger() {
    CommandRegistry registry;
    auto *action = registry.add(command("save-image", QStringLiteral("Save image")));
    CommandPalette palette(registry);
    QSignalSpy ran(action, &QAction::triggered);
    QObject watcher;
    QObject::connect(&palette, &QDialog::accepted, &watcher, [action] { action->setEnabled(false); });
    Probe probe;
    probe.palette = &palette;
    arm(action, &watcher, &probe);
    QCOMPARE(execReturn(palette, &probe), int(QDialog::Accepted));
    QCOMPARE(ran.count(), 0);
    QCOMPARE(probe.calls, 0);
    QVERIFY(!action->isEnabled());
  }

  void destroyingTheActionBeforeDeferredDispatchPreventsTheTrigger() {
    CommandRegistry registry;
    QPointer<QAction> action = registry.add(command("save-image", QStringLiteral("Save image")));
    CommandPalette palette(registry);
    int calls = 0;
    QObject watcher;
    QObject::connect(action, &QAction::triggered, &watcher, [&calls] { ++calls; });
    QObject::connect(&palette, &QDialog::accepted, &watcher, [action] { delete action.data(); });
    QCOMPARE(execReturn(palette, nullptr), int(QDialog::Accepted));
    QVERIFY(action.isNull());
    QCOMPARE(calls, 0);
  }

  void destroyingTheActionOwnerBeforeDeferredDispatchPreventsTheTrigger() {
    auto *owner = new QObject;
    auto *action = new QAction(QStringLiteral("Save image"), owner);
    CommandRegistry registry;
    QVERIFY(registry.adopt(command("save-image", QStringLiteral("Save image")), action));
    CommandPalette palette(registry);
    int calls = 0;
    QObject watcher;
    QObject::connect(action, &QAction::triggered, &watcher, [&calls] { ++calls; });
    QObject::connect(&palette, &QDialog::accepted, &watcher, [owner] { delete owner; });
    QCOMPARE(execReturn(palette, nullptr), int(QDialog::Accepted));
    QCOMPARE(calls, 0);
  }

  void destroyingTheDialogOwnerBeforeDeferredDispatchPreventsTheTrigger() {
    auto *window = new QWidget;
    CommandRegistry registry;
    auto *action = registry.add(command("save-image", QStringLiteral("Save image")));
    auto palette = std::make_unique<CommandPalette>(registry, window);
    QCOMPARE(palette->parentWidget(), window);
    palette->setParent(nullptr);
    int calls = 0;
    QObject watcher;
    QObject::connect(action, &QAction::triggered, &watcher, [&calls] { ++calls; });
    QObject::connect(palette.get(), &QDialog::accepted, &watcher, [window] { delete window; });
    QCOMPARE(execReturn(*palette, nullptr), int(QDialog::Accepted));
    QCOMPARE(calls, 0);
  }
};

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QPA_PLATFORMTHEME", "");
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);
  CommandPaletteTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "CommandPaletteTest.moc"
