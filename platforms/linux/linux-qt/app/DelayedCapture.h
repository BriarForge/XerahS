// CORE-016: capture after a 1–60 s delay. The last delay is kept for this session.
#pragma once

#include "capture/CaptureSchedule.h"

#include <QObject>

#include <memory>

class QWidget;

namespace xerahs::app {

class DelayedCapture final : public QObject {
  Q_OBJECT
public:
  explicit DelayedCapture(QObject *parent = nullptr);
  DelayedCapture(xerahs::capture::Clock &clock, xerahs::capture::Timer &timer, QObject *parent = nullptr);
  ~DelayedCapture() override;

  bool prompt(QWidget *parent);
  QString start(const xerahs::capture::DelayRequest &request);
  void cancel();
  void raiseCountdown();
  void onTimer();
  bool delayActive() const;
  int rememberedSeconds() const { return m_seconds; }
  xerahs::capture::CaptureSource rememberedSource() const { return m_source; }
  void setRemembered(int seconds, xerahs::capture::CaptureSource source);
  QString statusText() const;

signals:
  void captureRequested(xerahs::capture::CaptureSource source);
  void statusTextChanged(const QString &text);

private:
  void remember(int seconds, xerahs::capture::CaptureSource source);
  void wire();
  void hideCountdown();

  std::unique_ptr<xerahs::capture::SystemClock> m_ownedClock;
  xerahs::capture::Timer *m_ownedTimer = nullptr;
  xerahs::capture::Clock *m_clock = nullptr;
  xerahs::capture::Timer *m_timer = nullptr;
  std::unique_ptr<xerahs::capture::CaptureSchedule> m_schedule;
  QWidget *m_countdown = nullptr;
  int m_seconds = 3;
  int m_fireGeneration = 0;
  xerahs::capture::CaptureSource m_source = xerahs::capture::CaptureSource::FullScreen;
};

}  // namespace xerahs::app
