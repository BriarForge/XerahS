// CORE-015: scrolling capture state machine, dialog and native collaborators.
#include "ScrollCapture.h"

#include "PortalRemoteDesktop.h"
#include "PortalScreenshot.h"

#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace xerahs::app {
using xerahs::capture::ScrollStatus;
namespace {

QString describe(const QString &code, const xerahs::capture::ScrollStitcher &stitcher, const QString &failure) {
  const int frames = int(stitcher.frames().size());
  if (!failure.isEmpty()) {
    if (failure == u"scroll-frame-unavailable") return QStringLiteral("A frame could not be captured. ");
    if (failure.contains(u"permission")) return QStringLiteral("Screen capture permission was refused for a frame. ");
    return QStringLiteral("A frame could not be captured (%1). ").arg(failure);
  }
  if (code == u"scroll-low-confidence") {
    const double confidence = stitcher.joins().empty() ? 0.0 : stitcher.joins().back().confidence;
    return QStringLiteral("Frame %1 could not be aligned with the previous one (confidence %2%, needs %3%). No seamless image was made past that point. ")
        .arg(frames).arg(int(confidence * 100)).arg(int(xerahs::capture::kScrollAlignmentThreshold * 100));
  }
  if (code == u"scroll-frame-invalid") return QStringLiteral("Frame %1 was empty or a different size. ").arg(frames);
  if (code == u"scroll-end-of-page") return QStringLiteral("Reached the end of the page. ");
  if (code == u"scroll-max-height") return QStringLiteral("Stopped at the maximum height. ");
  if (code == u"scroll-max-frames") return QStringLiteral("Stopped at the maximum number of frames. ");
  return {};
}

class ScrollDialogView final : public ScrollCaptureView {
public:
  explicit ScrollDialogView(QObject *parent) : ScrollCaptureView(parent) {
    m_dialog = new QDialog(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
    m_dialog->setWindowTitle(QStringLiteral("Scrolling capture"));
    auto *layout = new QVBoxLayout(m_dialog);
    m_label = new QLabel(m_dialog); m_label->setWordWrap(true); layout->addWidget(m_label);
    m_hint = new QLabel(m_dialog); m_hint->setWordWrap(true); layout->addWidget(m_hint);
    auto *row = new QHBoxLayout; layout->addLayout(row);
    m_grab = new QPushButton(QStringLiteral("Grab frame"), m_dialog);
    m_manualButton = new QPushButton(QStringLiteral("Scroll manually"), m_dialog);
    auto *stop = new QPushButton(QStringLiteral("Stop and keep"), m_dialog);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), m_dialog);
    for (auto *button : {m_grab, m_manualButton, stop, cancel}) { button->setAutoDefault(false); row->addWidget(button); }
    m_grab->setDefault(true);
    connect(m_grab, &QPushButton::clicked, this, &ScrollCaptureView::grabRequested);
    connect(m_manualButton, &QPushButton::clicked, this, &ScrollCaptureView::manualRequested);
    connect(stop, &QPushButton::clicked, this, &ScrollCaptureView::stopRequested);
    connect(cancel, &QPushButton::clicked, this, &ScrollCaptureView::cancelRequested);
    connect(m_dialog, &QDialog::rejected, this, [this] { if (!m_closing) emit cancelRequested(); });
  }
  ~ScrollDialogView() override { delete m_dialog; }

  void open(bool manual) override { setManual(manual); m_dialog->show(); }
  void setProgress(int frames, int height, const QString &text) override {
    m_label->setText(QStringLiteral("%1 frames, %2 px. %3").arg(frames).arg(height).arg(text));
  }
  void setManual(bool manual) override {
    m_grab->setVisible(manual); m_manualButton->setVisible(!manual);
    m_hint->setText(manual ? QStringLiteral("Scroll the page yourself, then press Grab frame. Overlap consecutive views so frames can be aligned. Stop and keep when done.")
                           : QStringLiteral("XerahS scrolls the pointer wheel over the region. Keep the pointer over the page."));
    if (manual) m_grab->setFocus();
  }
  void hideForGrab(bool hide) override { m_dialog->setVisible(!hide); }
  void decide(const ScrollOutcome &outcome, std::function<void(ScrollChoice)> chosen) override {
    m_dialog->hide();
    auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Scrolling capture"), outcome.message, QMessageBox::NoButton, nullptr);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText(QStringLiteral("%1 frames captured; the stitched part is %2 px tall. Nothing has been saved or published.")
        .arg(outcome.frames.size()).arg(outcome.image.height()));
    auto *partial = box->addButton(QStringLiteral("Save stitched part"), QMessageBox::AcceptRole);
    auto *frames = box->addButton(QStringLiteral("Save frames separately"), QMessageBox::ActionRole);
    auto *discard = box->addButton(QStringLiteral("Discard"), QMessageBox::RejectRole);
    box->setDefaultButton(frames); box->setEscapeButton(discard);
    connect(box, &QDialog::finished, this, [box, partial, frames, chosen = std::move(chosen)] {
      const auto *clicked = box->clickedButton();
      chosen(clicked == partial ? ScrollChoice::SavePartial : clicked == frames ? ScrollChoice::SaveFrames : ScrollChoice::Discard);
    });
    m_box = box;
    box->open();
  }
  void close() override { m_closing = true; m_dialog->hide(); m_closing = false; if (m_box) m_box->close(); }

