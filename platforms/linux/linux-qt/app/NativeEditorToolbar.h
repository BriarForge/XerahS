#pragma once
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QSet>
#include <functional>
class QAction;
class QMainWindow;
class QShortcut;
class QToolBar;
class QWidget;
namespace xerahs::app {
using ToolbarActionRegistry = QMap<QString, QAction *>;
// Reuse the editor's actual commands. Visibility only changes toolbar placement;
// fixed menus and protected shortcuts stay available.
class NativeEditorToolbar final : public QObject {
public:
  NativeEditorToolbar(QMainWindow &window, QToolBar &toolbar, ToolbarActionRegistry actions,
                      std::function<bool()> ready = {});
  ~NativeEditorToolbar() override;
  void refresh();
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
private:
  bool canDispatch(const QString &gesture = {}) const;
  QMainWindow &m_window;
  QToolBar &m_toolbar;
  ToolbarActionRegistry m_actions;
  std::function<bool()> m_ready;
  QList<QShortcut *> m_shortcuts;
  QList<QAction *> m_separators;
  QSet<QString> m_gestures;
};
bool manageEditorToolbar(QWidget *parent, const ToolbarActionRegistry &actions);
} // namespace xerahs::app
