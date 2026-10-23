// CORE-015: RemoteDesktop portal transport on a private bus and the scrolling
// capture state machine with injected frame source, view and admission.
#include "PortalRemoteDesktop.h"
#include "ScrollCapture.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace xerahs::app;
using xerahs::capture::ScrollStatus;

namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
constexpr int kWidth = 160;

quint32 mix(quint32 v) { v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16; return v; }
QImage tallPage(int height, int period = 0) {
  QImage page(kWidth, height, QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < kWidth; ++x) {
      const quint32 v = mix(quint32(period ? y % period : y) * 977U + quint32(x) * 31U + 7U) & 0xff;
      page.setPixel(x, y, qRgb(int(v), int(v), int(v)));
    }
  return page;
}
}  // namespace

class FakeSession final : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Session")
public:
  explicit FakeSession(QStringList &log) : log(log) {}
  QStringList &log;
public slots:
  void Close() { log << QStringLiteral("Session.Close"); }
};

class FakeRemoteDesktop final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.RemoteDesktop")
public:
  explicit FakeRemoteDesktop(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerObject(root, this, QDBusConnection::ExportAllSlots)) qFatal("Fixture portal registration failed");
  }
  ~FakeRemoteDesktop() override { bus.unregisterObject(root); }
  QDBusConnection bus;
  QStringList log;
  QList<QPointF> motions;
  int steps = 0, axisCalls = 0;
  uint types = 0;
  uint startCode = 0;       // 1 = user declined
  uint startDevices = 2;
  bool failAxis = false;
  QString sessionPath;
  std::function<void()> onAxis;

  void respond(const QString &path, uint code, QVariantMap results) {
    QTimer::singleShot(0, this, [this, path, code, results] {
      auto signal = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
      signal << code << results; bus.send(signal);
    });
  }
  QDBusObjectPath handleFor(const QVariantMap &options) {
    QString sender = message().service().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QDBusObjectPath(root + "/request/" + sender + "/" + options.value("handle_token").toString());
  }
public slots:
  QDBusObjectPath CreateSession(const QVariantMap &options) {
    log << QStringLiteral("CreateSession");
    QString sender = message().service().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    sessionPath = root + "/session/" + sender + "/" + options.value("session_handle_token").toString();
    bus.registerObject(sessionPath, new FakeSession(log), QDBusConnection::ExportAllSlots | QDBusConnection::ExportChildObjects);
    const auto handle = handleFor(options);
    respond(handle.path(), 0, {{"session_handle", sessionPath}});
    return handle;
  }
  QDBusObjectPath SelectDevices(const QDBusObjectPath &session, const QVariantMap &options) {
    log << QStringLiteral("SelectDevices"); types = options.value("types").toUInt();
    if (session.path() != sessionPath) log << QStringLiteral("bad-session");
    const auto handle = handleFor(options); respond(handle.path(), 0, {}); return handle;
  }
  QDBusObjectPath Start(const QDBusObjectPath &, const QString &, const QVariantMap &options) {
    log << QStringLiteral("Start");
    const auto handle = handleFor(options); respond(handle.path(), startCode, startCode ? QVariantMap{} : QVariantMap{{"devices", startDevices}}); return handle;
  }
  void NotifyPointerMotion(const QDBusObjectPath &, const QVariantMap &, double dx, double dy) {
    log << QStringLiteral("motion"); motions << QPointF(dx, dy);
  }
  void NotifyPointerAxisDiscrete(const QDBusObjectPath &, const QVariantMap &, uint axis, int count) {
    if (failAxis) { sendErrorReply(QDBusError::AccessDenied, QStringLiteral("no")); return; }
    log << QStringLiteral("axis%1:%2").arg(axis).arg(count); steps += count; ++axisCalls;
    if (onAxis) onAxis();
  }
};

class FakeView final : public ScrollCaptureView {
  Q_OBJECT
public:
  using ScrollCaptureView::ScrollCaptureView;
  QStringList log;
  bool manual = false, decided = false, autoAnswer = true;
  ScrollChoice answer = ScrollChoice::Discard;
  ScrollOutcome outcome;
  int lastFrames = 0, lastHeight = 0;
  void open(bool m) override { manual = m; log << "open"; }
  void setProgress(int frames, int height, const QString &) override { lastFrames = frames; lastHeight = height; }
  void setManual(bool m) override { manual = m; log << "manual"; }
  void hideForGrab(bool hide) override { log << (hide ? "hide" : "show"); }
  void decide(const ScrollOutcome &o, std::function<void(ScrollChoice)> chosen) override {
    decided = true; outcome = o; log << "decide";
    if (autoAnswer) QTimer::singleShot(0, this, [chosen, a = answer] { chosen(a); });
  }
  void close() override { log << "close"; }
};

