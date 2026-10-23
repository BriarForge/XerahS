#include "AutoCaptureDialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace xerahs::app {

namespace {

QString diagnosticText(const xerahs::capture::ScheduleDiagnostic &diagnostic) {
  const auto &code = diagnostic.code;
  if (code == QLatin1String("bound-required")) return QStringLiteral("Choose a bound: a number of captures or an end time.");
  if (code == QLatin1String("source-unavailable")) return QStringLiteral("Automatic capture cannot use a region.");
  if (code == QLatin1String("interval-out-of-range")) return QStringLiteral("Interval must be from 1 second to 24 hours.");
  if (code == QLatin1String("count-out-of-range")) return QStringLiteral("Capture count must be from 1 to 10000.");
  if (code == QLatin1String("end-not-after-start")) return QStringLiteral("End time must be after the current time.");
  if (code == QLatin1String("end-beyond-limit")) return QStringLiteral("End time must be within 24 hours.");
  return code;
}

}  // namespace

AutoCaptureDialog::AutoCaptureDialog(std::function<qint64()> nowMs, bool running, QWidget *parent)
    : AutoCaptureDialog(std::move(nowMs), running, {}, parent) {}

AutoCaptureDialog::AutoCaptureDialog(std::function<qint64()> nowMs, bool running, xerahs::settings::CapturePreferencesPort preferences, QWidget *parent)
    : QDialog(parent), m_nowMs(std::move(nowMs)), m_running(running), m_preferences(std::move(preferences)) {
  setWindowTitle(QStringLiteral("Auto capture"));
  setObjectName(QStringLiteral("autoCaptureDialog"));
  auto *form = new QFormLayout;
  m_source = new QComboBox(this);
  m_source->setObjectName(QStringLiteral("autoCaptureSource"));
  m_source->setAccessibleName(QStringLiteral("Automatic capture source"));
  m_source->addItem(QStringLiteral("Full screen"), QStringLiteral("full-screen"));
  m_source->addItem(QStringLiteral("Monitor under pointer"), QStringLiteral("monitor"));
  m_source->addItem(QStringLiteral("Active window"), QStringLiteral("active-window"));
  form->addRow(QStringLiteral("Source"), m_source);
  m_interval = new QSpinBox(this);
  m_interval->setObjectName(QStringLiteral("autoCaptureInterval"));
  m_interval->setRange(1, 24 * 60 * 60);
  m_interval->setValue(5);
  m_interval->setSuffix(QStringLiteral(" s"));
  m_interval->setAccessibleName(QStringLiteral("Interval in seconds"));
  form->addRow(QStringLiteral("Interval"), m_interval);
  m_countBound = new QRadioButton(QStringLiteral("Number of captures"), this);
  m_countBound->setObjectName(QStringLiteral("autoCaptureCountBound"));
  m_count = new QSpinBox(this);
  m_count->setObjectName(QStringLiteral("autoCaptureCount"));
  m_count->setRange(xerahs::capture::kMinCaptureCount, xerahs::capture::kMaxCaptureCount);
  m_count->setValue(10);
  m_count->setAccessibleName(QStringLiteral("Number of captures"));
  auto *countRow = new QHBoxLayout;
  countRow->addWidget(m_countBound);
  countRow->addWidget(m_count);
  form->addRow(QStringLiteral("Bound"), countRow);
  m_timeBound = new QRadioButton(QStringLiteral("End time"), this);
  m_timeBound->setObjectName(QStringLiteral("autoCaptureTimeBound"));
  m_end = new QDateTimeEdit(QDateTime::fromMSecsSinceEpoch(m_nowMs() + 10 * 60 * 1000), this);
  m_end->setObjectName(QStringLiteral("autoCaptureEnd"));
  m_end->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  m_end->setCalendarPopup(true);
  m_end->setAccessibleName(QStringLiteral("End time"));
  auto *timeRow = new QHBoxLayout;
  timeRow->addWidget(m_timeBound);
  timeRow->addWidget(m_end);
  form->addRow(QString(), timeRow);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("autoCaptureStatus"));
  m_status->setWordWrap(true);
  auto *buttons = new QHBoxLayout;
  m_start = new QPushButton(QStringLiteral("Start"), this);
  m_stop = new QPushButton(QStringLiteral("Stop"), this);
  auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
  m_start->setObjectName(QStringLiteral("autoCaptureStart"));
  m_stop->setObjectName(QStringLiteral("autoCaptureStop"));
  m_start->setDefault(true);
  buttons->addStretch();
  buttons->addWidget(m_start);
  buttons->addWidget(m_stop);
  buttons->addWidget(cancel);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(m_status);
  layout->addLayout(buttons);
  if (m_preferences) {
    const auto values = m_preferences.values();
    const int source = m_source->findData(values.value(QStringLiteral("autoCapture.source")).toString());
    if (source >= 0) m_source->setCurrentIndex(source);
    m_interval->setValue(values.value(QStringLiteral("autoCapture.intervalMs")).toInt(5000) / 1000);
    m_count->setValue(values.value(QStringLiteral("autoCapture.count")).toInt(10));
    m_end->setDateTime(QDateTime::fromMSecsSinceEpoch(m_nowMs() + values.value(QStringLiteral("autoCapture.durationMinutes")).toInt(60) * 60 * 1000));
    if (values.value(QStringLiteral("autoCapture.boundKind")).toString() == QLatin1String("time")) m_timeBound->setChecked(true);
    else m_countBound->setChecked(true);
  }
  connect(m_source, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
  connect(m_interval, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refresh(); });
  connect(m_count, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refresh(); });
  connect(m_countBound, &QRadioButton::toggled, this, [this] { refresh(); });
  connect(m_timeBound, &QRadioButton::toggled, this, [this] { refresh(); });
  connect(m_end, &QDateTimeEdit::dateTimeChanged, this, [this] { refresh(); });
  connect(m_start, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_stop, &QPushButton::clicked, this, [this] { done(StopResult); });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  refresh();
}

