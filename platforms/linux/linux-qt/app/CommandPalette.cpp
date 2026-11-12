// CORE-007: search the registry and run the one QAction the other surfaces already share.
#include "CommandPalette.h"

#include "AccessibleCue.h"
#include "NativeTranslations.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace xerahs::app {
namespace {

constexpr CommandSurface kSurfaces[] = {
    CommandSurface::Tray,
    CommandSurface::Window,
    CommandSurface::FileMenu,
    CommandSurface::CaptureMenu,
    CommandSurface::ToolsMenu,
    CommandSurface::PreferencesMenu,
};

QString withoutMnemonic(const QString &text) {
  QString out;
  out.reserve(text.size());
  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i) != QLatin1Char('&')) {
      out.append(text.at(i));
      continue;
    }
    if (i + 1 >= text.size()) {
      out.append(text.at(i));
      break;
    }
    if (text.at(i + 1) == QLatin1Char('&')) {
      out.append(QLatin1Char('&'));
      ++i;
    }
  }
  return out;
}

QString shortcutText(const QAction &action) {
  const QKeySequence sequence = action.shortcut();
  if (sequence.isEmpty()) return {};
  return sequence.toString(QKeySequence::NativeText);
}

QString displayText(const QString &label, const QString &shortcut) {
  if (shortcut.isEmpty()) return label;
  return label + QStringLiteral("  ") + shortcut;
}

struct LoadedCommand {
  QString id;
  QString registeredLabel;
  int group = 0;
  int tieSurface = 0;
  int tieIndex = 0;
  QPointer<QAction> action;
};

// Group order is CommandGroup. Inside a group, each surface list is a
// subsequence of registration order. Merge those public lists. When two
// commands never share a surface, the earlier surface in kSurfaces wins.
QList<LoadedCommand> loadCommands(CommandRegistry &registry) {
  QHash<QString, LoadedCommand> byId;
  QList<QStringList> surfaceIds;
  surfaceIds.reserve(int(std::size(kSurfaces)));
  for (int surfaceIndex = 0; surfaceIndex < int(std::size(kSurfaces)); ++surfaceIndex) {
    const QStringList ids = registry.ids(kSurfaces[surfaceIndex]);
    surfaceIds.append(ids);
    for (int index = 0; index < ids.size(); ++index) {
      const QString &id = ids.at(index);
      if (byId.contains(id)) continue;
      QAction *action = registry.action(id);
      const CommandSpec *spec = registry.spec(id);
      if (!action || !spec) continue;
      LoadedCommand loaded;
      loaded.id = id;
      loaded.registeredLabel = withoutMnemonic(spec->label);
      loaded.group = static_cast<int>(spec->group);
      loaded.tieSurface = surfaceIndex;
      loaded.tieIndex = index;
      loaded.action = action;
      byId.insert(id, loaded);
    }
  }

  QHash<QString, QStringList> successors;
  QHash<QString, int> indegree;
  QSet<QString> edges;
  for (const auto &id : byId.keys()) indegree.insert(id, 0);
  for (const QStringList &ids : surfaceIds) {
    QHash<int, QString> previousInGroup;
    for (const QString &id : ids) {
      const auto found = byId.constFind(id);
      if (found == byId.cend()) continue;
      const QString previous = previousInGroup.value(found->group);
      previousInGroup.insert(found->group, id);
      if (previous.isEmpty() || previous == id) continue;
      const QString edge = previous + QChar(0x1f) + id;
      if (edges.contains(edge)) continue;
      edges.insert(edge);
      successors[previous].append(id);
      indegree[id] = indegree.value(id) + 1;
    }
  }

  const auto before = [&byId](const QString &left, const QString &right) {
    const LoadedCommand a = byId.value(left);
    const LoadedCommand b = byId.value(right);
    if (a.group != b.group) return a.group < b.group;
    if (a.tieSurface != b.tieSurface) return a.tieSurface < b.tieSurface;
    if (a.tieIndex != b.tieIndex) return a.tieIndex < b.tieIndex;
    return left < right;
  };

  QStringList ready;
  for (auto it = indegree.cbegin(); it != indegree.cend(); ++it)
    if (it.value() == 0) ready.append(it.key());

  QList<LoadedCommand> ordered;
  QSet<QString> emitted;
  while (!ready.isEmpty()) {
    int best = 0;
    for (int i = 1; i < ready.size(); ++i)
      if (before(ready.at(i), ready.at(best))) best = i;
    const QString id = ready.takeAt(best);
    if (emitted.contains(id)) continue;
    emitted.insert(id);
    ordered.append(byId.value(id));
    for (const QString &next : successors.value(id)) {
      const int remaining = indegree.value(next) - 1;
      indegree.insert(next, remaining);
      if (remaining == 0) ready.append(next);
    }
  }

  QStringList leftover;
  for (const QString &id : byId.keys())
    if (!emitted.contains(id)) leftover.append(id);
  std::sort(leftover.begin(), leftover.end(), before);
  for (const QString &id : leftover) ordered.append(byId.value(id));
  return ordered;
}

}  // namespace

