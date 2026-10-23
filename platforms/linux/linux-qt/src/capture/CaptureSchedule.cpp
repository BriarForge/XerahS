#include "capture/CaptureSchedule.h"

#include <QDateTime>
#include <QTimer>

#include <utility>

namespace xerahs::capture {

namespace {

class QtCaptureTimer final : public QObject, public Timer {
public:
  QtCaptureTimer(QObject *parent, std::function<void()> fired) : QObject(parent), m_fired(std::move(fired)) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { if (m_fired) m_fired(); });
  }
  void arm(int intervalMs) override {
    if (intervalMs <= 0) { m_timer.stop(); return; }
    m_timer.start(intervalMs);
  }
private:
  QTimer m_timer;
  std::function<void()> m_fired;
};

}  // namespace

QString captureSourceId(CaptureSource source) {
  switch (source) {
  case CaptureSource::FullScreen: return QStringLiteral("full-screen");
  case CaptureSource::Monitor: return QStringLiteral("monitor");
  case CaptureSource::ActiveWindow: return QStringLiteral("active-window");
  case CaptureSource::Region: return QStringLiteral("region");
  }
  return {};
}

std::optional<CaptureSource> captureSourceFromId(const QString &id) {
  if (id == QLatin1String("full-screen")) return CaptureSource::FullScreen;
  if (id == QLatin1String("monitor")) return CaptureSource::Monitor;
  if (id == QLatin1String("active-window")) return CaptureSource::ActiveWindow;
  if (id == QLatin1String("region")) return CaptureSource::Region;
  return std::nullopt;
}

bool isAutomaticCaptureSource(CaptureSource source) {
  return source == CaptureSource::FullScreen || source == CaptureSource::Monitor || source == CaptureSource::ActiveWindow;
}

std::optional<ScheduleDiagnostic> validateDelay(const DelayRequest &request) {
  if (request.seconds < kMinDelaySeconds || request.seconds > kMaxDelaySeconds)
    return ScheduleDiagnostic{QStringLiteral("delay-seconds-out-of-range")};
  return std::nullopt;
}

std::optional<ScheduleDiagnostic> validateAuto(CaptureSource source, int intervalMs, const std::optional<ScheduleBound> &bound, qint64 nowMs) {
  if (!isAutomaticCaptureSource(source)) return ScheduleDiagnostic{QStringLiteral("source-unavailable")};
  if (intervalMs < kMinIntervalMs || intervalMs > kMaxIntervalMs) return ScheduleDiagnostic{QStringLiteral("interval-out-of-range")};
  if (!bound) return ScheduleDiagnostic{QStringLiteral("bound-required")};
  if (const auto *count = std::get_if<CountBound>(&*bound)) {
    if (count->count < kMinCaptureCount || count->count > kMaxCaptureCount)
      return ScheduleDiagnostic{QStringLiteral("count-out-of-range")};
    return std::nullopt;
  }
  const auto endMs = std::get<TimeBound>(*bound).endMs;
  if (endMs <= nowMs) return ScheduleDiagnostic{QStringLiteral("end-not-after-start")};
  if (endMs - nowMs > kMaxScheduleWindowMs) return ScheduleDiagnostic{QStringLiteral("end-beyond-limit")};
  return std::nullopt;
}

QString autoCaptureSummaryText(const AutoSummary &summary) {
  return QStringLiteral("Captured %1, skipped %2, failed %3.").arg(summary.captured).arg(summary.skipped).arg(summary.failed);
}

QString autoCaptureFinishedTitle(const AutoSummary &summary) {
  return summary.stoppedByUser ? QStringLiteral("Auto capture stopped") : QStringLiteral("Auto capture finished");
}

qint64 SystemClock::nowMs() const { return QDateTime::currentMSecsSinceEpoch(); }

Timer *createQtCaptureTimer(QObject *owner, std::function<void()> fired) {
  return new QtCaptureTimer(owner, std::move(fired));
}

CaptureSchedule::CaptureSchedule(Clock &clock, Timer &timer, QObject *parent) : QObject(parent), m_clock(clock), m_timer(timer) {}

CaptureSchedule::~CaptureSchedule() { m_timer.arm(0); }

QString CaptureSchedule::statusText() const {
  if (m_mode == Mode::Delay) return QStringLiteral("Capturing in %1 s").arg(m_delayRemaining);
  if (m_mode == Mode::Auto) return QStringLiteral("Auto capture: %1 of %2").arg(m_attempts).arg(m_planned);
  return {};
}

void CaptureSchedule::touchStatus() { emit statusTextChanged(statusText()); }

void CaptureSchedule::armWait(qint64 waitMs) {
  if (waitMs <= 0) waitMs = 1;
  if (waitMs > kMaxIntervalMs) waitMs = kMaxIntervalMs;
  m_timer.arm(int(waitMs));
}

