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
#include <QTextEdit>
#include <QToolBar>
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
    : QObject(&window), m_window(window), m_toolbar(toolbar), m_actions(std::move(actions)), m_ready(std::move(ready)) {
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
  for (auto *shortcut : m_shortcuts) { shortcut->setEnabled(false); shortcut->deleteLater(); }
  m_shortcuts.clear(); m_gestures.clear();
  m_toolbar.clear(); qDeleteAll(m_separators); m_separators.clear();
  for (auto *action : m_actions)
    for (const auto &key : action->shortcuts()) if (!key.isEmpty()) m_gestures.insert(canonical(key));
  bool haveVisible = false, pendingGroup = false;
  for (const auto &placement : effectiveToolbarItems(nativeEditorPreferences().values.value(u"ToolbarItems").toArray())) {
    auto *action = m_actions.value(placement.id); if (!action) continue;
    pendingGroup = pendingGroup || placement.beginGroup;
    if (placement.visible) {
      if (pendingGroup && haveVisible) m_separators.append(m_toolbar.addSeparator());
      m_toolbar.addAction(action); haveVisible = true; pendingGroup = false;
    }
    if (placement.hotkey.text.isEmpty()) continue;
    m_gestures.insert(placement.hotkey.text);
    bool alreadyBound = false;
    for (const auto &key : action->shortcuts()) if (canonical(key) == placement.hotkey.text) alreadyBound = true;
    if (alreadyBound) continue;
    auto *shortcut = new QShortcut(QKeySequence(QKeyCombination(placement.hotkey.modifiers, placement.hotkey.key)), &m_window);
    shortcut->setObjectName(QStringLiteral("toolbarHotkey:") + placement.id);
    shortcut->setContext(Qt::WindowShortcut);
    shortcut->setAutoRepeat(action->autoRepeat());
    connect(shortcut, &QShortcut::activated, this, [this, action, gesture = placement.hotkey.text] {
      if (canDispatch(gesture) && action->isEnabled()) action->trigger();
    });
    m_shortcuts.append(shortcut);
  }
}
} // namespace xerahs::app
