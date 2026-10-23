#include "DelayedCapture.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFont>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace xerahs::app {

namespace {

class DelayCaptureDialog final : public QDialog {
public:
  DelayCaptureDialog(int seconds, xerahs::capture::CaptureSource source, QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Capture after delay"));
    setObjectName(QStringLiteral("delayCaptureDialog"));
    auto *form = new QFormLayout;
    m_seconds = new QSpinBox(this);
    m_seconds->setObjectName(QStringLiteral("delaySeconds"));
    m_seconds->setRange(xerahs::capture::kMinDelaySeconds, xerahs::capture::kMaxDelaySeconds);
    m_seconds->setValue(seconds);
    m_seconds->setSuffix(QStringLiteral(" s"));
    m_seconds->setAccessibleName(QStringLiteral("Delay in seconds"));
    form->addRow(QStringLiteral("Delay"), m_seconds);
    m_source = new QComboBox(this);
    m_source->setObjectName(QStringLiteral("delaySource"));
    m_source->setAccessibleName(QStringLiteral("Capture source"));
    const struct { const char *label; xerahs::capture::CaptureSource source; } sources[] = {
        {"Full screen", xerahs::capture::CaptureSource::FullScreen},
        {"Monitor under pointer", xerahs::capture::CaptureSource::Monitor},
        {"Active window", xerahs::capture::CaptureSource::ActiveWindow},
        {"Region", xerahs::capture::CaptureSource::Region},
    };
    for (const auto &entry : sources) m_source->addItem(QString::fromUtf8(entry.label), xerahs::capture::captureSourceId(entry.source));
    const int index = m_source->findData(xerahs::capture::captureSourceId(source));
    if (index >= 0) m_source->setCurrentIndex(index);
    form->addRow(QStringLiteral("Source"), m_source);
    auto *buttons = new QHBoxLayout;
    auto *start = new QPushButton(QStringLiteral("Start"), this);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    start->setDefault(true);
    buttons->addStretch();
    buttons->addWidget(start);
    buttons->addWidget(cancel);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(buttons);
    connect(start, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  }
  int seconds() const { return m_seconds->value(); }
  xerahs::capture::CaptureSource source() const {
    const auto parsed = xerahs::capture::captureSourceFromId(m_source->currentData().toString());
    return parsed ? *parsed : xerahs::capture::CaptureSource::FullScreen;
  }
private:
  QSpinBox *m_seconds = nullptr;
  QComboBox *m_source = nullptr;
};

class CaptureCountdownWindow final : public QDialog {
public:
  explicit CaptureCountdownWindow(QWidget *parent = nullptr) : QDialog(parent) {
    setObjectName(QStringLiteral("captureCountdown"));
    setWindowTitle(QStringLiteral("Capture after delay"));
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setModal(false);
    setFixedSize(240, 150);
    auto *layout = new QVBoxLayout(this);
    auto *caption = new QLabel(QStringLiteral("Capture starts in"), this);
    caption->setAlignment(Qt::AlignCenter);
    m_seconds = new QLabel(QStringLiteral("0"), this);
    m_seconds->setObjectName(QStringLiteral("countdownSeconds"));
    m_seconds->setAlignment(Qt::AlignCenter);
    m_seconds->setAccessibleName(QStringLiteral("Seconds remaining"));
    QFont font = m_seconds->font();
    font.setPointSize(28);
    m_seconds->setFont(font);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("countdownCancel"));
    cancel->setAccessibleName(QStringLiteral("Cancel delayed capture"));
    layout->addWidget(caption);
    layout->addWidget(m_seconds);
    layout->addWidget(cancel);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { emitCancel(); });
    connect(cancel, &QPushButton::clicked, this, [this] { emitCancel(); });
  }
  void setRemaining(int seconds) { m_seconds->setText(QString::number(seconds)); }
  std::function<void()> onCancel;
protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Escape) { emitCancel(); event->accept(); return; }
    QDialog::keyPressEvent(event);
  }
  void closeEvent(QCloseEvent *event) override {
    emitCancel();
    event->accept();
  }
