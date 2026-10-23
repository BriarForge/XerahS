// POST-CAPTURE-ACTIONS-001 `pin`: a frameless, always-on-top window that keeps
// a private copy of the working image on screen. Each pin owns its image and
// deletes itself when closed.
#pragma once
#include <QImage>
#include <QWidget>

namespace xerahs::app {

class PinWindow final : public QWidget {
  Q_OBJECT
public:
  static constexpr double kMinZoom = 0.1, kMaxZoom = 4.0, kMinOpacity = 0.2, kMaxOpacity = 1.0;

  // Shows at 100%, or scaled down to fit the available area of `screen`
  // (never scaled up). A null screen uses the one under the pointer.
  explicit PinWindow(const QImage &image, QScreen *screen = nullptr);

  const QImage &image() const { return m_image; }
  double zoom() const { return m_zoom; }
  double pinOpacity() const { return m_opacity; }
  // Zoom around `anchor` (widget coordinates) so that point stays under the cursor.
  void setZoom(double zoom, const QPointF &anchor);
  void setPinOpacity(double opacity);
  bool copyToClipboard() const;
  // Encodes and writes on an OwnedWork worker; saveFinished reports the outcome.
  void saveTo(const QString &path);

signals:
  void saveFinished(bool ok, const QString &path);

protected:
  void paintEvent(QPaintEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void contextMenuEvent(QContextMenuEvent *) override;

private:
  void chooseSavePath();
  QImage m_image;
  double m_zoom = 1.0, m_opacity = 1.0;
  QPoint m_dragOffset;
  bool m_dragging = false;
};

}  // namespace xerahs::app