private:
  QDialog *m_dialog = nullptr;
  QLabel *m_label = nullptr, *m_hint = nullptr;
  QPushButton *m_grab = nullptr, *m_manualButton = nullptr;
  QPointer<QMessageBox> m_box;
  bool m_closing = false;
};

}  // namespace

ScrollCapture::ScrollCapture(ScrollCapturePorts ports, ScrollCaptureSettings settings, QObject *parent)
    : QObject(parent), m_ports(std::move(ports)), m_settings(std::move(settings)) {
  m_timer = new QTimer(this); m_timer->setSingleShot(true);
}
ScrollCapture::~ScrollCapture() {
  ++m_generation; m_work.cancel();
  if (m_cancelGrab) m_cancelGrab();
  if (m_remote) m_remote->close();
}

int ScrollCapture::frames() const { return m_stitcher ? int(m_stitcher->frames().size()) : 0; }

void ScrollCapture::setState(State state) {
  if (m_state == state) return;
  m_state = state;
  emit stateChanged(state);
}

void ScrollCapture::armTimer(int ms, std::function<void()> action) {
  m_timer->disconnect();
  connect(m_timer, &QTimer::timeout, this, std::move(action));
  m_timer->start(std::max(0, ms));
}

ScrollCaptureSettings scrollCaptureSettings(const QJsonObject &values, ScrollCaptureSettings base) {
  auto number = [&values](const char *key, int fallback) {
    const auto value = values.value(QLatin1String(key));
    return value.isDouble() ? value.toInt(fallback) : fallback;
  };
  base.settleMs = number("scrollingCapture.settleMs", base.settleMs);
  base.wheelSteps = number("scrollingCapture.wheelSteps", base.wheelSteps);
  base.stitch.headerBand = number("scrollingCapture.headerBand", base.stitch.headerBand);
  base.stitch.footerBand = number("scrollingCapture.footerBand", base.stitch.footerBand);
  base.stitch.maxHeight = number("scrollingCapture.maxHeight", base.stitch.maxHeight);
  base.stitch.maxFrames = number("scrollingCapture.maxFrames", base.stitch.maxFrames);
  const auto pointer = values.value(QLatin1String("scrollingCapture.positionPointer"));
  if (pointer.isBool()) base.positionPointer = pointer.toBool();
  return base;
}

void ScrollCapture::start() {
  if (m_state != State::Idle && m_state != State::SelectingTarget) return;
  // The overlay reports no cancellation here, so a stale selection may be restarted.
  if (m_preferences) m_settings = scrollCaptureSettings(m_preferences.values(), m_settings);
  const auto generation = ++m_generation;
  setState(State::SelectingTarget);
  m_ports.selectRegion([this, generation](QRectF region) {
    if (generation != m_generation || m_state != State::SelectingTarget) return;
    targetSelected(region);
  });
}

