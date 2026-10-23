// CORE-016: one-shot delay and automatic capture. An automatic run cannot start
// without a count or an end time, and a busy capture skips that tick.
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <variant>

namespace xerahs::capture {

enum class CaptureSource { FullScreen, Monitor, ActiveWindow, Region };

inline constexpr int kMinDelaySeconds = 1;
inline constexpr int kMaxDelaySeconds = 60;
inline constexpr int kMinCaptureCount = 1;
inline constexpr int kMaxCaptureCount = 10000;
inline constexpr int kMinIntervalMs = 1000;
inline constexpr qint64 kMaxScheduleWindowMs = 24LL * 60 * 60 * 1000;
inline constexpr int kMaxIntervalMs = 24 * 60 * 60 * 1000;

QString captureSourceId(CaptureSource source);
std::optional<CaptureSource> captureSourceFromId(const QString &id);
bool isAutomaticCaptureSource(CaptureSource source);

struct CountBound { int count = 0; };
struct TimeBound { qint64 endMs = 0; };
using ScheduleBound = std::variant<CountBound, TimeBound>;

struct DelayRequest {
  CaptureSource source = CaptureSource::FullScreen;
  int seconds = 3;
};

struct AutoRequest {
  CaptureSource source = CaptureSource::FullScreen;
  int intervalMs = kMinIntervalMs;
  ScheduleBound bound = CountBound{1};
};

struct ScheduleDiagnostic { QString code; };

struct AutoSummary {
  int captured = 0;
  int skipped = 0;
  int failed = 0;
  bool stoppedByUser = false;
};

enum class AttemptDisposition { Captured, Skipped, Failed };

std::optional<ScheduleDiagnostic> validateDelay(const DelayRequest &request);
// bound is required. std::nullopt is rejected with "bound-required".
std::optional<ScheduleDiagnostic> validateAuto(CaptureSource source, int intervalMs, const std::optional<ScheduleBound> &bound, qint64 nowMs);
QString autoCaptureSummaryText(const AutoSummary &summary);
QString autoCaptureFinishedTitle(const AutoSummary &summary);

class Clock {
public:
  virtual ~Clock() = default;
  virtual qint64 nowMs() const = 0;
};

class SystemClock final : public Clock {
public:
  qint64 nowMs() const override;
};

class ManualClock final : public Clock {
public:
  explicit ManualClock(qint64 nowMs = 0) : m_now(nowMs) {}
  qint64 nowMs() const override { return m_now; }
  void setNow(qint64 nowMs) { m_now = nowMs; }
  void advance(qint64 ms) { m_now += ms; }
private:
  qint64 m_now = 0;
};

// One shot. intervalMs <= 0 disarms.
class Timer {
public:
  virtual ~Timer() = default;
  virtual void arm(int intervalMs) = 0;
};

// Parents the timer to owner. The callback runs on the owner's thread.
Timer *createQtCaptureTimer(QObject *owner, std::function<void()> fired);

class CaptureSchedule final : public QObject {
  Q_OBJECT
public:
  CaptureSchedule(Clock &clock, Timer &timer, QObject *parent = nullptr);
  ~CaptureSchedule() override;

  QString startDelay(const DelayRequest &request);
  void cancelDelay();
  bool delayActive() const { return m_mode == Mode::Delay; }
  int delayRemainingSeconds() const { return m_delayRemaining; }

  QString startAuto(const AutoRequest &request);
  void stopAuto();
  bool autoActive() const { return m_mode == Mode::Auto; }
  bool attemptInFlight() const { return m_inflight; }
  int issuedAttempts() const { return m_attempts; }
  int plannedAttempts() const { return m_planned; }
  AutoSummary summary() const { return m_summary; }
  void setBusy(bool busy) { m_busy = busy; }
  bool busy() const { return m_busy; }
  void finishAttempt(AttemptDisposition disposition);

  QString statusText() const;
  void onTimer();

signals:
  void delayTick(int remainingSeconds);
  void delayElapsed(xerahs::capture::CaptureSource source);
  void delayCancelled();
  void captureRequested(xerahs::capture::CaptureSource source);
  void autoFinished(xerahs::capture::AutoSummary summary);
  void statusTextChanged(const QString &text);

private:
  enum class Mode { Idle, Delay, Auto };

  void onDelayTimer();
  void issueAuto(qint64 now);
  void scheduleNext(qint64 now);
  void finishIfIdle();
  void touchStatus();
  void armWait(qint64 waitMs);

  Clock &m_clock;
  Timer &m_timer;
  Mode m_mode = Mode::Idle;
  bool m_busy = false;
  bool m_inflight = false;
  bool m_scheduling = false;
  bool m_timeBound = false;
  CaptureSource m_source = CaptureSource::FullScreen;
  int m_delaySeconds = 0;
  int m_delayRemaining = 0;
  qint64 m_delayStartMs = 0;
  int m_intervalMs = 0;
  int m_attempts = 0;
  int m_planned = 0;
  qint64 m_endMs = 0;
  qint64 m_nextDueMs = 0;
  AutoSummary m_summary;
};

}  // namespace xerahs::capture
