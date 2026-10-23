// CORE-016: automatic capture requires a count or an end time, and stays stoppable.
#pragma once

#include "capture/CaptureSchedule.h"

#include <QDialog>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QDateTimeEdit;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace xerahs::app {

class AutoCaptureDialog final : public QDialog {
  Q_OBJECT
public:
  static constexpr int StopResult = 2;
  explicit AutoCaptureDialog(std::function<qint64()> nowMs, bool running, QWidget *parent = nullptr);
  std::optional<xerahs::capture::AutoRequest> takeRequest();
  void accept() override;
private:
  void refresh();
  xerahs::capture::CaptureSource source() const;
  std::optional<xerahs::capture::ScheduleBound> bound() const;
  std::function<qint64()> m_nowMs;
  bool m_running = false;
  QComboBox *m_source = nullptr;
  QSpinBox *m_interval = nullptr;
  QRadioButton *m_countBound = nullptr;
  QRadioButton *m_timeBound = nullptr;
  QSpinBox *m_count = nullptr;
  QDateTimeEdit *m_end = nullptr;
  QLabel *m_status = nullptr;
  QPushButton *m_start = nullptr;
  QPushButton *m_stop = nullptr;
  std::optional<xerahs::capture::AutoRequest> m_request;
};

class AutoCaptureSession final : public QObject {
  Q_OBJECT
public:
  explicit AutoCaptureSession(QObject *parent = nullptr);
  AutoCaptureSession(xerahs::capture::Clock &clock, xerahs::capture::Timer &timer, QObject *parent = nullptr);
  bool prompt(QWidget *parent);
  QString start(const xerahs::capture::AutoRequest &request);
  void stop();
  void onTimer();
  bool active() const;
  void setBusy(bool busy);
  void finishAttempt(xerahs::capture::AttemptDisposition disposition);
  QString statusText() const;
signals:
  void captureRequested(xerahs::capture::CaptureSource source);
  void statusTextChanged(const QString &text);
  void runningChanged(bool running);
  void finished(xerahs::capture::AutoSummary summary);
private:
  void wire();
  void emitRunning();
  std::unique_ptr<xerahs::capture::SystemClock> m_ownedClock;
  xerahs::capture::Timer *m_ownedTimer = nullptr;
  xerahs::capture::Clock *m_clock = nullptr;
  xerahs::capture::Timer *m_timer = nullptr;
  std::unique_ptr<xerahs::capture::CaptureSchedule> m_schedule;
  bool m_running = false;
};

}  // namespace xerahs::app