void ScrollCapture::targetSelected(const QRectF &region) {
  if (region.width() < 16 || region.height() < 64) {
    setState(State::Idle);
    m_ports.notify(QStringLiteral("Scrolling capture"), QStringLiteral("scroll-region-too-small: choose a region at least 16 x 64 pixels."), false);
    return;
  }
  if (!m_ports.acquire([this] { cancel(); })) {
    setState(State::Idle);
    m_ports.notify(QStringLiteral("Scrolling capture"), QStringLiteral("scroll-capture-busy: another capture or dialog is in progress."), false);
    return;
  }
  m_region = region; m_manual = false; m_pointerPlaced = false; m_stopping = false;
  m_stitcher = std::make_shared<xerahs::capture::ScrollStitcher>(m_settings.stitch);
  m_view = m_ports.view(this);
  connect(m_view, &ScrollCaptureView::grabRequested, this, [this] { if (m_state == State::WaitingManual) grabFrame(); });
  connect(m_view, &ScrollCaptureView::stopRequested, this, &ScrollCapture::requestStop);
  connect(m_view, &ScrollCaptureView::cancelRequested, this, &ScrollCapture::cancel);
  connect(m_view, &ScrollCaptureView::manualRequested, this, [this] { useManual(QStringLiteral("scroll-switched-manual")); });
  m_view->open(false);
  report();
  connectInput();
}

void ScrollCapture::connectInput() {
  setState(State::ConnectingInput);
  m_remote = m_ports.remote ? m_ports.remote(this) : nullptr;
  if (!m_remote) { useManual(QStringLiteral("scroll-input-unavailable")); return; }
  const auto generation = m_generation;
  connect(m_remote, &PortalRemoteDesktop::ready, this, [this, generation] { if (generation == m_generation && m_state == State::ConnectingInput) inputReady(); });
  connect(m_remote, &PortalRemoteDesktop::unavailable, this, [this, generation](PortalRemoteDesktop::Outcome, const QString &diagnostic) {
    if (generation == m_generation && m_state == State::ConnectingInput) inputUnavailable(diagnostic);
  });
  m_remote->start();
}

void ScrollCapture::inputReady() {
  if (!m_settings.positionPointer || !m_ports.desktopOrigin) { grabFrame(); return; }
  // Home the pointer into the desktop corner, then move to the region centre.
  const QPointF origin = m_ports.desktopOrigin(), centre = m_region.center();
  const auto generation = m_generation;
  m_remote->movePointer(-100000, -100000, [this, generation, centre, origin](bool homed) {
    if (generation != m_generation || m_state != State::ConnectingInput) return;
    if (!homed) { useManual(QStringLiteral("scroll-input-unavailable")); return; }
    m_remote->movePointer(centre.x() - origin.x(), centre.y() - origin.y(), [this, generation](bool placed) {
      if (generation != m_generation || m_state != State::ConnectingInput) return;
      if (!placed) { useManual(QStringLiteral("scroll-input-unavailable")); return; }
      m_pointerPlaced = true; grabFrame();
    });
  });
}

void ScrollCapture::inputUnavailable(const QString &diagnostic) { useManual(diagnostic); }

void ScrollCapture::useManual(const QString &why) {
  if (m_state == State::Idle) return;
  const bool already = m_manual;
  m_manual = true;
  if (m_remote) { m_remote->close(); m_remote->deleteLater(); m_remote = nullptr; }
  if (m_view) m_view->setManual(true);
  if (!already) {
    const QString reason = why == u"scroll-input-denied" ? QStringLiteral("Pointer control was not allowed")
        : why == u"scroll-switched-manual" ? QStringLiteral("Switched to manual scrolling")
        : QStringLiteral("Pointer control is not available");
    m_ports.notify(QStringLiteral("Scrolling capture"), QStringLiteral("%1 (%2). Scroll the page yourself and press Grab frame for each view.").arg(reason, why), false);
  }
  if (m_state == State::ConnectingInput || m_state == State::Scrolling || m_state == State::Settling) {
    m_timer->stop();
    setState(State::WaitingManual);
  }
}

void ScrollCapture::grabFrame() {
  setState(State::Grabbing);
  if (m_view) m_view->hideForGrab(true);
  const auto generation = m_generation;
  armTimer(m_settings.hideMs, [this, generation] {
    if (generation != m_generation || m_state != State::Grabbing) return;
    m_cancelGrab = m_ports.grab(m_region, [this, generation](QImage frame, QString diagnostic) {
      if (generation != m_generation || m_state != State::Grabbing) return;
      m_cancelGrab = {};
      if (m_view) m_view->hideForGrab(false);
      frameGrabbed(std::move(frame), diagnostic);
    });
  });
}