xerahs::capture::CaptureSource AutoCaptureDialog::source() const {
  const auto parsed = xerahs::capture::captureSourceFromId(m_source->currentData().toString());
  return parsed ? *parsed : xerahs::capture::CaptureSource::Region;
}

std::optional<xerahs::capture::ScheduleBound> AutoCaptureDialog::bound() const {
  if (m_countBound->isChecked()) return xerahs::capture::CountBound{m_count->value()};
  if (m_timeBound->isChecked()) return xerahs::capture::TimeBound{m_end->dateTime().toMSecsSinceEpoch()};
  return std::nullopt;
}

void AutoCaptureDialog::refresh() {
  const bool fields = !m_running;
  m_source->setEnabled(fields);
  m_interval->setEnabled(fields);
  m_countBound->setEnabled(fields);
  m_timeBound->setEnabled(fields);
  m_count->setEnabled(fields && m_countBound->isChecked());
  m_end->setEnabled(fields && m_timeBound->isChecked());
  m_stop->setEnabled(m_running);
  if (m_running) {
    m_status->setText(QStringLiteral("Automatic capture is running."));
    m_start->setEnabled(false);
    return;
  }
  const auto diagnostic = xerahs::capture::validateAuto(source(), m_interval->value() * 1000, bound(), m_nowMs());
  m_start->setEnabled(!diagnostic);
  m_status->setText(diagnostic ? diagnosticText(*diagnostic) : QStringLiteral("Ready."));
}

void AutoCaptureDialog::accept() {
  const auto diagnostic = xerahs::capture::validateAuto(source(), m_interval->value() * 1000, bound(), m_nowMs());
  if (m_running || diagnostic) { refresh(); return; }
  xerahs::capture::AutoRequest request;
  request.source = source();
  request.intervalMs = m_interval->value() * 1000;
  request.bound = *bound();
  m_request = request;
  saveChoice();
  QDialog::accept();
}

