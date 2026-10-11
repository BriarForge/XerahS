#include "NativeEditorWindowState.h"
#include "NativeEditorPreferences.h"
#include "image-editor/EditorWindowPlacement.h"
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <limits>

namespace xerahs::app {
using namespace xerahs::editor;
static_assert(maximumEditorWindowExtent == QWIDGETSIZE_MAX);
namespace {
QMarginsF frameMargins(const QWidget &window) {
  const QRect client = window.geometry(), frame = window.frameGeometry();
  return {std::max(0.0, double(client.left()) - frame.left()), std::max(0.0, double(client.top()) - frame.top()),
      std::max(0.0, double(frame.right()) - client.right()), std::max(0.0, double(frame.bottom()) - client.bottom())};
}
}
NativeEditorWindowState::NativeEditorWindowState(QWidget &window, QSize fallback, DisplayReader displays)
    : QObject(&window), m_window(window), m_displays(std::move(displays)) {
  setObjectName(QStringLiteral("editorWindowState"));
  if (!m_displays) m_displays = [this] {
    NativeWindowDisplays result; const auto screens = QGuiApplication::screens();
    const int current = screens.indexOf(m_window.screen()); result.preferred = current < 0 ? 0 : current;
    for (auto *screen : screens) result.available.append(screen->availableGeometry());
    return result;
  };
  const auto preference = windowPreferencesFromPreferences(nativeEditorPreferences());
  const auto size = preference.remember ? preference.normalSize : QSizeF(fallback);
  const auto displaysNow = m_displays(); const double nan = std::numeric_limits<double>::quiet_NaN();
  if (const auto placement = recoverEditorWindowPlacement(QRectF(QPointF(nan, nan), size), displaysNow.available, displaysNow.preferred)) {
    m_initialBounds = placement->clientBounds; m_window.setGeometry(m_initialBounds.toRect());
  } else { m_window.resize(fallback); m_initialBounds = m_window.geometry(); }
  m_maximizedBeforeMinimize = preference.remember && preference.maximized;
  if (m_maximizedBeforeMinimize) m_window.setWindowState(Qt::WindowMaximized);
  m_window.installEventFilter(this);
  for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
  connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watchScreen(screen); scheduleRecovery(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) { m_screens.remove(screen); scheduleRecovery(); });
}
void NativeEditorWindowState::watchScreen(QScreen *screen) {
  if (!screen || m_screens.contains(screen)) return;
  m_screens.insert(screen);
  connect(screen, &QScreen::geometryChanged, this, [this] { scheduleRecovery(); });
  connect(screen, &QScreen::availableGeometryChanged, this, [this] { scheduleRecovery(); });
  connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { scheduleRecovery(); });
  connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] { scheduleRecovery(); });
  connect(screen, &QObject::destroyed, this, [this, screen] { m_screens.remove(screen); });
}
void NativeEditorWindowState::scheduleRecovery() {
  if (m_queued) return;
  m_queued = true;
  QTimer::singleShot(0, this, [this] { m_queued = false; recoverPlacement(); });
}
bool NativeEditorWindowState::eventFilter(QObject *object, QEvent *event) {
  if (object == &m_window) {
    if (event->type() == QEvent::WindowStateChange && !m_window.isMinimized()) m_maximizedBeforeMinimize = m_window.isMaximized();
    if (!m_window.isMaximized() && !m_window.isMinimized() &&
        (event->type() == QEvent::Show || event->type() == QEvent::Resize || event->type() == QEvent::Move))
      m_initialBounds = m_window.geometry();
  }
  if (object == &m_window && (event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange)) {
    if (auto *handle = m_window.windowHandle(); handle && handle != m_handle) {
      m_handle = handle;
      connect(handle, &QWindow::screenChanged, this, [this] { scheduleRecovery(); });
    }
    scheduleRecovery();
  }
  return QObject::eventFilter(object, event);
}
void NativeEditorWindowState::recoverPlacement() {
  // The compositor controls maximized/minimized geometry. Restoration to the
  // normal state queues another recovery against the then-current work areas.
  if (m_window.isMaximized() || m_window.isMinimized()) return;
  const auto displays = m_displays();
  const auto placement = recoverEditorWindowPlacement(m_window.geometry(), displays.available, displays.preferred,
      frameMargins(m_window));
  if (placement && placement->clientBounds.toRect() != m_window.geometry()) m_window.setGeometry(placement->clientBounds.toRect());
}
QJsonObject NativeEditorWindowState::snapshot() const {
  QRectF normal = m_window.isMaximized() || m_window.isMinimized() ? QRectF(m_window.normalGeometry()) : QRectF(m_window.geometry());
  if (normal.isEmpty()) normal = m_initialBounds;
  const auto displays = m_displays();
  const auto placement = recoverEditorWindowPlacement(normal, displays.available, displays.preferred, frameMargins(m_window));
  if (!placement) return {};
  return {{QStringLiteral("WindowWidth"), placement->clientBounds.width()}, {QStringLiteral("WindowHeight"), placement->clientBounds.height()},
      {QStringLiteral("IsWindowMaximized"), m_window.isMinimized() ? m_maximizedBeforeMinimize : m_window.isMaximized()}};
}
QString NativeEditorWindowState::remember() { return rememberEditorWindowState(snapshot()); }
}  // namespace xerahs::app