void ScrollCapture::frameGrabbed(QImage frame, const QString &diagnostic) {
  if (!diagnostic.isEmpty() || frame.isNull()) { conclude(diagnostic.isEmpty() ? QStringLiteral("scroll-frame-unavailable") : diagnostic); return; }
  if (m_stopping) { conclude({}); return; }
  setState(State::Stitching);
  const auto generation = m_generation;
  auto stitcher = m_stitcher;
  m_work = OwnedWork::run<ScrollStatus>(this,
      [stitcher, frame = std::move(frame)](const OwnedWork::Context &) { return stitcher->add(frame); },
      [this, generation](ScrollStatus status) {
        if (generation != m_generation || m_state != State::Stitching) return;
        frameStitched(status);
      });
}

void ScrollCapture::report() {
  if (!m_view) return;
  const QString note = m_manual ? QStringLiteral("Manual scrolling.") : QStringLiteral("Wheel scrolling.");
  m_view->setProgress(frames(), m_stitcher ? m_stitcher->height() : 0, note);
}

void ScrollCapture::frameStitched(ScrollStatus status) {
  report();
  if (status != ScrollStatus::Continue) { conclude({}); return; }
  if (m_stopping) { conclude({}); return; }
  if (m_manual) { setState(State::WaitingManual); return; }
  scrollNext();
}

void ScrollCapture::scrollNext() {
  setState(State::Scrolling);
  const auto generation = m_generation;
  m_remote->scroll(m_settings.wheelSteps, [this, generation](bool ok) {
    if (generation != m_generation || m_state != State::Scrolling) return;
    if (!ok) { useManual(QStringLiteral("scroll-input-unavailable")); return; }
    setState(State::Settling);
    armTimer(m_settings.settleMs, [this, generation] { if (generation == m_generation && m_state == State::Settling) grabFrame(); });
  });
}

void ScrollCapture::requestStop() {
  switch (m_state) {
    case State::Grabbing: case State::Stitching: m_stopping = true; break;
    case State::ConnectingInput: case State::Scrolling: case State::Settling: case State::WaitingManual: conclude({}); break;
    default: break;
  }
}

void ScrollCapture::cancel() {
  if (m_state == State::Idle) return;
  if (m_state == State::SelectingTarget) { ++m_generation; setState(State::Idle); return; }
  const bool deciding = m_state == State::Deciding;
  m_work.cancel(); ++m_generation;
  if (m_cancelGrab) { auto cancel = std::move(m_cancelGrab); m_cancelGrab = {}; cancel(); }
  m_ports.notify(QStringLiteral("Scrolling capture cancelled"), deciding ? QStringLiteral("Discarded.") : QStringLiteral("scroll-cancelled: nothing was saved."), false);
  end(true);
}

void ScrollCapture::conclude(const QString &failure) {
  setState(State::Stitching);
  m_timer->stop();
  if (m_view) m_view->hideForGrab(false);
  const auto generation = m_generation;
  auto stitcher = m_stitcher;
  m_work = OwnedWork::run<ScrollOutcome>(this,
      [stitcher, failure](const OwnedWork::Context &) {
        ScrollOutcome outcome;
        outcome.status = stitcher->status();
        outcome.code = failure.isEmpty() ? stitcher->code() : failure;
        outcome.seamless = failure.isEmpty() && stitcher->seamless();
        outcome.message = describe(outcome.code, *stitcher, failure);
        outcome.image = stitcher->image();
        outcome.frames = stitcher->frames();
        return outcome;
      },
      [this, generation](ScrollOutcome outcome) {
        if (generation != m_generation || m_state != State::Stitching) return;
        resolve(outcome);
      });
}

void ScrollCapture::resolve(const ScrollOutcome &outcome) {
  if (outcome.image.isNull() || outcome.frames.isEmpty()) {
    m_ports.notify(QStringLiteral("Scrolling capture failed"), QStringLiteral("%1: no frame was captured.").arg(outcome.code.isEmpty() ? QStringLiteral("scroll-no-frames") : outcome.code), false);
    end(true);
    return;
  }
  if (outcome.seamless) {
    // The merge is trusted: publish through the normal post-capture pipeline.
    if (!m_ports.submit(outcome.image)) m_ports.notify(QStringLiteral("Scrolling capture failed"), QStringLiteral("scroll-submit-failed: the stitched image could not be handed to capture actions."), false);
    end(false);
    return;
  }
  setState(State::Deciding);
  const auto generation = m_generation;
  m_view->decide(outcome, [this, generation, outcome](ScrollChoice choice) {
    if (generation != m_generation || m_state != State::Deciding) return;
    if (choice == ScrollChoice::SavePartial) {
      if (!m_ports.submit(outcome.image)) m_ports.notify(QStringLiteral("Scrolling capture failed"), QStringLiteral("scroll-submit-failed: the stitched part could not be handed to capture actions."), false);
      end(false);
    } else if (choice == ScrollChoice::SaveFrames) {
      m_ports.saveFrames(outcome.frames, [this, generation](bool ok, QString where) {
        m_ports.notify(ok ? QStringLiteral("Scrolling capture frames saved") : QStringLiteral("Scrolling capture failed"),
                       ok ? QStringLiteral("Frames saved in %1. No stitched image was made.").arg(where) : QStringLiteral("scroll-frames-save-failed: %1").arg(where), ok);
        if (generation == m_generation && m_state == State::Deciding) end(true);
      });
    } else {
      m_ports.notify(QStringLiteral("Scrolling capture discarded"), QStringLiteral("Nothing was saved."), false);
      end(true);
    }
  });
}

