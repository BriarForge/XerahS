// Fullscreen region-selection surface for one screen (RC-005). It shows the
// frozen snapshot, maps input to desktop physical pixels, and paints the
// session state owned by CaptureController. It holds no selection logic.
#pragma once

#include "capture/RegionGeometry.h"

#include <QImage>
#include <QWidget>

namespace xerahs::app {

class CaptureController;

class RegionOverlay final : public QWidget {
  Q_OBJECT

public:
  RegionOverlay(CaptureController &controller, QScreen *screen, xerahs::capture::Display display, QImage snapshot);

  const xerahs::capture::Display &display() const { return m_display; }
  // Announces the current bounds to assistive technology (RC-024).
  void announce(const QString &text);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  xerahs::capture::PhysicalPoint toPhysical(const QPointF &widgetPoint) const;
  QRectF toWidget(const xerahs::capture::PhysicalRect &rect) const;
  QPointF toWidget(xerahs::capture::PhysicalPoint point) const;
  void paintHint(QPainter &painter);
  void paintBadge(QPainter &painter, const QRectF &selection, const xerahs::capture::PhysicalRect &physical);

  CaptureController &m_controller;
  xerahs::capture::Display m_display;
  QImage m_snapshot;  // this display's pixels, physical size
  QPointF m_screenOrigin;
};

}  // namespace xerahs::app
