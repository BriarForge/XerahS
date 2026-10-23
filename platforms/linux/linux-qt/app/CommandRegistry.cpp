// CORE-007, EU-001: one QAction per command, shared by every surface.
#include "CommandRegistry.h"

#include <QMenu>

#include <algorithm>

namespace xerahs::app {
namespace {

// Empty, uppercase, spaced or double-hyphen ids are not stable kebab-case.
bool validId(const QString &id) {
  if (id.isEmpty()) return false;
  bool hyphen = false;
  for (const QChar &c : id) {
    const bool word = (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9');
    if (c == u'-') {
      if (hyphen) return false;
      hyphen = true;
      continue;
    }
    if (!word) return false;
    hyphen = false;
  }
  return id.front() != u'-' && id.back() != u'-';
}

void describe(QAction *action, const CommandSpec &spec) {
  action->setObjectName(spec.id);
  if (spec.accessibleDescription.isEmpty()) return;
  if (action->toolTip().isEmpty()) action->setToolTip(spec.accessibleDescription);
  if (action->statusTip().isEmpty()) action->setStatusTip(spec.accessibleDescription);
}

}  // namespace

CommandRegistry::CommandRegistry(QObject *parent) : QObject(parent) {}

QAction *CommandRegistry::add(const CommandSpec &spec) {
  if (!validId(spec.id) || m_entries.contains(spec.id)) return nullptr;
  auto *action = new QAction(spec.label, this);
  describe(action, spec);
  m_entries.insert(spec.id, Entry{spec, action, m_sequence++});
  return action;
}

bool CommandRegistry::adopt(const CommandSpec &spec, QAction *action) {
  if (!action || !validId(spec.id) || m_entries.contains(spec.id)) return false;
  describe(action, spec);
  m_entries.insert(spec.id, Entry{spec, action, m_sequence++});
  return true;
}

QAction *CommandRegistry::action(const QString &id) const {
  const auto it = m_entries.constFind(id);
  return it == m_entries.cend() ? nullptr : it->action;
}

const CommandSpec *CommandRegistry::spec(const QString &id) const {
  const auto it = m_entries.constFind(id);
  return it == m_entries.cend() ? nullptr : &it->spec;
}

QList<CommandRegistry::Entry> CommandRegistry::ordered(CommandSurface surface) const {
  QList<Entry> list;
  for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
    if (it->spec.surfaces.testFlag(surface)) list.append(it.value());
  std::stable_sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) {
    const auto left = static_cast<int>(a.spec.group);
    const auto right = static_cast<int>(b.spec.group);
    if (left != right) return left < right;
    return a.sequence < b.sequence;
  });
  return list;
}

QList<QAction *> CommandRegistry::actions(CommandSurface surface) const {
  QList<QAction *> result;
  for (const auto &entry : ordered(surface)) result.append(entry.action);
  return result;
}

QStringList CommandRegistry::ids(CommandSurface surface) const {
  QStringList result;
  for (const auto &entry : ordered(surface)) result.append(entry.spec.id);
  return result;
}

void CommandRegistry::populate(QMenu *menu, CommandSurface surface) const {
  if (!menu) return;
  bool any = false;
  CommandGroup previous = CommandGroup::Capture;
  for (const auto &entry : ordered(surface)) {
    if (any && entry.spec.group != previous) menu->addSeparator();
    menu->addAction(entry.action);
    previous = entry.spec.group;
    any = true;
  }
}

}  // namespace xerahs::app