CommandPalette::CommandPalette(CommandRegistry &registry, QWidget *parent)
    : QDialog(parent), m_registry(&registry), m_owner(parent), m_hadOwner(parent != nullptr) {
  setObjectName(QStringLiteral("commandPalette"));
  setWindowModality(Qt::ApplicationModal);
  setMinimumSize(360, 280);
  resize(520, 440);
  bindNativeText(*this, "windowTitle", nativeText<CommandPalette>(QT_TR_NOOP("Command palette")));
  bindNativeText(*this, "accessibleName", nativeText<CommandPalette>(QT_TR_NOOP("Command palette")));
  bindNativeText(*this, "accessibleDescription", nativeText<CommandPalette>(QT_TR_NOOP(
      "Search registered commands and run one shared action. Unavailable commands stay listed and do not run.")));

  auto *layout = new QVBoxLayout(this);
  m_search = new QLineEdit(this);
  m_search->setObjectName(QStringLiteral("commandPaletteSearch"));
  m_search->setClearButtonEnabled(true);
  m_search->setFocusPolicy(Qt::StrongFocus);
  bindNativeText(*m_search, "accessibleName", nativeText<CommandPalette>(QT_TR_NOOP("Search commands")));
  bindNativeText(*m_search, "placeholderText", nativeText<CommandPalette>(QT_TR_NOOP("Search commands")));
  m_search->installEventFilter(this);
  layout->addWidget(m_search);

  m_commands = new QListWidget(this);
  m_commands->setObjectName(QStringLiteral("commandPaletteCommands"));
  m_commands->setSelectionMode(QAbstractItemView::SingleSelection);
  m_commands->setUniformItemSizes(true);
  m_commands->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  bindNativeText(*m_commands, "accessibleName", nativeText<CommandPalette>(QT_TR_NOOP("Commands")));
  layout->addWidget(m_commands, 1);

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("commandPaletteStatus"));
  m_status->setTextFormat(Qt::PlainText);
  m_status->setWordWrap(true);
  m_status->setFocusPolicy(Qt::NoFocus);
  bindNativeText(*m_status, "accessibleName", nativeText<CommandPalette>(QT_TR_NOOP("Command status")));
  layout->addWidget(m_status);

  m_run = new QPushButton(this);
  m_run->setObjectName(QStringLiteral("runCommand"));
  m_run->setAutoDefault(false);
  m_run->setDefault(false);
  bindNativeText(*m_run, "text", nativeText<CommandPalette>(QT_TR_NOOP("Run")));
  bindNativeText(*m_run, "accessibleName", nativeText<CommandPalette>(QT_TR_NOOP("Run command")));
  layout->addWidget(m_run, 0, Qt::AlignRight);

  connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
  connect(m_search, &QLineEdit::returnPressed, this, &CommandPalette::accept);
  connect(m_commands, &QListWidget::currentItemChanged, this, [this] { explainSelection(); });
  connect(m_commands, &QListWidget::itemActivated, this, &CommandPalette::accept);
  connect(m_run, &QPushButton::clicked, this, &CommandPalette::accept);
  connect(&registry, &QObject::destroyed, this, [this] {
    cancelDispatch();
    clearRows();
    setStatusText(tr("Commands are no longer available."), true);
    m_run->setEnabled(false);
  });
  reload();
}

CommandPalette::~CommandPalette() { cancelDispatch(); }

void CommandPalette::showEvent(QShowEvent *event) {
  // Apply modality before the window is mapped so Qt records this show as modal.
  if (!event->spontaneous()) setWindowModality(Qt::ApplicationModal);
  QDialog::showEvent(event);
  if (event->spontaneous()) return;
  cancelDispatch();
  {
    const QSignalBlocker blocker(m_search);
    m_search->clear();
  }
  reload();
  m_search->setFocus(Qt::OtherFocusReason);
}

