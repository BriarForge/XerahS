#include "EditorShutdown.h"
#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QVariant>
#include <QWidget>
#include <QSessionManager>
namespace xerahs::app {
namespace {
bool shutdownBlockedByModalWork() {
  const auto *modal = QApplication::activeModalWidget();
  // The task editor itself owns a close policy. Its nested confirmation,
  // picker or worker dialog must finish before shutdown can enter that policy.
  return modal && !modal->property("xerahsGuardEditorClose").toBool();
}
}
QString closeEditorsForShutdown() {
  static bool closing = false;
  if (closing || shutdownBlockedByModalWork()) return QStringLiteral("application-busy");
  const QScopedValueRollback<bool> guard(closing, true);
  const bool autoQuit = QApplication::quitOnLastWindowClosed();
  QApplication::setQuitOnLastWindowClosed(false);
  const auto restore = qScopeGuard([autoQuit] { QApplication::setQuitOnLastWindowClosed(autoQuit); });
  QList<QPointer<QWidget>> editors;
  for (auto *window : QApplication::topLevelWidgets())
    if (window->property("xerahsGuardEditorClose").toBool()) editors.append(window);
  if (auto *modal = QApplication::activeModalWidget()) {
    editors.removeAll(modal); editors.prepend(modal);
  }
  for (const auto &editor : editors) {
    if (shutdownBlockedByModalWork()) return QStringLiteral("application-busy");
    if (editor && !editor->close()) return QStringLiteral("editor-close-cancelled");
  }
  return shutdownBlockedByModalWork() ? QStringLiteral("application-busy") : QString{};
}
namespace {
class EditorQuitGuard final : public QObject {
public:
  explicit EditorQuitGuard(QObject *parent) : QObject(parent) {
    connect(qApp, &QGuiApplication::commitDataRequest, this, [](QSessionManager &manager) {
      const bool interactive = manager.allowsInteraction();
      const auto error = commitEditorsForSession(interactive);
      if (interactive) manager.release();
      if (!error.isEmpty()) manager.cancel();
    }, Qt::DirectConnection);
  }
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() != QEvent::Quit) return false;
    if (closeEditorsForShutdown().isEmpty()) return false;
    event->ignore(); return true;
  }
};
}
QString commitEditorsForSession(bool interactionAllowed) {
  if (interactionAllowed) return closeEditorsForShutdown();
  for (auto *window : QApplication::topLevelWidgets())
    if (window->property("xerahsGuardEditorClose").toBool() && window->isVisible())
      return QStringLiteral("editor-session-interaction-required");
  return {};
}
void guardEditorShutdown(QWidget &editor) {
  editor.setProperty("xerahsGuardEditorClose", true);
  static QPointer<EditorQuitGuard> guard;
  if (!guard && qApp) { guard = new EditorQuitGuard(qApp); qApp->installEventFilter(guard); }
}
}