void ScrollCapture::end(bool release) {
  m_timer->stop();
  if (m_remote) { m_remote->close(); m_remote->deleteLater(); m_remote = nullptr; }
  if (m_view) { m_view->close(); m_view->deleteLater(); m_view = nullptr; }
  if (m_cancelGrab) { auto cancel = std::move(m_cancelGrab); m_cancelGrab = {}; cancel(); }
  m_stitcher.reset(); m_stopping = false;
  ++m_generation;
  setState(State::Idle);
  if (release && m_ports.release) m_ports.release();
}

ScrollCapturePorts ScrollCapture::nativePorts(QObject *owner, std::function<QString()> saveFolder) {
  ScrollCapturePorts ports;
  ports.remote = [](QObject *parent) { return new PortalRemoteDesktop(parent); };
  ports.view = [](QObject *parent) -> ScrollCaptureView * { return new ScrollDialogView(parent); };
  ports.desktopOrigin = [] {
    QRect bounds;
    for (QScreen *screen : QGuiApplication::screens()) bounds = bounds.united(screen->geometry());
    return QPointF(bounds.topLeft());
  };
  ports.grab = [owner](const QRectF &region, std::function<void(QImage, QString)> done) -> std::function<void()> {
    auto *portal = new PortalScreenshot(owner);
    QObject::connect(portal, &PortalScreenshot::finished, owner,
        [portal, region, done](PortalScreenshot::Outcome outcome, const QImage &image, const QString &detail) {
          portal->setParent(nullptr); portal->retire();
          if (outcome != PortalScreenshot::Outcome::Captured) {
            done({}, !detail.isEmpty() ? detail : outcome == PortalScreenshot::Outcome::Cancelled ? QStringLiteral("scroll-grab-cancelled") : QStringLiteral("scroll-frame-unavailable"));
            return;
          }
          QRect bounds;
          for (QScreen *screen : QGuiApplication::screens()) bounds = bounds.united(screen->geometry());
          const double scale = bounds.width() > 0 ? double(image.width()) / bounds.width() : 1.0;
          const QRect crop = QRectF((region.x() - bounds.x()) * scale, (region.y() - bounds.y()) * scale,
                                    region.width() * scale, region.height() * scale).toAlignedRect().intersected(image.rect());
          if (crop.isEmpty()) { done({}, QStringLiteral("scroll-region-outside-capture")); return; }
          done(image.copy(crop), {});
        });
    portal->request();
    return [guard = QPointer<PortalScreenshot>(portal)] { if (guard) guard->cancel(); };
  };
  ports.saveFrames = [owner, saveFolder](const QVector<QImage> &frames, std::function<void(bool, QString)> done) {
    const QString folder = QDir(saveFolder()).filePath(QStringLiteral("scrolling-frames-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    OwnedWork::run<QPair<bool, QString>>(owner, [frames, folder](const OwnedWork::Context &) {
      if (!QDir().mkpath(folder)) return QPair<bool, QString>(false, QStringLiteral("could not create %1").arg(folder));
      for (int i = 0; i < frames.size(); ++i)
        if (!frames[i].save(QDir(folder).filePath(QStringLiteral("frame-%1.png").arg(i + 1, 3, 10, QLatin1Char('0')))))
          return QPair<bool, QString>(false, QStringLiteral("could not write frame %1 in %2").arg(i + 1).arg(folder));
      return QPair<bool, QString>(true, folder);
    }, [done](QPair<bool, QString> result) { done(result.first, result.second); });
  };
  return ports;
}

}  // namespace xerahs::app