struct Harness {
  QImage page;
  int position = 0, grabs = 0, pageHeight = 300;
  bool admitted = true, grabFails = false;
  int released = 0, saveFramesCalls = 0, scrollsSeen = 0;
  QVector<QImage> submitted, savedFrames;
  QStringList notes;
  QPointer<FakeView> view;
  std::function<void()> cancelHook;
  std::function<void(QRectF)> regionCallback;
  QRectF region{40, 60, 200, 300};
  PortalRemoteDesktop *remote = nullptr;
  std::function<PortalRemoteDesktop *(QObject *)> makeRemote;
  ScrollChoice answer = ScrollChoice::Discard;

  ScrollCapturePorts ports() {
    ScrollCapturePorts p;
    p.selectRegion = [this](std::function<void(QRectF)> cb) { regionCallback = cb; QTimer::singleShot(0, [this, cb] { if (region.isValid()) cb(region); }); };
    p.acquire = [this](std::function<void()> hook) { if (!admitted) return false; cancelHook = hook; return true; };
    p.release = [this] { ++released; };
    p.submit = [this](const QImage &image) { submitted << image; return true; };
    p.remote = [this](QObject *parent) -> PortalRemoteDesktop * { return makeRemote ? makeRemote(parent) : nullptr; };
    p.grab = [this](const QRectF &, std::function<void(QImage, QString)> done) -> std::function<void()> {
      ++grabs;
      QTimer::singleShot(0, [this, done] {
        if (grabFails) { done({}, QStringLiteral("capture-permission-denied")); return; }
        done(page.copy(0, std::min(position, page.height() - pageHeight), page.width(), pageHeight), {});
      });
      return {};
    };
    p.saveFrames = [this](const QVector<QImage> &frames, std::function<void(bool, QString)> done) { ++saveFramesCalls; savedFrames = frames; done(true, QStringLiteral("/frames")); };
    p.notify = [this](const QString &title, const QString &body, bool) { notes << title + ": " + body; };
    p.view = [this](QObject *parent) -> ScrollCaptureView * { auto *v = new FakeView(parent); v->answer = answer; view = v; return v; };
    p.desktopOrigin = [] { return QPointF(-1920, 0); };
    return p;
  }
  bool noted(const QString &text) const { for (const auto &n : notes) if (n.contains(text)) return true; return false; }
};

ScrollCaptureSettings quick() {
  ScrollCaptureSettings s; s.settleMs = 1; s.hideMs = 0; s.wheelSteps = 5; return s;
}

class ScrollCaptureTest final : public QObject {
  Q_OBJECT
  QDBusConnection server{QStringLiteral("unused")}, client{QStringLiteral("unused")};
  std::unique_ptr<FakeRemoteDesktop> portal;

