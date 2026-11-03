#include "EditorShutdown.h"
#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QVariant>
#include <QWidget>
namespace xerahs::app {
namespace {
class EditorQuitGuard final : public QObject {
public:
  explicit EditorQuitGuard(QObject *parent) : QObject(parent) {}
  bool closing = false;
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() != QEvent::Quit) return false;
    if (closing) { event->ignore(); return true; }
    closing = true;
    const auto windows = QApplication::topLevelWidgets();
    QList<QPointer<QWidget>> editors;
    for (auto *window : windows)
      if (window->isVisible() && window->property("xerahsGuardEditorClose").toBool()) editors.append(window);
    for (const auto &editor : editors) if (editor && !editor->close()) {
      closing = false; event->ignore(); return true;
    }
    closing = false; return false;
  }
};
}
void guardEditorShutdown(QWidget &editor) {
  editor.setProperty("xerahsGuardEditorClose", true);
  static QPointer<EditorQuitGuard> guard;
  if (!guard && qApp) { guard = new EditorQuitGuard(qApp); qApp->installEventFilter(guard); }
}
}