void CommandPalette::changeEvent(QEvent *event) {
  QDialog::changeEvent(event);
  if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange
      || event->type() == QEvent::StyleChange) {
    for (const CommandRow &row : m_rows) refreshRow(row.id);
    explainSelection();
  }
}

void CommandPalette::keyPressEvent(QKeyEvent *event) {
  if (event->matches(QKeySequence::Cancel)) {
    reject();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    accept();
    return;
  }
  QDialog::keyPressEvent(event);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_search && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Down) {
      moveSelection(1);
      return true;
    }
    if (key->key() == Qt::Key_Up) {
      moveSelection(-1);
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      accept();
      return true;
    }
    if (key->matches(QKeySequence::Cancel)) {
      reject();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void CommandPalette::clearRows() {
  m_reloading = true;
  for (const CommandRow &row : m_rows) {
    if (row.action) QObject::disconnect(row.action, nullptr, this, nullptr);
  }
  m_rows.clear();
  if (m_commands) m_commands->clear();
  m_reloading = false;
}

void CommandPalette::reload() {
  clearRows();
  if (!m_registry) {
    setStatusText(tr("Commands are no longer available."), true);
    m_run->setEnabled(false);
    return;
  }
  const QSignalBlocker blocker(m_commands);
  const QList<LoadedCommand> loaded = loadCommands(*m_registry);
  m_reloading = true;
  m_rows.reserve(loaded.size());
  for (const LoadedCommand &entry : loaded) {
    if (!entry.action) continue;
    CommandRow row;
    row.id = entry.id;
    row.registeredLabel = entry.registeredLabel;
    row.action = entry.action;
    row.item = new QListWidgetItem(m_commands);
    row.item->setData(CommandIdRole, row.id);
    m_rows.append(row);
    connectRow(m_rows.last());
    refreshRow(row.id);
  }
  m_reloading = false;
  applyFilter();
}

void CommandPalette::connectRow(CommandRow &row) {
  if (!row.action) return;
  const QString id = row.id;
  connect(row.action, &QAction::changed, this, [this, id] {
    if (!m_reloading) refreshRow(id);
  });
  connect(row.action, &QObject::destroyed, this, [this, id] {
    if (!m_reloading) dropRow(id);
  });
}

CommandPalette::CommandRow *CommandPalette::rowFor(const QString &id) {
  for (CommandRow &row : m_rows)
    if (row.id == id) return &row;
  return nullptr;
}

const CommandPalette::CommandRow *CommandPalette::rowFor(const QString &id) const {
  for (const CommandRow &row : m_rows)
    if (row.id == id) return &row;
  return nullptr;
}

QString CommandPalette::labelFor(const CommandRow &row) const {
  QString label = row.action ? withoutMnemonic(row.action->text()) : QString();
  if (label.isEmpty()) label = row.registeredLabel;
  if (label.isEmpty()) label = row.id;
  return label;
}

QString CommandPalette::unavailableMessage(const QAction &action, const QString &label) const {
  QString message = tr("\"%1\" is unavailable.").arg(label);
  const QString reason = action.toolTip().trimmed();
  if (!reason.isEmpty() && reason != label) message += QLatin1Char(' ') + reason;
  return message;
}

void CommandPalette::refreshRow(const QString &id) {
  CommandRow *row = rowFor(id);
  if (!row || !row->item) return;
  if (m_registry) {
    if (const CommandSpec *spec = m_registry->spec(id)) row->registeredLabel = withoutMnemonic(spec->label);
  }
  const QString label = labelFor(*row);
  const QString shortcut = row->action ? shortcutText(*row->action) : QString();
  const bool enabled = row->action && row->action->isEnabled();
  row->item->setText(displayText(label, shortcut));
  row->item->setData(CommandIdRole, row->id);
  row->item->setData(CommandLabelRole, label);
  row->item->setData(CommandShortcutRole, shortcut);
  if (enabled) row->item->setData(Qt::ForegroundRole, QVariant());
  else if (m_commands) row->item->setForeground(m_commands->palette().color(QPalette::Disabled, QPalette::Text));
  const QString description = row->action
      ? (enabled ? row->action->statusTip() : unavailableMessage(*row->action, label))
      : tr("\"%1\" is unavailable.").arg(label);
  row->item->setToolTip(description);
  row->item->setData(Qt::AccessibleDescriptionRole, description);
  if (!m_reloading) applyFilter();
}

void CommandPalette::dropRow(const QString &id) {
  for (int index = 0; index < m_rows.size(); ++index) {
    if (m_rows.at(index).id != id) continue;
    delete m_commands->takeItem(index);
    m_rows.removeAt(index);
    break;
  }
  applyFilter();
}

void CommandPalette::applyFilter() {
  if (!m_commands) return;
  const QString query = m_search ? m_search->text().trimmed() : QString();
  QString selectedId;
  if (QListWidgetItem *current = m_commands->currentItem()) selectedId = current->data(CommandIdRole).toString();
  int firstVisible = -1;
  int selectedVisible = -1;
  for (int index = 0; index < m_rows.size(); ++index) {
    const CommandRow &row = m_rows.at(index);
    const QString label = labelFor(row);
    const bool shown = row.action && row.action->isVisible()
        && (query.isEmpty() || label.contains(query, Qt::CaseInsensitive)
            || row.registeredLabel.contains(query, Qt::CaseInsensitive)
            || row.id.contains(query, Qt::CaseInsensitive));
    m_commands->setRowHidden(index, !shown);
    if (!shown) continue;
    if (firstVisible < 0) firstVisible = index;
    if (row.id == selectedId) selectedVisible = index;
  }
  const QSignalBlocker blocker(m_commands);
  if (selectedVisible >= 0) m_commands->setCurrentRow(selectedVisible);
  else if (firstVisible >= 0) m_commands->setCurrentRow(firstVisible);
  else m_commands->setCurrentRow(-1);
  explainSelection();
}

void CommandPalette::moveSelection(int delta) {
  if (!m_commands || delta == 0) return;
  const int count = m_commands->count();
  if (count == 0) return;
  int row = m_commands->currentRow();
  if (row < 0) row = delta > 0 ? -1 : count;
  for (int step = 0; step < count; ++step) {
    row += delta;
    if (row < 0 || row >= count) return;
    if (!m_commands->isRowHidden(row)) {
      m_commands->setCurrentRow(row);
      if (QListWidgetItem *item = m_commands->item(row)) m_commands->scrollToItem(item);
      return;
    }
  }
}

CommandPalette::CommandRow *CommandPalette::selectedRow() {
  if (!m_commands) return nullptr;
  QListWidgetItem *current = m_commands->currentItem();
  if (!current) return nullptr;
  const int row = m_commands->row(current);
  if (row < 0 || row >= m_rows.size() || m_commands->isRowHidden(row)) return nullptr;
  return &m_rows[row];
}

bool CommandPalette::runnable(const QAction *action) const {
  return action && action->isEnabled() && action->isVisible();
}

void CommandPalette::setStatusText(const QString &message, bool alert) {
  if (!m_status) return;
  if (m_run) m_run->setAccessibleDescription(m_run->isEnabled() ? tr("Run the selected command") : message);
  if (m_status->text() == message && m_status->accessibleDescription() == message) return;
  bindNativeText(*m_status, "text", NativeText::literal(message));
  m_status->setAccessibleDescription(message);
  raiseAccessibleCue(m_status, alert ? QAccessible::Alert : QAccessible::DescriptionChanged);
}

void CommandPalette::explainSelection() {
  if (!m_run || !m_status) return;
  if (!m_registry) {
    m_run->setEnabled(false);
    setStatusText(tr("Commands are no longer available."), true);
    return;
  }
  int visibleCount = 0;
  bool anyAction = false;
  for (int index = 0; index < m_rows.size(); ++index) {
    if (m_rows.at(index).action) anyAction = true;
    if (m_commands && !m_commands->isRowHidden(index)) ++visibleCount;
  }
  const CommandRow *selected = selectedRow();
  if (!selected || !selected->action) {
    m_run->setEnabled(false);
    const QString query = m_search ? m_search->text().trimmed() : QString();
    if (!anyAction) setStatusText(tr("No commands are registered."), true);
    else if (!query.isEmpty() && visibleCount == 0) setStatusText(tr("No commands match \"%1\".").arg(query), true);
    else if (visibleCount == 0) setStatusText(tr("No commands are available."), true);
    else setStatusText(tr("Select a command."), false);
    return;
  }
  const QString label = labelFor(*selected);
  if (!runnable(selected->action)) {
    m_run->setEnabled(false);
    setStatusText(unavailableMessage(*selected->action, label), true);
    return;
  }
  m_run->setEnabled(true);
  const QString tip = selected->action->statusTip().trimmed();
  const QString tooltip = selected->action->toolTip().trimmed();
  if (!tip.isEmpty()) setStatusText(tip, false);
  else if (!tooltip.isEmpty()) setStatusText(tooltip, false);
  else setStatusText(tr("Press Enter to run \"%1\".").arg(label), false);
}

void CommandPalette::cancelDispatch() {
  m_armed = false;
  m_pendingAction.clear();
  m_pendingId.clear();
  ++m_ticket;
}

bool CommandPalette::modalityReleased() const {
  // Hidden is not enough: the dialog must be off Qt's modal window stack.
  if (isVisible() || QApplication::activeModalWidget() == this) return false;
  return !isModal() && windowModality() == Qt::NonModal && !testAttribute(Qt::WA_ShowModal);
}

void CommandPalette::accept() {
  if (m_armed || m_allowDone) return;
  CommandRow *selected = selectedRow();
  if (!selected || !selected->action || !m_registry) {
    explainSelection();
    return;
  }
  if (!runnable(selected->action)) {
    explainSelection();
    return;
  }
  if (m_registry->action(selected->id) != selected->action.data()) {
    setStatusText(tr("The command is no longer available."), true);
    return;
  }

  m_pendingId = selected->id;
  m_pendingAction = selected->action;
  m_armed = true;
  const quint64 ticket = ++m_ticket;
  QPointer<CommandPalette> self(this);
  // Hide while the palette is still modal. Qt only removes a window from the
  // modal stack on hide when its modality is still set. Clearing modality
  // first leaves this dialog as the active modal widget.
  m_allowDone = true;
  if (!finishDialog(Accepted)) return;
  if (!self) return;
  m_allowDone = false;
  setWindowModality(Qt::NonModal);
  setAttribute(Qt::WA_ShowModal, false);
  QTimer::singleShot(0, self.data(), [self, ticket] {
    if (self) self->dispatch(ticket);
  });
}

bool CommandPalette::finishDialog(int code) {
  // Qt 6.4 emits finished after accepted/rejected without guarding observer deletion.
  // Let Qt close its modal loop, then deliver the same signals with a lifetime guard.
  QPointer<CommandPalette> self(this);
  const bool blocked = blockSignals(true);
  QDialog::done(code);
  if (!self) return false;
  blockSignals(blocked);
  if (!blocked) {
    if (code == Accepted) emit accepted();
    else emit rejected();
    if (!self) return false;
    emit finished(code);
  }
  return !self.isNull();
}

void CommandPalette::reject() {
  cancelDispatch();
  if (!isVisible() && result() == Rejected) return;
  setStatusText(tr("No command was started."), true);
  m_allowDone = true;
  if (finishDialog(Rejected)) m_allowDone = false;
}

void CommandPalette::dispatch(quint64 ticket) {
  if (!m_armed || ticket != m_ticket) return;
  const QString id = m_pendingId;
  QPointer<QAction> action = m_pendingAction;
  m_armed = false;
  m_pendingAction.clear();
  m_pendingId.clear();

  if (m_hadOwner && m_owner.isNull()) {
    setStatusText(tr("The command is no longer available."), true);
    return;
  }
  // exec() restores WA_ShowModal after its loop returns. Clear that again
  // while the palette is hidden, then refuse to invoke if it is still modal.
  if (isVisible()) hide();
  setWindowModality(Qt::NonModal);
  setAttribute(Qt::WA_ShowModal, false);
  if (!modalityReleased()) {
    setStatusText(tr("The command was not started."), true);
    return;
  }
  if (!action || !m_registry) {
    setStatusText(tr("The command is no longer available."), true);
    return;
  }
  // Adopted actions outlive the registry entry only as a dangling registry
  // pointer. Touch the action only through the QPointer checked above.
  if (m_registry->action(id) != action.data() || !action->isEnabled() || !action->isVisible()) {
    const QString label = withoutMnemonic(action->text());
    setStatusText(unavailableMessage(*action, label.isEmpty() ? id : label), true);
    return;
  }
  QPointer<CommandPalette> self(this);
  action->trigger();
  if (!self) return;
}

}  // namespace xerahs::app
