// Qt window/display I/O over the production ESET-005 placement model.
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <functional>
class QWidget;
class QWindow;
class QScreen;
namespace xerahs::app {
struct NativeWindowDisplays { QList<QRectF> available; int preferred = 0; };
class NativeEditorWindowState final : public QObject {
  Q_OBJECT
public:
  using DisplayReader = std::function<NativeWindowDisplays()>;
  NativeEditorWindowState(QWidget &window, QSize fallbackSize, DisplayReader displays = {});
  void recoverPlacement();
  QJsonObject snapshot() const;
  QString remember();
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
private:
  void watchScreen(QScreen *screen);
  void scheduleRecovery();
  QWidget &m_window;
  DisplayReader m_displays;
  QRectF m_initialBounds;
  bool m_maximizedBeforeMinimize = false;
  QSet<QScreen *> m_screens;
  QPointer<QWindow> m_handle;
  bool m_queued = false;
};
}  // namespace xerahs::app