  void startPortal() { portal = std::make_unique<FakeRemoteDesktop>(server); }
  PortalRemoteDesktop *newRemote(QObject *parent) { return new PortalRemoteDesktop(client, {service, 3000}, parent); }

private slots:
  void init() {
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "scroll-test-server");
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "scroll-test-client");
    QVERIFY(server.isConnected() && client.isConnected()); QVERIFY(server.registerService(service));
    startPortal();
  }
  void cleanup() {
    portal.reset();
    server.unregisterService(service);
    QDBusConnection::disconnectFromBus("scroll-test-client");
    QDBusConnection::disconnectFromBus("scroll-test-server");
  }

  void portalSessionSelectsPointerAndIssuesAxisCalls() {
    PortalRemoteDesktop remote(client, {service, 3000});
    QSignalSpy ready(&remote, &PortalRemoteDesktop::ready), unavailable(&remote, &PortalRemoteDesktop::unavailable);
    remote.start();
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(unavailable.count(), 0);
    QCOMPARE(portal->log, (QStringList{"CreateSession", "SelectDevices", "Start"}));
    QCOMPARE(portal->types, 2u);
    bool moved = false, scrolled = false;
    remote.movePointer(12.5, 40, [&](bool ok) { moved = ok; });
    remote.scroll(3, [&](bool ok) { scrolled = ok; });
    QTRY_VERIFY(moved && scrolled);
    QCOMPARE(portal->motions, (QList<QPointF>{QPointF(12.5, 40)}));
    QCOMPARE(portal->log.mid(3), (QStringList{"motion", "axis0:3"}));
    remote.close();
    QTRY_VERIFY(portal->log.contains("Session.Close"));
  }

  void declinedConsentIsReportedAndNothingIsScrolled() {
    portal->startCode = 1;
    PortalRemoteDesktop remote(client, {service, 3000});
    QSignalSpy unavailable(&remote, &PortalRemoteDesktop::unavailable);
    remote.start();
    QTRY_COMPARE(unavailable.count(), 1);
    QCOMPARE(unavailable.first().at(0).value<PortalRemoteDesktop::Outcome>(), PortalRemoteDesktop::Outcome::Denied);
    QCOMPARE(unavailable.first().at(1).toString(), QString("scroll-input-denied"));
    bool result = true; remote.scroll(1, [&](bool ok) { result = ok; });
    QVERIFY(!result);
    QCOMPARE(portal->axisCalls, 0);
  }

  void pointerlessGrantIsTreatedAsDenied() {
    portal->startDevices = 1;  // keyboard only
    PortalRemoteDesktop remote(client, {service, 3000});
    QSignalSpy unavailable(&remote, &PortalRemoteDesktop::unavailable);
    remote.start();
    QTRY_COMPARE(unavailable.count(), 1);
    QCOMPARE(unavailable.first().at(1).toString(), QString("scroll-input-denied"));
  }

  void missingPortalIsUnavailable() {
    portal.reset(); server.unregisterService(service);
    PortalRemoteDesktop remote(client, {service, 3000});
    QSignalSpy unavailable(&remote, &PortalRemoteDesktop::unavailable);
    remote.start();
    QTRY_COMPARE(unavailable.count(), 1);
    QCOMPARE(unavailable.first().at(1).toString(), QString("scroll-input-unavailable"));
  }

  void wheelStrategyPlacesPointerAtRegionCentreThenScrollsToEndOfPage() {
    Harness h; h.page = tallPage(900);
    h.makeRemote = [this](QObject *parent) { return newRemote(parent); };
    portal->onAxis = [&] { h.position += 100; };
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    // Pointer homed, then moved to the region centre relative to the desktop origin.
    QCOMPARE(portal->motions.size(), 2);
    QCOMPARE(portal->motions[0], QPointF(-100000, -100000));
    QCOMPARE(portal->motions[1], QPointF(140 + 1920, 210));
    QVERIFY(portal->log.indexOf("axis0:5") > portal->log.lastIndexOf("motion"));
    QVERIFY(portal->axisCalls >= 6);
    QCOMPARE(h.submitted.size(), 1);
    QCOMPARE(h.submitted[0], h.page.convertToFormat(h.submitted[0].format()));
    QCOMPARE(h.released, 0);  // submit owns the release
    QTRY_VERIFY(portal->log.contains("Session.Close"));
  }

  void deniedWheelInputFallsBackToManualAndNeverStaysSilent() {
    portal->startCode = 1;
    Harness h; h.page = tallPage(900);
    h.makeRemote = [this](QObject *parent) { return newRemote(parent); };
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    QVERIFY(capture.manual());
    QVERIFY(h.noted("scroll-input-denied"));
    QCOMPARE(h.grabs, 0);
    QCOMPARE(portal->axisCalls, 0);
    // User scrolls and grabs three views, then stops.
    for (int top : {0, 200, 400}) {
      h.position = top;
      emit h.view->grabRequested();
      QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    }
    QCOMPARE(h.grabs, 3);
    QCOMPARE(h.view->lastFrames, 3);
    emit h.view->stopRequested();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    QCOMPARE(h.submitted.size(), 1);
    QCOMPARE(h.submitted[0].height(), 700);
  }

  void unavailableInputUsesManualWithoutAPortal() {
    Harness h; h.page = tallPage(900);  // makeRemote unset: no wheel input at all
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    QVERIFY(h.noted("scroll-input-unavailable"));
  }

  void failedAxisCallSwitchesToManual() {
    portal->failAxis = true;
    Harness h; h.page = tallPage(900);
    h.makeRemote = [this](QObject *parent) { return newRemote(parent); };
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    QCOMPARE(h.grabs, 1);
    QVERIFY(capture.manual());
  }

  void lowConfidenceNeverAutoPublishesAndOffersChoices_data() {
    QTest::addColumn<int>("choice");
    QTest::newRow("partial") << int(ScrollChoice::SavePartial);
    QTest::newRow("frames") << int(ScrollChoice::SaveFrames);
    QTest::newRow("discard") << int(ScrollChoice::Discard);
  }
  void lowConfidenceNeverAutoPublishesAndOffersChoices() {
    QFETCH(int, choice);
    Harness h; h.page = tallPage(900, 10);  // repeating rows: alignment is ambiguous
    h.answer = ScrollChoice(choice);
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    h.position = 0; emit h.view->grabRequested(); QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    h.position = 25; emit h.view->grabRequested();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    switch (ScrollChoice(choice)) {
      case ScrollChoice::SavePartial:
        QCOMPARE(h.submitted.size(), 1); QCOMPARE(h.submitted[0].height(), 300); QCOMPARE(h.released, 0); break;
      case ScrollChoice::SaveFrames:
        QCOMPARE(h.submitted.size(), 0); QCOMPARE(h.saveFramesCalls, 1); QCOMPARE(h.savedFrames.size(), 2); QCOMPARE(h.released, 1); break;
      case ScrollChoice::Discard:
        QCOMPARE(h.submitted.size(), 0); QCOMPARE(h.saveFramesCalls, 0); QCOMPARE(h.released, 1); break;
    }
  }

  void decisionIsWaitedForNotAssumed() {
    Harness h; h.page = tallPage(900, 10);
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    h.view->autoAnswer = false;
    h.position = 0; emit h.view->grabRequested(); QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    h.position = 25; emit h.view->grabRequested();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Deciding);
    QTest::qWait(50);
    QCOMPARE(capture.state(), ScrollCapture::State::Deciding);
    QVERIFY(h.submitted.isEmpty());
    QVERIFY(!h.view->outcome.seamless);
    QCOMPARE(h.view->outcome.code, QString("scroll-low-confidence"));
    QCOMPARE(h.released, 0);  // admission still held while the user decides
  }

  void busyControllerRefusesToStart() {
    Harness h; h.admitted = false; h.page = tallPage(900);
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    QVERIFY(h.noted("scroll-capture-busy"));
    QVERIFY(h.view.isNull());
    QCOMPARE(h.grabs, 0);
  }

  void cancelAbandonsEverythingAndReleasesAdmission() {
    Harness h; h.page = tallPage(900);
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    h.position = 0; emit h.view->grabRequested(); QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    h.cancelHook();  // controller Cancel while held
    QCOMPARE(capture.state(), ScrollCapture::State::Idle);
    QCOMPARE(h.released, 1);
    QVERIFY(h.submitted.isEmpty());
    QVERIFY(h.noted("scroll-cancelled"));
    QTest::qWait(30);
    QVERIFY(h.submitted.isEmpty());
  }

  void cancelWhileGrabIsInFlightIgnoresTheLateFrame() {
    Harness h; h.page = tallPage(900);
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    emit h.view->grabRequested();
    QCOMPARE(capture.state(), ScrollCapture::State::Grabbing);
    emit h.view->cancelRequested();
    QCOMPARE(capture.state(), ScrollCapture::State::Idle);
    QTest::qWait(50);
    QVERIFY(h.submitted.isEmpty());
    QCOMPARE(h.released, 1);
  }

  void failedGrabWithNoFramesReportsAndReleases() {
    Harness h; h.page = tallPage(900); h.grabFails = true;
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    emit h.view->grabRequested();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    QVERIFY(h.noted("capture-permission-denied"));
    QCOMPARE(h.released, 1);
    QVERIFY(h.submitted.isEmpty());
  }

  void maxHeightBoundPublishesBoundedSeamlessResult() {
    Harness h; h.page = tallPage(900);
    auto settings = quick(); settings.stitch.maxHeight = 450;
    ScrollCapture capture(h.ports(), settings);
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
    for (int top : {0, 100, 200}) { h.position = top; emit h.view->grabRequested(); QTRY_VERIFY(capture.state() != ScrollCapture::State::Grabbing && capture.state() != ScrollCapture::State::Stitching); if (capture.state() == ScrollCapture::State::Idle) break; }
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    QCOMPARE(h.submitted.size(), 1);
    QCOMPARE(h.submitted[0].height(), 450);
  }

  void restartingAfterAnOverlayCancelIsAllowed() {
    Harness h; h.page = tallPage(900); h.region = QRectF();  // overlay cancelled: callback never fires
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QCOMPARE(capture.state(), ScrollCapture::State::SelectingTarget);
    h.region = QRectF(40, 60, 200, 300);
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::WaitingManual);
  }

  void tinyRegionIsRefused() {
    Harness h; h.page = tallPage(900); h.region = QRectF(0, 0, 8, 8);
    ScrollCapture capture(h.ports(), quick());
    capture.start();
    QTRY_COMPARE(capture.state(), ScrollCapture::State::Idle);
    QVERIFY(h.noted("scroll-region-too-small"));
    QVERIFY(h.view.isNull());
  }
};

QTEST_MAIN(ScrollCaptureTest)
#include "ScrollCaptureTest.moc"
