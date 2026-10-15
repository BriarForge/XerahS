#include "NativeEditorToolbar.h"
#include "NativeEditorPreferences.h"
#include "image-editor/ToolbarPreferences.h"
#include <QAction>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QScopeGuard>
#include <QTextEdit>
#include <QToolBar>
#include <QTimer>
namespace xerahs::app {
using namespace xerahs::editor;
namespace {
QString canonical(const QKeySequence &sequence) {
  auto spelling = sequence.toString(QKeySequence::PortableText);
  // Qt PortableText abbreviates Page keys (PgUp/PgDown); our persisted
  // catalogue uses PageUp/PageDown. Compare the physical key before policy
  // routing so a shortcut cannot consume native list navigation silently.
  if (sequence.count() == 1) {
    const auto key = sequence[0].key();
    if (key == Qt::Key_PageUp || key == Qt::Key_PageDown)
      spelling = spelling.left(spelling.lastIndexOf('+') + 1) + (key == Qt::Key_PageUp ? "PageUp" : "PageDown");
  }
  const auto value = parseToolbarHotkey(spelling);
  return value ? value->text : spelling;
}
bool textEntry(QWidget *focus) {
  if (!focus) return false;
  if (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
      qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus)) return true;
  const auto *combo = qobject_cast<QComboBox *>(focus); return combo && combo->isEditable();
}
}
NativeEditorToolbar::NativeEditorToolbar(QMainWindow &window, QToolBar &toolbar,
    ToolbarActionRegistry actions, std::function<bool()> ready)
    : QObject(&window), m_window(window), m_toolbar(toolbar), m_ready(std::move(ready)) {
  for (auto it = actions.cbegin(); it != actions.cend(); ++it) m_actions.insert(it.key(), it.value());
  setObjectName(QStringLiteral("nativeEditorToolbar"));
  connect(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::toolbarChanged, this, &NativeEditorToolbar::refresh);
  qApp->installEventFilter(this); refresh();
}
NativeEditorToolbar::~NativeEditorToolbar() {
  qApp->removeEventFilter(this);
  qDeleteAll(m_shortcuts);
}
bool NativeEditorToolbar::canDispatch(const QString &gesture) const {
  auto *focus = QApplication::focusWidget();
  if (gesture == "Home" || gesture == "End" || gesture == "PageUp" || gesture == "PageDown") {
    for (auto *widget = focus; widget && widget != &m_window; widget = widget->parentWidget())
      if (qobject_cast<QAbstractItemView *>(widget)) return false;
  }
  return !QApplication::activeModalWidget() && !QApplication::activePopupWidget() &&
      (!focus || focus == &m_window || m_window.isAncestorOf(focus)) &&
      !textEntry(focus) && (!m_ready || m_ready());
}
bool NativeEditorToolbar::eventFilter(QObject *object, QEvent *event) {
  if (event->type() != QEvent::ShortcutOverride) return false;
  auto *widget = qobject_cast<QWidget *>(object);
  if (!widget || (widget != &m_window && !m_window.isAncestorOf(widget))) return false;
  const auto *key = static_cast<QKeyEvent *>(event);
  const auto gesture = canonical(QKeySequence(key->keyCombination()));
  if (!canDispatch(gesture) && m_gestures.contains(gesture)) {
    event->accept(); return true;
  }
  return false;
}
void NativeEditorToolbar::refresh() {
  if (m_refreshing) { m_refreshPending = true; return; }
  const QPointer<NativeEditorToolbar> owner(this); m_refreshing = true;
  const auto restore = qScopeGuard([owner] {
    if (!owner) return;
    owner->m_refreshing = false;
    if (owner->m_refreshPending) {
      owner->m_refreshPending = false;
      QTimer::singleShot(0, owner, [owner] { if (owner) owner->refresh(); });
    }
  });
  const auto placements = effectiveToolbarItems(nativeEditorPreferences().values.value(u"ToolbarItems").toArray());
  const auto actions = m_actions; const auto shortcuts = m_shortcuts;
  m_shortcuts.clear(); m_gestures.clear();
  for (auto *shortcut : shortcuts) { shortcut->setEnabled(false); if (!owner) return; shortcut->deleteLater(); }
  m_toolbar.clear(); if (!owner) return;
  const auto separators = m_separators; m_separators.clear();
  for (auto *separator : separators) { delete separator; if (!owner) return; }
  for (const auto &action : actions) if (action)
    for (const auto &key : action->shortcuts()) if (!key.isEmpty()) m_gestures.insert(canonical(key));
  bool haveVisible = false, pendingGroup = false;
  for (const auto &placement : placements) {
    const auto action = actions.value(placement.id); if (!action) continue;
    pendingGroup = pendingGroup || placement.beginGroup;
    if (placement.visible) {
      if (pendingGroup && haveVisible) { auto *separator = m_toolbar.addSeparator(); if (!owner) return; m_separators.append(separator); }
      m_toolbar.addAction(action); if (!owner) return;
      haveVisible = true; pendingGroup = false;
    }
    if (!action || placement.hotkey.text.isEmpty()) continue;
    m_gestures.insert(placement.hotkey.text);
    bool alreadyBound = false;
    for (const auto &key : action->shortcuts()) if (canonical(key) == placement.hotkey.text) alreadyBound = true;
    if (alreadyBound) continue;
    auto *shortcut = new QShortcut(QKeySequence(QKeyCombination(placement.hotkey.modifiers, placement.hotkey.key)), &m_window);
    if (!owner) return;
    if (!action) { shortcut->deleteLater(); continue; }
    shortcut->setObjectName(QStringLiteral("toolbarHotkey:") + placement.id);
    shortcut->setContext(Qt::WindowShortcut);
    shortcut->setAutoRepeat(action->autoRepeat());
    connect(shortcut, &QShortcut::activated, this, [this, action, gesture = placement.hotkey.text] {
      if (action && canDispatch(gesture) && action->isEnabled()) action->trigger();
    });
    m_shortcuts.append(shortcut);
  }
}
} // namespace xerahs::app
