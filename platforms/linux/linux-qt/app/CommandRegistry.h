// The one place app commands are declared. CORE-007, EU-001.
//
// A feature registers each command once, with a stable id, label, group and
// the surfaces it appears on. The tray menu, the command window's buttons, its
// menu bar and the hotkey binder are all built from the registry, so adding a
// tool never means editing main() or ApplicationWindow's id lists.
//
// Rules:
//  - Ids are stable kebab-case and unique; registering a duplicate id fails
//    (returns nullptr) instead of replacing the first command.
//  - Order is (group order, then registration order within a group). Surfaces
//    put a separator between groups.
//  - The registry owns actions it creates; existing actions (for example from
//    WorkflowShortcutActions) can be registered without changing ownership.
//  - Registration happens before the surfaces are built; surfaces read the
//    registry once. Enabling/disabling is done on the QAction, which every
//    surface shares, so state stays consistent everywhere.
#pragma once

#include <QAction>
#include <QFlags>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class QMenu;

namespace xerahs::app {

// Declaration order is tray order.
enum class CommandGroup { Capture, Schedule, Record, Workflows, Tools, Files, Upload, Preferences, Application };

enum class CommandSurface {
  Tray = 0x1,
  Window = 0x2,           // a button in the command window grid
  FileMenu = 0x4,
  CaptureMenu = 0x8,
  ToolsMenu = 0x10,
  PreferencesMenu = 0x20,
};
Q_DECLARE_FLAGS(CommandSurfaces, CommandSurface)
Q_DECLARE_OPERATORS_FOR_FLAGS(CommandSurfaces)

struct CommandSpec {
  QString id;                 // stable, kebab-case, unique
  QString label;              // menu text, with "…" when it opens a window
  CommandGroup group = CommandGroup::Tools;
  CommandSurfaces surfaces = CommandSurface::Tray;
  QString accessibleDescription = {};
};

class CommandRegistry : public QObject {
  Q_OBJECT
public:
  explicit CommandRegistry(QObject *parent = nullptr);

  // Creates and owns a QAction for spec, or returns nullptr for an invalid or duplicate id.
  QAction *add(const CommandSpec &spec);
  // Registers an action owned elsewhere. Returns false for an invalid or duplicate id.
  bool adopt(const CommandSpec &spec, QAction *action);

  QAction *action(const QString &id) const;
  const CommandSpec *spec(const QString &id) const;
  // Ordered by group, then registration order.
  QList<QAction *> actions(CommandSurface surface) const;
  QStringList ids(CommandSurface surface) const;
  // Appends this surface's actions to menu, with separators between groups.
  void populate(QMenu *menu, CommandSurface surface) const;

private:
  struct Entry { CommandSpec spec; QAction *action = nullptr; int sequence = 0; };
  QList<Entry> ordered(CommandSurface surface) const;
  QMap<QString, Entry> m_entries;
  int m_sequence = 0;
};

}  // namespace xerahs::app
