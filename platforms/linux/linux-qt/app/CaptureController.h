// Region capture flow for the linux-qt app: portal snapshot, overlays driven
// by RegionSession, pixel acquisition through RegionGeometry::composite, and
// the post-capture pipeline. Every decision lives in the src/ libraries; this
// class wires them to Qt.
#pragma once

#include "capture/RegionSession.h"

#include <QImage>
#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>
#include <vector>

class QScreen;

namespace xerahs::app {

class PortalScreenshot;
class RegionOverlay;

class CaptureController final : public QObject {
  Q_OBJECT

public:
  explicit CaptureController(QString saveDirectory, QObject *parent = nullptr);
  ~CaptureController() override;

  static QString instructions();
  QString saveDirectory() const { return m_saveDirectory; }
  bool busy() const { return m_busy; }

  // Overlay input, in desktop physical pixels.
  void pointerDown(xerahs::capture::PhysicalPoint point);
  void pointerMove(xerahs::capture::PhysicalPoint point);
  void pointerUp(xerahs::capture::PhysicalPoint point);
  bool key(int key, Qt::KeyboardModifiers modifiers, const xerahs::capture::Display &display);
  void cancel();

  // Overlay painting state.
  std::optional<xerahs::capture::PhysicalRect> selection() const;
  bool selectionSettled() const;
  std::optional<xerahs::capture::PhysicalPoint> keyboardCursor() const;
  QScreen *hintScreen() const;

public slots:
  void captureRegion();

signals:
  void busyChanged(bool busy);
  // CORE-010: the originating operation and its true result.
  void notify(const QString &title, const QString &body, bool success);

private:
  void onSnapshot(int outcome, const QImage &image, const QString &detail);
  void onOutcome(const xerahs::capture::SessionOutcome &outcome);
  void afterInput();
  void acquire();
  void runPipeline(const xerahs::capture::PhysicalRect &rect);
  void closeOverlays();
  void finish();
  void repaint();
  void announce();

  QString m_saveDirectory;
  bool m_busy = false;
  QPointer<PortalScreenshot> m_portal;
  QImage m_desktop;  // frozen snapshot in desktop physical pixels
  std::unique_ptr<xerahs::capture::RegionSession> m_session;
  std::vector<std::unique_ptr<RegionOverlay>> m_overlays;
  std::optional<xerahs::capture::PhysicalPoint> m_keyboardCursor;
  bool m_keyboardSelecting = false;
  std::optional<QImage> m_captured;
  std::vector<QMetaObject::Connection> m_screenConnections;
};

}  // namespace xerahs::app