private:
  void emitCancel() { if (onCancel) onCancel(); }
  QLabel *m_seconds = nullptr;
};

}  // namespace

DelayedCapture::DelayedCapture(QObject *parent) : QObject(parent) {
  m_ownedClock = std::make_unique<xerahs::capture::SystemClock>();
  m_ownedTimer = xerahs::capture::createQtCaptureTimer(this, [this] { onTimer(); });
  m_clock = m_ownedClock.get();
  m_timer = m_ownedTimer;
  m_schedule = std::make_unique<xerahs::capture::CaptureSchedule>(*m_clock, *m_timer, this);
  wire();
}

DelayedCapture::DelayedCapture(xerahs::capture::Clock &clock, xerahs::capture::Timer &timer, QObject *parent) : QObject(parent) {
  m_clock = &clock;
  m_timer = &timer;
  m_schedule = std::make_unique<xerahs::capture::CaptureSchedule>(*m_clock, *m_timer, this);
  wire();
}

DelayedCapture::~DelayedCapture() {
  if (m_countdown) {
    m_countdown->disconnect();
    delete m_countdown;
    m_countdown = nullptr;
  }
}

void DelayedCapture::wire() {
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::delayTick, this, [this](int seconds) {
    if (m_countdown) static_cast<CaptureCountdownWindow *>(m_countdown)->setRemaining(seconds);
  });
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::delayElapsed, this, [this](xerahs::capture::CaptureSource source) {
    hideCountdown();
    const int generation = m_fireGeneration;
    QTimer::singleShot(0, this, [this, source, generation] {
      if (generation != m_fireGeneration) return;
      emit captureRequested(source);
    });
  });
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::delayCancelled, this, [this] { hideCountdown(); });
  connect(m_schedule.get(), &xerahs::capture::CaptureSchedule::statusTextChanged, this, &DelayedCapture::statusTextChanged);
}

void DelayedCapture::remember(int seconds, xerahs::capture::CaptureSource source) {
  m_seconds = seconds;
  m_source = source;
}

void DelayedCapture::setRemembered(int seconds, xerahs::capture::CaptureSource source) { remember(seconds, source); }

bool DelayedCapture::prompt(QWidget *parent) {
  if (delayActive()) return false;
  DelayCaptureDialog dialog(m_seconds, m_source, parent);
  if (dialog.exec() != QDialog::Accepted) return false;
  remember(dialog.seconds(), dialog.source());
  return start({dialog.source(), dialog.seconds()}).isEmpty();
}

QString DelayedCapture::start(const xerahs::capture::DelayRequest &request) {
  const auto code = m_schedule->startDelay(request);
  if (!code.isEmpty()) return code;
  if (!m_countdown) {
    auto *window = new CaptureCountdownWindow;
    window->onCancel = [this] { cancel(); };
    m_countdown = window;
  }
  auto *window = static_cast<CaptureCountdownWindow *>(m_countdown);
  window->setRemaining(m_schedule->delayRemainingSeconds());
  if (auto *screen = QGuiApplication::primaryScreen()) {
    const QRect area = screen->availableGeometry();
    window->move(area.right() - window->width() - 24, area.top() + 24);
  }
  window->show();
  window->raise();
  window->activateWindow();
  return {};
}

void DelayedCapture::cancel() {
  ++m_fireGeneration;
  hideCountdown();
  if (m_schedule) m_schedule->cancelDelay();
}

void DelayedCapture::raiseCountdown() {
  if (!m_countdown) return;
  m_countdown->show();
  m_countdown->raise();
  m_countdown->activateWindow();
}

void DelayedCapture::onTimer() { if (m_schedule) m_schedule->onTimer(); }

bool DelayedCapture::delayActive() const { return m_schedule && m_schedule->delayActive(); }

QString DelayedCapture::statusText() const { return m_schedule ? m_schedule->statusText() : QString(); }

void DelayedCapture::hideCountdown() { if (m_countdown) m_countdown->hide(); }

}  // namespace xerahs::app
