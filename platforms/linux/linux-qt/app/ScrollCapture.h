// CORE-015: scrolling capture. The target is a region chosen with the normal
// overlay; the scroll strategy is portal wheel input or manual scrolling; every
// frame goes through ScrollStitcher, which refuses uncertain joins. Collaborators
// are injected so the state machine runs without a desktop.
#pragma once

#include "OwnedWork.h"
#include "capture/ScrollStitch.h"

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QVector>

#include <functional>
#include <memory>

class QTimer;

namespace xerahs::app {

class PortalRemoteDesktop;

enum class ScrollChoice { SavePartial, SaveFrames, Discard };

struct ScrollOutcome {
  xerahs::capture::ScrollStatus status = xerahs::capture::ScrollStatus::Continue;
  QString code;            // stable kebab-case reason
  QString message;         // plain English
  QImage image;            // stitched part (accepted joins only)
  QVector<QImage> frames;  // every grabbed frame
  bool seamless = false;
};

// What the user sees. The real one is a dialog; tests drive a fake.
class ScrollCaptureView : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void open(bool manual) = 0;                       // show progress, Stop, Cancel (and Grab when manual)
  virtual void setProgress(int frames, int height, const QString &text) = 0;
  virtual void setManual(bool manual) = 0;
  virtual void hideForGrab(bool hide) = 0;                  // keep the window out of the captured pixels
  virtual void decide(const ScrollOutcome &outcome, std::function<void(ScrollChoice)> chosen) = 0;
  virtual void close() = 0;
signals:
  void grabRequested();    // manual strategy: capture the current view
  void stopRequested();    // keep what is stitched and finish
  void cancelRequested();  // abandon
  void manualRequested();  // switch from wheel input to manual scrolling
};

struct ScrollCapturePorts {
  // Normal overlay selection; the callback receives the region in desktop logical pixels.
  std::function<void(std::function<void(QRectF)>)> selectRegion;
  // CaptureController admission: false when another capture or dialog owns it.
  std::function<bool(std::function<void()> onCancel)> acquire;
  std::function<void()> release;
  std::function<bool(const QImage &)> submit;  // post-capture pipeline; releases admission
  std::function<PortalRemoteDesktop *(QObject *parent)> remote;  // null means no wheel input
  // Grabs the region once; returns a canceller. Empty diagnostic means success.
  std::function<std::function<void()>(const QRectF &, std::function<void(QImage, QString)>)> grab;
  std::function<void(const QVector<QImage> &, std::function<void(bool ok, QString where)>)> saveFrames;
  std::function<void(const QString &title, const QString &body, bool success)> notify;
  std::function<ScrollCaptureView *(QObject *parent)> view;
  std::function<QPointF()> desktopOrigin;  // logical top-left of the virtual desktop
};

struct ScrollCaptureSettings {
  xerahs::capture::ScrollStitchOptions stitch;
  int settleMs = 300;        // between scroll and grab
  int hideMs = 120;          // control window hidden this long before a grab
  int wheelSteps = 5;        // wheel detents per frame
  bool positionPointer = true;
};

class ScrollCapture final : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, SelectingTarget, ConnectingInput, Grabbing, Scrolling, Settling, WaitingManual, Stitching, Deciding };
  Q_ENUM(State)

  ScrollCapture(ScrollCapturePorts ports, ScrollCaptureSettings settings = {}, QObject *parent = nullptr);
  ~ScrollCapture() override;

  void start();
  void cancel();
  State state() const { return m_state; }
  bool active() const { return m_state != State::Idle; }
  bool manual() const { return m_manual; }
  int frames() const;
  void setSettings(const ScrollCaptureSettings &settings) { if (!active()) m_settings = settings; }
  const ScrollCaptureSettings &settings() const { return m_settings; }

  // Real collaborators for the running application.
  static ScrollCapturePorts nativePorts(QObject *owner, std::function<QString()> saveFolder);

signals:
  void stateChanged(xerahs::app::ScrollCapture::State state);

private:
  void targetSelected(const QRectF &region);
  void connectInput();
  void inputReady();
  void inputUnavailable(const QString &diagnostic);
  void useManual(const QString &why);
  void grabFrame();
  void frameGrabbed(QImage frame, const QString &diagnostic);
  void frameStitched(xerahs::capture::ScrollStatus status);
  void scrollNext();
  void requestStop();
  void armTimer(int ms, std::function<void()> action);
  void finish(xerahs::capture::ScrollStatus status, const QString &failure = {});
  void conclude(const QString &failure);
  void resolve(const ScrollOutcome &outcome);
  void end(bool release);
  void setState(State state);
  void report();

  ScrollCapturePorts m_ports;
  ScrollCaptureSettings m_settings;
  State m_state = State::Idle;
  quint64 m_generation = 0;
  bool m_manual = false, m_pointerPlaced = false, m_stopping = false;
  QRectF m_region;
  QPointer<ScrollCaptureView> m_view;
  QPointer<PortalRemoteDesktop> m_remote;
  std::shared_ptr<xerahs::capture::ScrollStitcher> m_stitcher;
  std::function<void()> m_cancelGrab;
  QTimer *m_timer = nullptr;
  OwnedWork m_work;
};

}  // namespace xerahs::app
