#pragma once
#include "NativeDesktopAppearance.h"
#include <QObject>
class QWidget;
namespace xerahs::app {
class NativeEditorAppearance final : public QObject {
  Q_OBJECT
public:
  explicit NativeEditorAppearance(QWidget &window, NativeDesktopAppearance &source = nativeDesktopAppearance());
  void refresh();
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
private:
  QWidget &m_window;
  NativeDesktopAppearance &m_source;
  bool m_queued = false;
};
}  // namespace xerahs::app
