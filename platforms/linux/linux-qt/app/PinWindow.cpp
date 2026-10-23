#include "PinWindow.h"
#include "OwnedWork.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFileDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QSaveFile>
#include <QScreen>
#include <QToolTip>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace xerahs::app {

PinWindow::PinWindow(const QImage &image, QScreen *screen) : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_image(image) {
  setObjectName(QStringLiteral("pinWindow")); setWindowTitle(QStringLiteral("Pinned image"));
  setAccessibleName(QStringLiteral("Pinned image. Mouse wheel zooms, Control and wheel changes opacity, Escape or double-click closes."));
  setAttribute(Qt::WA_DeleteOnClose); setAttribute(Qt::WA_QuitOnClose, false); setFocusPolicy(Qt::StrongFocus);
  if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen) screen = QGuiApplication::primaryScreen();
  const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
  if (!m_image.isNull()) {
    const double fit = std::min({1.0, double(area.width()) / m_image.width(), double(area.height()) / m_image.height()});
    m_zoom = std::clamp(fit, kMinZoom, kMaxZoom);
  }
  resize(std::max(1, int(std::lround(m_image.width() * m_zoom))), std::max(1, int(std::lround(m_image.height() * m_zoom))));
  // Cascade extra pins; a Wayland compositor may ignore the position.
  int others = 0; for (const auto *widget : QApplication::topLevelWidgets()) others += widget != this && widget->objectName() == QLatin1String("pinWindow");
  const QPoint step(24 * (others % 8), 24 * (others % 8));
  move(area.center() - QPoint(width() / 2, height() / 2) + step);
}

void PinWindow::setZoom(double zoom, const QPointF &anchor) {
  zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
  if (m_image.isNull() || zoom == m_zoom) return;
  const double ratio = zoom / m_zoom; m_zoom = zoom;
  const QSize size(std::max(1, int(std::lround(m_image.width() * zoom))), std::max(1, int(std::lround(m_image.height() * zoom))));
  const QPoint origin = pos() + (anchor - anchor * ratio).toPoint();
  setGeometry(QRect(origin, size)); update();
}

void PinWindow::setPinOpacity(double opacity) { m_opacity = std::clamp(opacity, kMinOpacity, kMaxOpacity); setWindowOpacity(m_opacity); }

bool PinWindow::copyToClipboard() const {
  QClipboard *clipboard = QGuiApplication::clipboard(); clipboard->setImage(m_image); return clipboard->ownsClipboard();
}

void PinWindow::saveTo(const QString &path) {
  OwnedWork::run<bool>(this, [image = m_image, path](const OwnedWork::Context &) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
  }, [this, path](bool ok) {
    if (!ok) QToolTip::showText(mapToGlobal(QPoint(8, 8)), QStringLiteral("Could not save the pinned image."), this);
    emit saveFinished(ok, path);
  });
}

void PinWindow::chooseSavePath() {
  auto *dialog = new QFileDialog(this, QStringLiteral("Save pinned image"), QString(), QStringLiteral("PNG image (*.png)"));
  dialog->setObjectName(QStringLiteral("pinSaveDialog")); dialog->setAcceptMode(QFileDialog::AcceptSave);
  dialog->setDefaultSuffix(QStringLiteral("png")); dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &path) { if (!path.isEmpty()) saveTo(path); });
  dialog->open();
}

void PinWindow::paintEvent(QPaintEvent *) {
  QPainter painter(this); painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom != 1.0);
  painter.drawImage(rect(), m_image);
}

void PinWindow::wheelEvent(QWheelEvent *event) {
  const int delta = event->angleDelta().y();
  if (delta == 0) { event->ignore(); return; }
  const double notches = delta / 120.0;
  if (event->modifiers() & Qt::ControlModifier) setPinOpacity(m_opacity + notches * 0.05);
  else setZoom(m_zoom * std::pow(1.1, notches), event->position());
  event->accept();
}

void PinWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) return;
  // The compositor owns the move where it can (Wayland); otherwise track manually.
  if (windowHandle() && windowHandle()->startSystemMove()) return;
  m_dragging = true; m_dragOffset = event->globalPosition().toPoint() - pos();
}
void PinWindow::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) move(event->globalPosition().toPoint() - m_dragOffset);
}
void PinWindow::mouseDoubleClickEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) close(); }

void PinWindow::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) close(); else QWidget::keyPressEvent(event);
}

void PinWindow::contextMenuEvent(QContextMenuEvent *event) {
  m_dragging = false;
  auto *menu = new QMenu(this); menu->setObjectName(QStringLiteral("pinMenu")); menu->setAttribute(Qt::WA_DeleteOnClose);
  connect(menu->addAction(QStringLiteral("&Copy")), &QAction::triggered, this, [this] { copyToClipboard(); });
  connect(menu->addAction(QStringLiteral("&Save as…")), &QAction::triggered, this, [this] { chooseSavePath(); });
  menu->addSeparator();
  connect(menu->addAction(QStringLiteral("C&lose")), &QAction::triggered, this, &QWidget::close);
  menu->popup(event->globalPos());
}

}  // namespace xerahs::app