QString CaptureSchedule::startDelay(const DelayRequest &request) {
  if (const auto diagnostic = validateDelay(request)) return diagnostic->code;
  if (m_mode != Mode::Idle) return QStringLiteral("schedule-busy");
  m_mode = Mode::Delay;
  m_source = request.source;
  m_delaySeconds = request.seconds;
  m_delayRemaining = request.seconds;
  m_delayStartMs = m_clock.nowMs();
  m_timer.arm(1000);
  emit delayTick(m_delayRemaining);
  touchStatus();
  return {};
}

void CaptureSchedule::cancelDelay() {
  if (m_mode != Mode::Delay) return;
  m_mode = Mode::Idle;
  m_delayRemaining = 0;
  m_timer.arm(0);
  emit delayCancelled();
  emit statusTextChanged(QString());
}

void CaptureSchedule::onTimer() {
  if (m_mode == Mode::Delay) { onDelayTimer(); return; }
  if (m_mode != Mode::Auto || !m_scheduling) return;
  const qint64 now = m_clock.nowMs();
  if (now < m_nextDueMs) { armWait(m_nextDueMs - now); return; }
  issueAuto(now);
}

void CaptureSchedule::onDelayTimer() {
  if (m_mode != Mode::Delay) return;
  const qint64 elapsed = m_clock.nowMs() - m_delayStartMs;
  if (elapsed < 0) { m_timer.arm(1000); return; }
  if (elapsed < qint64(m_delaySeconds) * 1000) {
    const int remaining = int(m_delaySeconds - elapsed / 1000);
    if (remaining != m_delayRemaining && remaining > 0) {
      m_delayRemaining = remaining;
      emit delayTick(remaining);
      touchStatus();
    }
    const int into = int(elapsed % 1000);
    m_timer.arm(into == 0 ? 1000 : 1000 - into);
    return;
  }
  const auto source = m_source;
  m_mode = Mode::Idle;
  m_delayRemaining = 0;
  m_timer.arm(0);
  emit statusTextChanged(QString());
  emit delayElapsed(source);
}

QString CaptureSchedule::startAuto(const AutoRequest &request) {
  const auto diagnostic = validateAuto(request.source, request.intervalMs, request.bound, m_clock.nowMs());
  if (diagnostic) return diagnostic->code;
  if (m_mode != Mode::Idle) return QStringLiteral("schedule-busy");
  m_mode = Mode::Auto;
  m_scheduling = true;
  m_inflight = false;
  m_summary = {};
  m_attempts = 0;
  m_source = request.source;
  m_intervalMs = request.intervalMs;
  if (const auto *count = std::get_if<CountBound>(&request.bound)) {
    m_timeBound = false;
    m_endMs = 0;
    m_planned = count->count;
  } else {
    m_timeBound = true;
    m_endMs = std::get<TimeBound>(request.bound).endMs;
    const qint64 span = m_endMs - m_clock.nowMs();
    m_planned = int(span / m_intervalMs) + 1;
  }
  issueAuto(m_clock.nowMs());
  return {};
}

void CaptureSchedule::issueAuto(qint64 now) {
  if (m_mode != Mode::Auto || !m_scheduling) return;
  if (m_attempts >= m_planned || (m_timeBound && now > m_endMs)) {
    m_scheduling = false;
    finishIfIdle();
    return;
  }
  ++m_attempts;
  if (m_busy || m_inflight) ++m_summary.skipped;
  else {
    m_inflight = true;
    emit captureRequested(m_source);
  }
  touchStatus();
  scheduleNext(now);
}

void CaptureSchedule::scheduleNext(qint64 now) {
  if (m_mode != Mode::Auto) return;
  if (m_summary.stoppedByUser || m_attempts >= m_planned) {
    m_scheduling = false;
    finishIfIdle();
    return;
  }
  m_nextDueMs = now + m_intervalMs;
  if (m_timeBound && m_nextDueMs > m_endMs) {
    m_scheduling = false;
    finishIfIdle();
    return;
  }
  armWait(m_nextDueMs - m_clock.nowMs());
}

void CaptureSchedule::finishIfIdle() {
  if (m_mode != Mode::Auto || m_inflight || m_scheduling) return;
  m_mode = Mode::Idle;
  m_timer.arm(0);
  const auto summary = m_summary;
  emit statusTextChanged(QString());
  emit autoFinished(summary);
}

void CaptureSchedule::finishAttempt(AttemptDisposition disposition) {
  if (!m_inflight || m_mode != Mode::Auto) return;
  m_inflight = false;
  switch (disposition) {
  case AttemptDisposition::Captured: ++m_summary.captured; break;
  case AttemptDisposition::Skipped: ++m_summary.skipped; break;
  case AttemptDisposition::Failed: ++m_summary.failed; break;
  }
  finishIfIdle();
}

void CaptureSchedule::stopAuto() {
  if (m_mode != Mode::Auto) return;
  m_summary.stoppedByUser = true;
  m_scheduling = false;
  m_timer.arm(0);
  finishIfIdle();
}

}  // namespace xerahs::capture
