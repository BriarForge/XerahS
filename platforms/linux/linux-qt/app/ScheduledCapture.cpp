#include "ScheduledCapture.h"

#include "AutoCaptureDialog.h"
#include "DelayedCapture.h"

#include <QAction>

#include <utility>

namespace xerahs::app {

namespace {

QString failureBody(const QString &code) {
  const auto stable = code == QLatin1String("busy") ? QStringLiteral("activation-busy") : code;
  QString message = QStringLiteral("XerahS could not start the capture.");
  if (stable == QLatin1String("activation-busy"))
    message = QStringLiteral("XerahS is busy. Try again after the current capture or dialog finishes.");
  else if (stable == QLatin1String("activation-unavailable"))
    message = QStringLiteral("The operation is unavailable. Review the saved workflow and private execution storage ownership, permissions and capacity before retrying.");
  else if (stable == QLatin1String("capture-not-started"))
    message = QStringLiteral("The capture did not start.");
  return stable + QStringLiteral(": ") + message;
}

}  // namespace

ScheduledCapture::ScheduledCapture(ScheduledCapturePorts ports, QObject *parent)
    : QObject(parent), m_ports(std::move(ports)) {
  m_delay = new DelayedCapture(this);
  m_automatic = new AutoCaptureSession(this);
  wire();
}

ScheduledCapture::ScheduledCapture(ScheduledCapturePorts ports, xerahs::capture::Clock &clock, xerahs::capture::Timer &timer, QObject *parent)
    : QObject(parent), m_ports(std::move(ports)) {
  m_delay = new DelayedCapture(clock, timer, this);
  m_automatic = new AutoCaptureSession(clock, timer, this);
  wire();
}

void ScheduledCapture::setDialogParent(QWidget *parent) { m_dialogs = parent; }

void ScheduledCapture::setPreferences(xerahs::settings::CapturePreferencesPort port) {
  m_delay->setPreferences(port);
  m_automatic->setPreferences(std::move(port));
}

void ScheduledCapture::wire() {
  m_delayAction = new QAction(QStringLiteral("Capture after delay…"), this);
  m_autoAction = new QAction(QStringLiteral("Auto capture…"), this);
  m_stopAction = new QAction(QStringLiteral("Stop auto capture"), this);
  m_stopAction->setEnabled(false);
  connect(m_delayAction, &QAction::triggered, this, &ScheduledCapture::promptDelay);
  connect(m_autoAction, &QAction::triggered, this, &ScheduledCapture::promptAuto);
  connect(m_stopAction, &QAction::triggered, m_automatic, &AutoCaptureSession::stop);
  connect(m_automatic, &AutoCaptureSession::runningChanged, m_stopAction, &QAction::setEnabled);
  connect(m_automatic, &AutoCaptureSession::captureRequested, this, &ScheduledCapture::onAutoRequested);
  connect(m_delay, &DelayedCapture::captureRequested, this, &ScheduledCapture::onDelayRequested);
  connect(m_automatic, &AutoCaptureSession::finished, this, [this](const xerahs::capture::AutoSummary &summary) {
    notify(xerahs::capture::autoCaptureFinishedTitle(summary), xerahs::capture::autoCaptureSummaryText(summary), summary.failed == 0);
  });
  const auto notSaved = [this](const QString &title, const QString &code) {
    notify(title, QStringLiteral("Your choice could not be remembered (%1). The capture was still started.").arg(code), false);
  };
  connect(m_delay, &DelayedCapture::preferencesSaveFailed, this, [notSaved](const QString &code) { notSaved(QStringLiteral("Capture after delay"), code); });
  connect(m_automatic, &AutoCaptureSession::preferencesSaveFailed, this, [notSaved](const QString &code) { notSaved(QStringLiteral("Auto capture"), code); });
  connect(m_delay, &DelayedCapture::statusTextChanged, this, [this](const QString &) { refreshTooltip(); });
  connect(m_automatic, &AutoCaptureSession::statusTextChanged, this, [this](const QString &) { refreshTooltip(); });
  refreshTooltip();
}

void ScheduledCapture::onTimer() {
  m_delay->onTimer();
  m_automatic->onTimer();
}

void ScheduledCapture::captureBusyChanged(bool busy) {
  m_automatic->setBusy(busy);
  if (!busy && m_attempt) {
    m_attempt = false;
    m_automatic->finishAttempt(m_success ? xerahs::capture::AttemptDisposition::Captured : xerahs::capture::AttemptDisposition::Failed);
    m_success = false;
  }
}

void ScheduledCapture::captureFinished(bool success) {
  if (m_attempt) m_success = success;
}

void ScheduledCapture::promptDelay() {
  if (m_automatic->active()) {
    notify(QStringLiteral("Capture after delay"), QStringLiteral("Stop automatic capture before starting a delayed capture."), false);
    return;
  }
  if (m_delay->delayActive()) { m_delay->raiseCountdown(); return; }
  m_delay->prompt(m_dialogs);
}

void ScheduledCapture::promptAuto() {
  if (m_delay->delayActive()) {
    notify(QStringLiteral("Auto capture"), QStringLiteral("Cancel the delayed capture before starting automatic capture."), false);
    return;
  }
  m_automatic->prompt(m_dialogs);
}

void ScheduledCapture::refreshTooltip() {
  if (!m_ports.setTooltip) return;
  const auto automatic = m_automatic->statusText();
  const auto delay = m_delay->statusText();
  m_ports.setTooltip(!automatic.isEmpty() ? automatic : !delay.isEmpty() ? delay : QStringLiteral("XerahS"));
}

void ScheduledCapture::notify(const QString &title, const QString &body, bool ok) {
  if (m_ports.notify) m_ports.notify(title, body, ok);
}

void ScheduledCapture::onAutoRequested(xerahs::capture::CaptureSource source) {
  if (m_ports.blocked && m_ports.blocked()) {
    m_automatic->finishAttempt(xerahs::capture::AttemptDisposition::Skipped);
    return;
  }
  m_attempt = true;
  m_success = false;
  const auto code = m_ports.start ? m_ports.start(source) : QStringLiteral("activation-unavailable");
  if (!m_attempt) return;
  if (code.isEmpty()) return;
  m_attempt = false;
  m_automatic->finishAttempt(code == QLatin1String("busy") ? xerahs::capture::AttemptDisposition::Skipped : xerahs::capture::AttemptDisposition::Failed);
}

void ScheduledCapture::onDelayRequested(xerahs::capture::CaptureSource source) {
  const auto code = m_ports.start ? m_ports.start(source) : QStringLiteral("activation-unavailable");
  if (!code.isEmpty()) notify(QStringLiteral("Capture after delay"), failureBody(code), false);
}

}  // namespace xerahs::app