// A failed save is shown in the status line and never blocks the capture.
void AutoCaptureDialog::saveChoice() {
  if (!m_preferences) return;
  auto values = m_preferences.values();
  values.insert(QStringLiteral("autoCapture.source"), xerahs::capture::captureSourceId(source()));
  values.insert(QStringLiteral("autoCapture.intervalMs"), m_interval->value() * 1000);
  if (m_countBound->isChecked()) {
    values.insert(QStringLiteral("autoCapture.boundKind"), QStringLiteral("count"));
    values.insert(QStringLiteral("autoCapture.count"), m_count->value());
  } else {
    // Only the length is kept, never the absolute end time.
    const qint64 remaining = m_end->dateTime().toMSecsSinceEpoch() - m_nowMs();
    values.insert(QStringLiteral("autoCapture.boundKind"), QStringLiteral("time"));
    values.insert(QStringLiteral("autoCapture.durationMinutes"), static_cast<int>((remaining + 59999) / 60000));
  }
  const auto code = m_preferences.save(values);
  m_status->setText(code.isEmpty() ? QString() : QStringLiteral("Could not remember this choice (%1). The capture will still start.").arg(code));
  if (!code.isEmpty()) m_saveError = code;
}

std::optional<xerahs::capture::AutoRequest> AutoCaptureDialog::takeRequest() { return std::exchange(m_request, std::nullopt); }

AutoCaptureSession::AutoCaptureSession(QObject *parent) : QObject(parent) {
  m_ownedClock = std::make_unique<xerahs::capture::SystemClock>();
  m_ownedTimer = xerahs::capture::createQtCaptureTimer(this, [this] { onTimer(); });
  m_clock = m_ownedClock.get();
  m_timer = m_ownedTimer;
  m_schedule = std::make_unique<xerahs::capture::CaptureSchedule>(*m_clock, *m_timer, this);
  wire();
}

AutoCaptureSession::AutoCaptureSession(xerahs::capture::Clock &clock, xerahs::capture::Timer &timer, QObject *parent) : QObject(parent) {
  m_clock = &clock;
  m_timer = &timer;
  m_schedule = std::make_unique<xerahs::capture::CaptureSchedule>(*m_clock, *m_timer, this);
  wire();
}

void AutoCaptureSession::wire() {
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::captureRequested, this, &AutoCaptureSession::captureRequested);
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::statusTextChanged, this, &AutoCaptureSession::statusTextChanged);
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::autoFinished, this, [this](const xerahs::capture::AutoSummary &summary) {
    emitRunning();
    emit finished(summary);
  });
}

bool AutoCaptureSession::prompt(QWidget *parent) {
  AutoCaptureDialog dialog([this] { return m_clock->nowMs(); }, active(), m_preferences, parent);
  const int result = dialog.exec();
  if (result == AutoCaptureDialog::StopResult) { stop(); return true; }
  if (result != QDialog::Accepted) return false;
  const auto request = dialog.takeRequest();
  if (!request) return false;
  if (!dialog.saveError().isEmpty()) emit preferencesSaveFailed(dialog.saveError());
  return start(*request).isEmpty();
}

QString AutoCaptureSession::start(const xerahs::capture::AutoRequest &request) {
  const auto code = m_schedule->startAuto(request);
  emitRunning();
  return code;
}

void AutoCaptureSession::stop() {
  m_schedule->stopAuto();
  emitRunning();
}

void AutoCaptureSession::onTimer() { m_schedule->onTimer(); }

bool AutoCaptureSession::active() const { return m_schedule && m_schedule->autoActive(); }

void AutoCaptureSession::setBusy(bool busy) { if (m_schedule) m_schedule->setBusy(busy); }

void AutoCaptureSession::finishAttempt(xerahs::capture::AttemptDisposition disposition) {
  if (m_schedule) m_schedule->finishAttempt(disposition);
}

QString AutoCaptureSession::statusText() const { return m_schedule ? m_schedule->statusText() : QString(); }

void AutoCaptureSession::emitRunning() {
  const bool running = active();
  if (running == m_running) return;
  m_running = running;
  emit runningChanged(running);
}

}  // namespace xerahs::app
