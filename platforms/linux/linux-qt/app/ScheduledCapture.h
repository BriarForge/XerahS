// CORE-016: delayed and automatic capture actions, attempt results and tray status.
#pragma once

#include "capture/CaptureSchedule.h"
#include "settings/ApplicationSettings.h"

#include <QObject>

#include <functional>

class QAction;
class QWidget;

namespace xerahs::app {

struct ScheduledCapturePorts {
  std::function<bool()> blocked;
  std::function<QString(xerahs::capture::CaptureSource)> start;
  std::function<void(const QString &title, const QString &body, bool ok)> notify;
  std::function<void(const QString &tooltip)> setTooltip;
};

class DelayedCapture;
class AutoCaptureSession;

// Owns the delay and the automatic session. Admission stays behind ports.start.
class ScheduledCapture final : public QObject {
  Q_OBJECT
public:
  explicit ScheduledCapture(ScheduledCapturePorts ports, QObject *parent = nullptr);
  ScheduledCapture(ScheduledCapturePorts ports, xerahs::capture::Clock &clock, xerahs::capture::Timer &timer, QObject *parent = nullptr);
  void setDialogParent(QWidget *parent);
  // Remember the delay and automatic capture choices in the application settings.
  void setPreferences(xerahs::settings::CapturePreferencesPort port);
  QAction *delayAction() const { return m_delayAction; }
  QAction *autoAction() const { return m_autoAction; }
  QAction *stopAction() const { return m_stopAction; }
  void onTimer();

public slots:
  void captureBusyChanged(bool busy);
  void captureFinished(bool success);

private:
  void wire();
  void promptDelay();
  void promptAuto();
  void refreshTooltip();
  void notify(const QString &title, const QString &body, bool ok);
  void onAutoRequested(xerahs::capture::CaptureSource source);
  void onDelayRequested(xerahs::capture::CaptureSource source);

  ScheduledCapturePorts m_ports;
  DelayedCapture *m_delay = nullptr;
  AutoCaptureSession *m_automatic = nullptr;
  QWidget *m_dialogs = nullptr;
  QAction *m_delayAction = nullptr;
  QAction *m_autoAction = nullptr;
  QAction *m_stopAction = nullptr;
  bool m_attempt = false;
  bool m_success = false;
};

}  // namespace xerahs::app
