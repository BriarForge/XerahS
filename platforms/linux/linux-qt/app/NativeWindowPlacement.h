// SETCAT-013: transient windows follow current logical work areas. This observer
// owns no settings and never serializes a QWidget, QWindow or display handle.
#pragma once
#include "image-editor/EditorWindowPlacement.h"
#include <QEvent>
#include <QApplication>
#include <QScrollArea>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <functional>

namespace xerahs::app {
class NativeWindowPlacement final : public QObject {
public:
  using DisplayReader = std::function<QList<QRectF>()>;
  explicit NativeWindowPlacement(QWidget &window, DisplayReader reader = {})
      : QObject(&window), m_window(window), m_reader(std::move(reader)) {
    setObjectName("nativeWindowPlacement");
    if (!m_reader) m_reader = [] {
      QList<QRectF> areas;
      for (auto *screen : QGuiApplication::screens()) areas.append(screen->availableGeometry());
      return areas;
    };
    window.installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *next) {
      if (!next || !m_window.isAncestorOf(next)) return;
      const QPointer<QWidget> target(next);
      QTimer::singleShot(0, this, [this, target] {
        if (!target || !m_window.isAncestorOf(target)) return;
        for (auto *ancestor = target->parentWidget(); ancestor && ancestor != &m_window; ancestor = ancestor->parentWidget())
          if (auto *scroll = qobject_cast<QScrollArea *>(ancestor)) scroll->ensureWidgetVisible(target);
      });
    });
    for (auto *screen : QGuiApplication::screens()) watch(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watch(screen); schedule(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { schedule(); });
  }
  void recover() {
    if (m_window.isMaximized() || m_window.isMinimized()) return;
    const auto client = m_window.geometry(), frame = m_window.frameGeometry();
    const QMarginsF margins{double(std::max(0, client.left() - frame.left())), double(std::max(0, client.top() - frame.top())),
        double(std::max(0, frame.right() - client.right())), double(std::max(0, frame.bottom() - client.bottom()))};
    const auto areas = m_reader();
    int preferred = 0;
    if (auto *screen = m_window.screen()) {
      const auto index = areas.indexOf(QRectF(screen->availableGeometry()));
      if (index >= 0) preferred = index;
    }
    if (const auto placement = editor::recoverEditorWindowPlacement(client, areas, preferred, margins, m_window.minimumSizeHint())) {
      const auto bounds = placement->clientBounds.toRect();
      const auto minimum = m_window.minimumSizeHint().expandedTo(m_window.minimumSize());
      if (!m_scroll && (minimum.width() > bounds.width() || minimum.height() > bounds.height())) makeScrollable();
      if (bounds != m_window.geometry()) m_window.setGeometry(bounds);
    }
  }
protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange || event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::LayoutRequest) {
      if (auto *handle = m_window.windowHandle(); handle && handle != m_handle) {
        m_handle = handle;
        connect(handle, &QWindow::screenChanged, this, [this] { schedule(); });
      }
      schedule();
    }
    return false;
  }
private:
  void makeScrollable() {
    QWidget *content = nullptr;
    auto *main = qobject_cast<QMainWindow *>(&m_window);
    if (main) content = main->takeCentralWidget();
    else if (auto *layout = m_window.layout()) { content = new QWidget; content->setLayout(layout); }
    if (!content) return;
    m_scroll = new QScrollArea(&m_window); m_scroll->setObjectName("adaptiveWindowContent");
    m_scroll->setAccessibleName("Scrollable window controls"); m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setFrameShape(QFrame::NoFrame); m_scroll->setWidgetResizable(true); m_scroll->setWidget(content);
    if (main) main->setCentralWidget(m_scroll);
    else { auto *outer = new QVBoxLayout(&m_window); outer->setContentsMargins(0,0,0,0); outer->addWidget(m_scroll); }
    m_window.setMinimumSize(0,0);
    if (m_window.layout()) m_window.layout()->activate();
  }
  void watch(QScreen *screen) {
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { schedule(); });
    connect(screen, &QScreen::geometryChanged, this, [this] { schedule(); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { schedule(); });
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] { schedule(); });
  }
  void schedule() {
    if (m_queued) return;
    m_queued = true;
    QTimer::singleShot(0, this, [this] { m_queued = false; recover(); });
  }
  QWidget &m_window;
  DisplayReader m_reader;
  QPointer<QWindow> m_handle;
  QPointer<QScrollArea> m_scroll;
  bool m_queued = false;
};
}
