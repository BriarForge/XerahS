// CORE-007, EU-001: ids, ownership, order and separators.
#include "CommandRegistry.h"

#include <QMenu>
#include <QPointer>
#include <QTest>

using namespace xerahs::app;

namespace {

CommandSpec spec(const QString &id, CommandGroup group, CommandSurfaces surfaces) {
  return CommandSpec{id, id, group, surfaces};
}

QStringList menuEntries(QMenu &menu) {
  QStringList entries;
  for (auto *action : menu.actions())
    entries.append(action->isSeparator() ? QStringLiteral("---") : action->objectName());
  return entries;
}

}  // namespace

class CommandRegistryTest final : public QObject {
  Q_OBJECT
private slots:
  void invalidAndDuplicateIdsAreRefused() {
    CommandRegistry registry;
    QVERIFY(!registry.add(spec(QString(), CommandGroup::Tools, CommandSurface::Tray)));
    for (const char *id : {"-", "-capture", "capture-", "capture--region", "Capture", "capture_region", "capture region"})
      QVERIFY(!registry.add(spec(QString::fromLatin1(id), CommandGroup::Tools, CommandSurface::Tray)));
    auto *first = registry.add(spec(QStringLiteral("capture-region"), CommandGroup::Capture, CommandSurface::Tray));
    QVERIFY(first);
    QVERIFY(!registry.add(spec(QStringLiteral("capture-region"), CommandGroup::Tools, CommandSurface::Window)));
    QCOMPARE(registry.action(QStringLiteral("capture-region")), first);
    QCOMPARE(registry.spec(QStringLiteral("capture-region"))->group, CommandGroup::Capture);
    QAction owned(QStringLiteral("elsewhere"));
    QVERIFY(!registry.adopt(spec(QStringLiteral("capture-region"), CommandGroup::Tools, CommandSurface::Tray), &owned));
    QVERIFY(!registry.adopt(spec(QStringLiteral("valid-id"), CommandGroup::Tools, CommandSurface::Tray), nullptr));
    QCOMPARE(registry.action(QStringLiteral("capture-region")), first);
  }

  void orderIsGroupThenRegistration() {
    CommandRegistry registry;
    registry.add(spec(QStringLiteral("hash-checker"), CommandGroup::Tools, CommandSurface::Tray | CommandSurface::Window));
    registry.add(spec(QStringLiteral("capture-region"), CommandGroup::Capture, CommandSurface::Tray | CommandSurface::Window));
    registry.add(spec(QStringLiteral("settings"), CommandGroup::Preferences, CommandSurface::Tray));
    registry.add(spec(QStringLiteral("capture-monitor"), CommandGroup::Capture, CommandSurface::Window));
    registry.add(spec(QStringLiteral("quit"), CommandGroup::Application, CommandSurface::Tray | CommandSurface::FileMenu));
    QCOMPARE(registry.ids(CommandSurface::Tray), QStringList({QStringLiteral("capture-region"), QStringLiteral("hash-checker"),
        QStringLiteral("settings"), QStringLiteral("quit")}));
    QCOMPARE(registry.ids(CommandSurface::Window), QStringList({QStringLiteral("capture-region"), QStringLiteral("capture-monitor"),
        QStringLiteral("hash-checker")}));
    QCOMPARE(registry.ids(CommandSurface::FileMenu), QStringList({QStringLiteral("quit")}));
    QCOMPARE(registry.ids(CommandSurface::CaptureMenu), QStringList());
  }

  void adoptKeepsOwnership() {
    QObject owner;
    auto *action = new QAction(QStringLiteral("Enable shortcuts"), &owner);
    action->setToolTip(QStringLiteral("kept"));
    QPointer<QAction> created;
    {
      CommandRegistry registry;
      QVERIFY(registry.adopt(spec(QStringLiteral("enable-workflow-shortcuts"), CommandGroup::Workflows, CommandSurface::Tray), action));
      QCOMPARE(action->parent(), &owner);
      QCOMPARE(action->objectName(), QStringLiteral("enable-workflow-shortcuts"));
      QCOMPARE(action->toolTip(), QStringLiteral("kept"));
      created = registry.add(spec(QStringLiteral("pick-colour"), CommandGroup::Tools, CommandSurface::Tray));
      QVERIFY(created);
      QCOMPARE(created->parent(), &registry);
    }
    QVERIFY(created.isNull());
    QCOMPARE(action->parent(), &owner);
    QVERIFY(action);
  }

  void oneActionIsSharedByEverySurface() {
    CommandRegistry registry;
    auto *action = registry.add(spec(QStringLiteral("pick-colour"), CommandGroup::Tools,
        CommandSurface::Tray | CommandSurface::Window | CommandSurface::ToolsMenu | CommandSurface::FileMenu));
    QCOMPARE(registry.actions(CommandSurface::Tray).value(0), action);
    QCOMPARE(registry.actions(CommandSurface::Window).value(0), action);
    QCOMPARE(registry.actions(CommandSurface::ToolsMenu).value(0), action);
    QCOMPARE(registry.actions(CommandSurface::FileMenu).value(0), action);
    action->setEnabled(false);
    QVERIFY(!registry.actions(CommandSurface::Window).first()->isEnabled());
  }

  void populateSeparatesOnlyNonEmptyGroups() {
    CommandRegistry registry;
    registry.add(spec(QStringLiteral("capture-region"), CommandGroup::Capture, CommandSurface::Tray));
    registry.add(spec(QStringLiteral("cancel-capture"), CommandGroup::Capture, CommandSurface::Tray));
    registry.add(spec(QStringLiteral("hash-checker"), CommandGroup::Tools, CommandSurface::Window));
    registry.add(spec(QStringLiteral("new-image"), CommandGroup::Files, CommandSurface::Tray));
    registry.add(spec(QStringLiteral("quit"), CommandGroup::Application, CommandSurface::Tray));
    QMenu menu;
    registry.populate(&menu, CommandSurface::Tray);
    QCOMPARE(menuEntries(menu), QStringList({QStringLiteral("capture-region"), QStringLiteral("cancel-capture"),
        QStringLiteral("---"), QStringLiteral("new-image"), QStringLiteral("---"), QStringLiteral("quit")}));
    QMenu windowOnly;
    registry.populate(&windowOnly, CommandSurface::Window);
    QCOMPARE(menuEntries(windowOnly), QStringList({QStringLiteral("hash-checker")}));
    registry.populate(nullptr, CommandSurface::Tray);
  }
};

QTEST_MAIN(CommandRegistryTest)
#include "CommandRegistryTest.moc"
