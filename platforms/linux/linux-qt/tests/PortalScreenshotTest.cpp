// Actual native transport and capture controller on a private, non-activating
// bus. Every image, output directory and clipboard belongs to this fixture.
#include "PortalScreenshot.h"
#include "CaptureController.h"
#include "WindowDiscovery.h"
#include "NativeWorkflowExecutions.h"
#include "CaptureActions.h"
#include "shell/ActivationDispatcher.h"
#include "CaptureRecoveryActions.h"
#include "ApplicationWindow.h"
#include "ActionSelectionDialog.h"
#include "CaptureFileOutput.h"
#include "CaptureSaveAsDialog.h"
#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "capture/RegionGeometry.h"
#include "capture/LastRegionStore.h"
#include "image-editor/AnnotationDocument.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCheckBox>
#include <QColorSpace>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QPlainTextEdit>
#include <QJsonDocument>
#include <QLabel>
#include <QTreeWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QScreen>
#include <QSettings>
#include <QDialogButtonBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <memory>
#include <atomic>
#include <stdexcept>

using namespace xerahs::app;
namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QString fixtureImage(const QTemporaryDir &dir, QSize size = {8, 6}) {
  const auto path = dir.filePath("snapshot.png"); QImage image(size, QImage::Format_ARGB32);
  image.fill(QColor(42, 71, 100, 155)); image.setColorSpace(QColorSpace::SRgb);
  if (!image.save(path)) qFatal("Fixture PNG save failed"); return path;
}
class FixedRegionTargets final : public WindowDiscovery {
public:
  int calls = 0;
  std::vector<xerahs::capture::SnapTarget> targets(const xerahs::capture::Topology &, qint64) override { ++calls; return {{{10, 10, 40, 35}}}; }
  std::optional<xerahs::capture::SnapTarget> activeWindow(const xerahs::capture::Topology &, qint64) override { return std::nullopt; }
};
class FixedActiveWindow final : public WindowDiscovery {
public:
  std::optional<xerahs::capture::SnapTarget> window;
  std::vector<xerahs::capture::SnapTarget> targets(const xerahs::capture::Topology &, qint64) override { return {}; }
  std::optional<xerahs::capture::SnapTarget> activeWindow(const xerahs::capture::Topology &, qint64) override { return window; }
};
bool overlayExists() {
  for (auto *widget : QApplication::topLevelWidgets())
    if (widget->windowTitle() == u"XerahS region capture" && widget->isVisible()) return true;
  return false;
}
QWidget *overlayWidget() {
  for (auto *widget : QApplication::topLevelWidgets()) if (widget->windowTitle() == u"XerahS region capture" && widget->isVisible()) return widget;
  return nullptr;
}
struct CompositionGate {
  std::atomic<bool> entered{false}, released{false}, done{false};
  std::atomic<int> calls{0}; int threshold = 50;
};
struct ReleaseGate {
  std::shared_ptr<CompositionGate> gate;
  ~ReleaseGate() { gate->released.store(true); }
};
SnapshotComposer gatedComposition(const std::shared_ptr<CompositionGate> &gate) {
  return [gate](const QImage &image, const xerahs::capture::PhysicalRect &rectangle, const xerahs::capture::Topology &topology,
                const xerahs::capture::CompositeControl &control) {
    ++gate->calls; auto observed = control;
    observed.progress = [gate, control](int percent) {
      if (control.progress) control.progress(percent);
      if (percent >= gate->threshold && !gate->entered.exchange(true)) {
        QElapsedTimer deadline; deadline.start();
        while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      }
    };
    auto result = composeSnapshot(image, rectangle, topology, observed); gate->done.store(true); return result;
  };
}
}
class FakeRequest final : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
  FakeRequest(QString path, QStringList &closed, QObject *parent) : QObject(parent), path(std::move(path)), closed(closed) {}
  QString path; QStringList &closed;
public slots:
  void Close() { closed.append(path); }
};
class FakeScreenshotPortal final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
public:
  explicit FakeScreenshotPortal(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerObject(root, this, QDBusConnection::ExportAllSlots)) qFatal("Fixture portal registration failed");
  }
  ~FakeScreenshotPortal() override { bus.unregisterObject(root); }
  QDBusConnection bus;
  int calls = 0;
  bool early = false, legacy = false, delayed = false, invalidHandle = false;
  QString error, parentWindow, predicted, handle;
  QVariantMap options;
  QVariant uri;
  uint code = 0;
  QStringList closed;
  QDBusMessage pending;
  void respond(const QString &path = {}) {
    auto signal = QDBusMessage::createSignal(path.isEmpty() ? handle : path, requestInterface, QStringLiteral("Response"));
    QVariantMap results; if (uri.isValid()) results.insert(QStringLiteral("uri"), uri);
    signal << code << results; if (!bus.send(signal)) qFatal("Fixture response failed");
  }
  void finishHandle() { if (!bus.send(pending.createReply(QVariant::fromValue(QDBusObjectPath(handle))))) qFatal("Fixture handle reply failed"); }
public slots:
  QDBusObjectPath Screenshot(const QString &parent, const QVariantMap &requested) {
    ++calls; parentWindow = parent; options = requested;
    if (!error.isEmpty()) { sendErrorReply(error, QStringLiteral("Private fixture details must not escape")); return QDBusObjectPath("/"); }
    if (invalidHandle) return QDBusObjectPath("/");
    QString sender = message().service().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    predicted = root + "/request/" + sender + "/" + requested.value("handle_token").toString();
    handle = legacy ? root + QString("/request/legacy/result_%1").arg(calls) : predicted;
    for (const auto &path : predicted == handle ? QStringList{handle} : QStringList{predicted, handle}) {
      auto *request = new FakeRequest(path, closed, this);
      if (!bus.registerObject(path, request, QDBusConnection::ExportAllSlots)) qFatal("Fixture request registration failed");
    }
    if (early) respond();
    if (delayed) { setDelayedReply(true); pending = message(); }
    return QDBusObjectPath(handle);
  }
};
class PortalScreenshotTest final : public QObject {
  Q_OBJECT
  QDBusConnection server{QString()}, client{QString()};
  using Outcome = PortalScreenshot::Outcome;
  static Outcome outcome(const QSignalSpy &spy) { return spy.at(0).at(0).value<Outcome>(); }
  static QImage resultImage(const QSignalSpy &spy) { return spy.at(0).at(1).value<QImage>(); }
private slots:
  void lastRegionFailureStillReportsWhenWorkflowCannotEnterProcessing() {
    using namespace xerahs::workflows;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    NativeWorkflowExecutions executions(dir.filePath("executions")); const auto writer = executions.writer(); int writes = 0;
    auto executionWriter = [&](const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) { return ++writes == 3 ? xerahs::actions::JournalWrite{false, "private fixture failure"} : writer(id, expected, bytes); };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.setLastRegionWriter([](const xerahs::capture::LastRegion &) { return QString("last-region-storage-busy: Last-region metadata is locked."); });
    QSignalSpy notices(&controller, &CaptureController::notify); auto workflow = defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"save"};
    auto run = controller.prepareWorkflowCapture(workflow, QUuid::createUuid().toString(QUuid::WithoutBraces), "native-shell", executionWriter); QVERIFY(run); run();
    QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    QVERIFY(!notices.isEmpty()); QVERIFY(notices.last()[1].toString().contains("last-region-storage-busy"));
    QVERIFY(notices.last()[1].toString().contains("No post-capture action")); QVERIFY(!notices.last()[2].toBool()); QVERIFY(!QFile::exists(dir.filePath("outputs")));
  }
  void cancellationDuringMetadataWriteDoesNotStartPostCaptureActions() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.setLastRegionWriter([gate](const xerahs::capture::LastRegion &) {
      gate->entered.store(true); QElapsedTimer deadline; deadline.start(); while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      gate->done.store(true); return QString{};
    });
    QApplication::clipboard()->setText("metadata cancellation fixture"); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load()); controller.cancel();
    gate->released.store(true); QTRY_VERIFY(!controller.busy()); QVERIFY(gate->done.load());
    QVERIFY(!QFileInfo::exists(dir.filePath("outputs"))); QCOMPARE(QApplication::clipboard()->text(), QString("metadata cancellation fixture"));
  }
  void lastRegionWriterRunsOnlyAfterSuccessfulPixelsAndBeforeActions() {
    using namespace xerahs::capture;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate}; LastRegion recorded; std::atomic<bool> onWorker{false};
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.setLastRegionWriter([&, gate](const LastRegion &region) {
      recorded = region; onWorker.store(QThread::currentThread() != QApplication::instance()->thread()); ++gate->calls; gate->entered.store(true);
      QElapsedTimer deadline; deadline.start(); while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      gate->done.store(true); return QString{};
    });
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); QCOMPARE(gate->calls.load(), 0);
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load());
    QVERIFY(onWorker.load()); QCOMPARE(recorded.version, 1); QCOMPARE(recorded.rectangle, (PhysicalRect{10, 10, 40, 35}));
    QCOMPARE(recorded.displays.size(), size_t(1)); QCOMPARE(recorded.displays[0].id, QApplication::primaryScreen()->name());
    QCOMPARE(recorded.displays[0].physical, (PhysicalRect{0, 0, QApplication::primaryScreen()->geometry().width(), QApplication::primaryScreen()->geometry().height()}));
    QVERIFY(controller.busy()); QVERIFY(!controller.lastPipelineResult()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
    bool heartbeat = false; QTimer::singleShot(0, &controller, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    gate->released.store(true); QTRY_VERIFY(!controller.busy()); QCOMPARE(gate->calls.load(), 1);
    QVERIFY(controller.lastPipelineResult() && controller.lastPipelineResult()->file); QCOMPARE(QImage(controller.lastPipelineResult()->file->path).size(), QSize(30, 25));
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.cancel(); QTRY_VERIFY(!controller.busy()); QCOMPARE(gate->calls.load(), 1);
    portal.code = 2; controller.captureRegion(); QTRY_VERIFY(!controller.busy()); QCOMPARE(gate->calls.load(), 1);
  }
  void lastRegionMetadataFailureKeepsCaptureAndIsPresentInFinalReport_data() {
    QTest::addColumn<bool>("throws"); QTest::newRow("newer-store") << false; QTest::newRow("writer-exception") << true;
  }
  void lastRegionMetadataFailureKeepsCaptureAndIsPresentInFinalReport() {
    using namespace xerahs::capture; QFETCH(bool, throws);
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    const auto metadataPath = dir.filePath("last-region.json"); QFile metadata(metadataPath); QVERIFY(metadata.open(QIODevice::WriteOnly));
    const QByteArray newer = "{\"version\":2,\"future\":true}"; QCOMPARE(metadata.write(newer), newer.size()); metadata.close();
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.setLastRegionWriter([=](const LastRegion &region) -> QString {
      if (throws) throw std::runtime_error("Private metadata fixture details must not escape");
      const LastRegionStore store(metadataPath); return lastRegionStorageDiagnostic(store.save(region, store.read()));
    });
    QSignalSpy notified(&controller, &CaptureController::notify);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto receipt = controller.lastPipelineResult(); QVERIFY(receipt && receipt->file); QCOMPARE(QImage(receipt->file->path).size(), QSize(30, 25));
    QCOMPARE(bytes(metadataPath), newer); QVERIFY(!notified.isEmpty()); QVERIFY(!notified.last()[2].toBool());
    const auto message = notified.last()[1].toString(); QVERIFY(message.contains(throws ? "last-region-storage-failed" : "last-region-storage-newer-version"));
    QVERIFY(message.contains("Saved")); QVERIFY(!message.contains("Private metadata fixture"));
  }
  void failedCompositionAndDestroyedMetadataOwnerNeverStartActionsOrCallBackLate() {
    using namespace xerahs::capture;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); std::atomic<int> writes{0};
    SnapshotComposer failure = [](const QImage &, const PhysicalRect &, const Topology &, const CompositeControl &) { return ComposedSnapshot{{}, "capture-source-failed", false}; };
    CaptureController failed(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, failure);
    failed.setLastRegionWriter([&](const LastRegion &) { ++writes; return QString{}; });
    failed.captureRegion(); QTRY_VERIFY(overlayExists()); failed.pointerDown({10, 10}); failed.pointerUp({40, 35}); QTRY_VERIFY(!failed.busy()); QCOMPARE(writes.load(), 0);
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller->setLastRegionWriter([gate](const LastRegion &) {
      gate->entered.store(true); QElapsedTimer deadline; deadline.start(); while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      gate->done.store(true); return QString{};
    });
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load());
    QElapsedTimer elapsed; elapsed.start(); controller.reset(); QVERIFY(elapsed.elapsed() < 1000); QVERIFY(!overlayExists());
    gate->released.store(true); QTRY_VERIFY(gate->done.load()); QTest::qWait(30); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }

  void nativeCornerHandlesResizeWithoutPrematureCaptureAndKeepGrabOffset_data() {
    QTest::addColumn<int>("corner"); QTest::addColumn<int>("scale");
    for (int scale : {1, 2}) for (int corner = 0; corner < 4; ++corner)
      QTest::newRow(qPrintable(QString("corner-%1-scale-%2").arg(corner).arg(scale))) << corner << scale;
  }
  void nativeCornerHandlesResizeWithoutPrematureCaptureAndKeepGrabOffset() {
    QFETCH(int, corner); QFETCH(int, scale);
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size() * scale);
    const QPoint grips[]{QPoint(12, 12), QPoint(82, 12), QPoint(12, 62), QPoint(82, 62)};
    const QPoint ends[]{QPoint(2, 2), QPoint(102, 2), QPoint(2, 82), QPoint(102, 82)};
    const xerahs::capture::PhysicalRect expected[]{ {0, 0, 80, 60}, {10, 0, 100, 60}, {0, 10, 80, 80}, {10, 10, 100, 80} };
    const auto logical = expected[corner];
    const xerahs::capture::PhysicalRect physical{logical.left * scale, logical.top * scale, logical.right * scale, logical.bottom * scale};
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.setRegionSettingsResolver([] { return xerahs::capture::SessionSettings{false, false}; });
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); auto *overlay = overlayWidget(); QVERIFY(overlay);
    QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(10, 10)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(80, 60));
    QVERIFY(controller.selectionSettled()); QVERIFY(controller.busy());
    QTest::mouseMove(overlay, grips[corner]); QCOMPARE(overlay->cursor().shape(), (corner == 0 || corner == 3 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor));
    QTest::mousePress(overlay, Qt::LeftButton, {}, grips[corner]);
    QCOMPARE(controller.selection(), std::optional<xerahs::capture::PhysicalRect>({10 * scale, 10 * scale, 80 * scale, 60 * scale}));
    QTest::mouseMove(overlay, ends[corner]);
    QCOMPARE(overlay->cursor().shape(), (corner == 0 || corner == 3 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor));
    QTest::mouseRelease(overlay, Qt::LeftButton, {}, ends[corner]);
    QCOMPARE(controller.selection(), std::optional<xerahs::capture::PhysicalRect>(physical));
    QVERIFY(controller.selectionSettled()); QVERIFY(!controller.lastPipelineResult()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
    QVERIFY(overlay->accessibleDescription().contains(QString("%1 by %2").arg(physical.width()).arg(physical.height())));
    QTest::keyClick(overlay, Qt::Key_Return); QTRY_VERIFY(!controller.busy());
    const auto receipt = controller.lastPipelineResult(); QVERIFY(receipt && receipt->file); QCOMPARE(QImage(receipt->file->path).size(), QSize(int(physical.width()), int(physical.height())));
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); overlay = overlayWidget();
    QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(80, 60));
    QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20)); QTest::mouseMove(overlay, QPoint(5, 5));
    QTest::keyClick(overlay, Qt::Key_Escape); QTRY_VERIFY(!controller.busy());
    QCOMPARE(QDir(dir.filePath("outputs")).entryList({"*.png"}, QDir::Files).size(), 1);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); overlay = overlayWidget();
    QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(80, 60));
    QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(40, 35)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(50, 45));
    QCOMPARE(controller.selection(), std::optional<xerahs::capture::PhysicalRect>({40 * scale, 35 * scale, 50 * scale, 45 * scale}));
    controller.cancel(); QTRY_VERIFY(!controller.busy());
  }
  void regionOptionsFreezeBeforeAcquisitionAndAllowKeyboardAdjustment() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    xerahs::capture::SessionSettings preferences{false, false}; int reads = 0;
    controller.setRegionSettingsResolver([&] { ++reads; return preferences; });
    auto discovery = std::make_unique<FixedRegionTargets>(); auto *targets = discovery.get(); controller.setWindowDiscovery(std::move(discovery));
    controller.captureRegion(); QCOMPARE(reads, 1); preferences = {true, true};
    QTRY_VERIFY(overlayExists()); QCOMPARE(targets->calls, 0);
    controller.pointerDown({20, 20}); controller.pointerUp({20, 20}); QVERIFY(!controller.selection()); QVERIFY(controller.busy());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QVERIFY(controller.selectionSettled()); QVERIFY(controller.busy());
    QVERIFY(!controller.lastPipelineResult()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
    const xerahs::capture::Display display{"test", {0, 0, 800, 600}, {0, 0, 800, 600}, 1};
    QVERIFY(controller.key(Qt::Key_Right, Qt::NoModifier, display));
    QVERIFY(controller.key(Qt::Key_Down, Qt::ShiftModifier, display));
    QVERIFY(controller.key(Qt::Key_Left, Qt::AltModifier, display));
    const auto selection = controller.selection(); QVERIFY(selection);
    QCOMPARE(selection->left, qint64(11)); QCOMPARE(selection->top, qint64(20)); QCOMPARE(selection->width(), qint64(29)); QCOMPARE(selection->height(), qint64(25));
    QVERIFY(controller.key(Qt::Key_Return, Qt::NoModifier, display)); QTRY_VERIFY(!controller.busy());
    const auto first = controller.lastPipelineResult(); QVERIFY(first && first->file); QCOMPARE(QImage(first->file->path).size(), QSize(29, 25));
    controller.key(Qt::Key_Return, Qt::NoModifier, display); QCOMPARE(controller.lastPipelineResult(), first);
    controller.captureRegion(); QCOMPARE(reads, 2); preferences = {false, false};
    QTRY_VERIFY(overlayExists()); QCOMPARE(targets->calls, 1);
    controller.pointerMove({20, 20}); QVERIFY(controller.snapPreview());
    controller.pointerDown({20, 20}); controller.pointerUp({20, 20}); QTRY_VERIFY(!controller.busy());
    const auto second = controller.lastPipelineResult(); QVERIFY(second && second != first && second->file); QCOMPARE(QImage(second->file->path).size(), QSize(30, 25));
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); QCOMPARE(targets->calls, 1);
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QVERIFY(controller.selectionSettled());
    controller.cancel(); QTRY_VERIFY(!controller.busy()); QCOMPARE(QDir(dir.filePath("outputs")).entryList({"*.png"}, QDir::Files).size(), 2);
  }

  void unknownWorkflowOutputCannotAdmitCaptureOrWriteAnExecution() {
    using namespace xerahs::workflows; QTemporaryDir dir; FakeScreenshotPortal portal(server);
    NativeWorkflowExecutions executions(dir.filePath("executions"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    auto workflow = defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"future-native-action"};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(!controller.prepareWorkflowCapture(workflow, id, "native-shell", executions.writer()));
    QCOMPARE(portal.calls, 0); QVERIFY(!controller.busy()); QVERIFY(!controller.lastPipelineResult());
    QVERIFY(!QFile::exists(dir.filePath("outputs"))); QVERIFY(!QFile::exists(dir.filePath("executions")));
  }
  void supportedOutputWorkflowAdmissionKeepsPermissionDenialBeforeActions_data() {
    QTest::addColumn<QString>("action"); for (const auto *id : {"upload", "url-clipboard", "pin", "print"}) QTest::newRow(id) << QString::fromLatin1(id);
  }
  void supportedOutputWorkflowAdmissionKeepsPermissionDenialBeforeActions() {
    using namespace xerahs::workflows; QFETCH(QString, action); QTemporaryDir dir;
    FakeScreenshotPortal portal(server); portal.error = "org.freedesktop.portal.Error.NotAllowed";
    NativeWorkflowExecutions executions(dir.filePath("executions"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    auto w = defaultCaptureWorkflows().entries[0]; w.actions = QStringList{action}; const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto run = controller.prepareWorkflowCapture(w, id, "native-shell", executions.writer()); QVERIFY(run); run(); run = {};
    QTRY_VERIFY(!controller.busy()); QCOMPARE(portal.calls, 1); QVERIFY(!controller.lastPipelineResult()); QVERIFY(!QFile::exists(dir.filePath("outputs")));
    const auto result = executions.result(id); QCOMPARE(result.value("state"), QJsonValue("permission-denied")); QVERIFY(!result.contains("pipelineTaskId"));
  }
  void admittedPinAndPrintWorkflowsReachTheirActualNativePorts_data() {
    QTest::addColumn<QString>("action"); QTest::newRow("pin") << QString("pin"); QTest::newRow("print") << QString("print");
  }
  void admittedPinAndPrintWorkflowsReachTheirActualNativePorts() {
    using namespace xerahs::workflows; using namespace xerahs::actions; QFETCH(QString, action); QTemporaryDir dir;
    const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto original = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    NativeWorkflowExecutions executions(dir.filePath("executions")); int calls = 0; bool onGui = false; CaptureActionPorts ports;
    ports.pin = [&](const QImage &image, const CancellationToken &) { ++calls; onGui = QThread::currentThread() == qGuiApp->thread() && image.size() == QSize(30, 25); return ActionOutcome{}; };
    ports.print = [&](const LoadedSource &source, const CancellationToken &) { ++calls; onGui = QThread::currentThread() == qGuiApp->thread() && source.image.size() == QSize(30, 25); return ActionOutcome{}; };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    auto w = defaultCaptureWorkflows().entries[0]; w.actions = QStringList{action}; const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto run = controller.prepareWorkflowCapture(w, id, "native-shell", executions.writer()); QVERIFY(run); run(); run = {};
    QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto receipt = controller.lastPipelineResult(); QVERIFY(receipt); QCOMPARE(receipt->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(receipt->pipeline.orderedResults[0].action, action); QCOMPARE(receipt->pipeline.orderedResults[0].state, ActionState::Succeeded);
    QCOMPARE(calls, 1); QVERIFY(onGui); QCOMPARE(executions.result(id).value("state"), QJsonValue("succeeded")); QCOMPARE(bytes(path), original); QVERIFY(!QFile::exists(dir.filePath("outputs")));
  }

  void counterPersistenceFailureKeepsPngAndReportsPartialExecutionWithoutRetry() {
    using namespace xerahs::workflows; using namespace xerahs::actions;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    NativeWorkflowExecutions executions(dir.filePath("executions")); auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    CaptureActionPorts ports; ports.save.counterPath = dir.filePath("state/counters.json"); ports.save.counterProfile = "profile";
    const auto counterPath = ports.save.counterPath;
    ports.save.beforePublish = [counterPath](const QString &) {
      const QByteArray changed(R"({"format":"com.xerahs.app/linux-qt/filename-counters","version":1,"counters":{"profile":"41"}})");
      QFile file(counterPath); if (!file.open(QIODevice::WriteOnly) || file.write(changed) != changed.size()) throw std::runtime_error("fixture write failed");
    };
    CaptureController controller(dir.filePath("output"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    QWidget owner; CaptureRecoveryActions recovery(controller, &owner, {}, &owner); QSignalSpy notices(&controller, &CaptureController::notify);
    auto workflow = defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"save"}; workflow.output.pattern = "counter-%i{3}";
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces); auto run = controller.prepareWorkflowCapture(workflow, id, "native-shell", executions.writer()); QVERIFY(run); run(); run = {};
    QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto result = controller.lastPipelineResult(); QVERIFY(result && result->file); const auto kept = bytes(result->file->path); QVERIFY(!kept.isEmpty());
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Failed); QCOMPARE(result->pipeline.orderedResults[0].diagnostic, std::optional<QString>("counter-changed"));
    QCOMPARE(executions.result(id).value("state"), QJsonValue("partial")); QVERIFY(!notices.isEmpty()); QVERIFY(!notices.last()[2].toBool());
    QVERIFY(notices.last()[1].toString().contains("PNG saved as counter-001.png")); QVERIFY(notices.last()[1].toString().contains("filename counter update failed"));
    QVERIFY(!recovery.retryAction()->isEnabled()); QVERIFY(recovery.retryAction()->toolTip().contains("PNG was saved"));
    QVERIFY(recovery.reviewAction()->isEnabled()); QVERIFY(!controller.retryFailedActions(result)); QCOMPARE(bytes(result->file->path), kept); QCOMPARE(portal.calls, 1);
    QCOMPARE(QDir(dir.filePath("output")).entryList({"*.png"}, QDir::Files), QStringList{"counter-001.png"});
  }
  void actualApplicationLaunchAcknowledgementFindsItsDeniedExecutionHeadlessly() {
    using namespace xerahs::workflows;
    QTemporaryDir dir; FakeScreenshotPortal portal(server); portal.error = "org.freedesktop.portal.Error.NotAllowed";
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) environment.insert(key, dir.filePath(key));
    environment.insert("QT_QPA_PLATFORM", "offscreen"); environment.insert("QT_QPA_PLATFORMTHEME", ""); environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
    QVERIFY(QDir().mkpath(environment.value("XDG_RUNTIME_DIR"))); QVERIFY(QFile::setPermissions(environment.value("XDG_RUNTIME_DIR"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    auto catalog = defaultCaptureWorkflows(); auto &profile = catalog.entries[0]; profile.actions = QStringList{"save"}; profile.output.directory = dir.filePath("outputs");
    xerahs::editor::FilePreferenceStorage storage(dir.filePath("XDG_CONFIG_HOME/com.xerahs.app/linux-qt/capture-workflows.json"));
    CaptureWorkflowStore saved(storage); QVERIFY(saved.save(catalog, saved.revision()).saved);
    QProcess primary; primary.setProcessEnvironment(environment);
    const auto cleanup = qScopeGuard([&] { primary.terminate(); if (!primary.waitForFinished(1000)) { primary.kill(); primary.waitForFinished(1000); } });
    primary.start(QCoreApplication::applicationDirPath() + "/xerahs", {"--run-workflow", profile.id.toString(QUuid::WithoutBraces), "--json"});
    QVERIFY(primary.waitForStarted(1000)); QTRY_VERIFY(primary.canReadLine());
    const auto first = QJsonDocument::fromJson(primary.readLine()).object(); QVERIFY(first.value("ok").toBool()); QCOMPARE(first.value("state"), QJsonValue("accepted")); QCOMPARE(first.value("delivery"), QJsonValue("started"));
    NativeWorkflowExecutions executions(dir.filePath("XDG_STATE_HOME/com.xerahs.app/workflow-executions")); const auto firstId = first.value("requestId").toString(); QVERIFY(!QUuid(firstId).isNull());
    QTRY_COMPARE(executions.result(firstId).value("state"), QJsonValue("permission-denied")); QCOMPARE(executions.result(firstId).value("origin"), QJsonValue("command-line")); QCOMPARE(portal.calls, 1);
    // The same installed binary forwards the next request through the actual
    // primary, then looks up that acknowledgement without graphical startup.
    QProcess secondary; secondary.setProcessEnvironment(environment); secondary.start(QCoreApplication::applicationDirPath() + "/xerahs", {"--run-workflow", profile.id.toString(QUuid::WithoutBraces), "--json"});
    QTRY_COMPARE(secondary.state(), QProcess::NotRunning); QCOMPARE(secondary.exitCode(), 0);
    const auto second = QJsonDocument::fromJson(secondary.readAllStandardOutput()).object(); QCOMPARE(second.value("delivery"), QJsonValue("running-instance"));
    const auto secondId = second.value("requestId").toString(); QVERIFY(secondId != firstId); QVERIFY(!QUuid(secondId).isNull());
    QTRY_COMPARE(executions.result(secondId).value("state"), QJsonValue("permission-denied")); QCOMPARE(executions.result(secondId).value("origin"), QJsonValue("external-activation")); QCOMPARE(portal.calls, 2);
    environment.insert("QT_QPA_PLATFORM", "xcb"); environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + dir.filePath("no-query-bus"));
    QProcess lookup; lookup.setProcessEnvironment(environment); lookup.start(QCoreApplication::applicationDirPath() + "/xerahs", {"--workflow-result", secondId, "--non-interactive", "--json"});
    QVERIFY(lookup.waitForFinished(5000)); QCOMPARE(lookup.exitCode(), 0); QCOMPARE(lookup.exitStatus(), QProcess::NormalExit);
    const auto output = lookup.readAllStandardOutput(); QCOMPARE(output.count('\n'), 1); const auto result = QJsonDocument::fromJson(output).object();
    QCOMPARE(result.value("requestId"), QJsonValue(secondId)); QCOMPARE(result.value("state"), QJsonValue("permission-denied")); QVERIFY(result.value("terminal").toBool()); QVERIFY(!result.contains("pipelineTaskId"));
    QVERIFY(!output.contains(dir.path().toUtf8())); QVERIFY(lookup.readAllStandardError().isEmpty()); QVERIFY(!QFile::exists(dir.filePath("outputs"))); QCOMPARE(portal.calls, 2); QCOMPARE(primary.state(), QProcess::Running);
  }
  void admittedExecutionLinksRealPipelineBeforeOutputAndSurvivesRestart() {
    using namespace xerahs::workflows; using namespace xerahs::shell;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto source = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    NativeWorkflowExecutions executions(dir.filePath("executions")); auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("pipelines"));
    auto workflow = defaultCaptureWorkflows().entries[0]; workflow.name = "Frozen private profile"; workflow.actions = QStringList{"save"}; workflow.output.directory = dir.filePath("original"); workflow.output.pattern = "shot-%width";
    const auto request = makeActivationRequest(ActivationAction::RunWorkflow, {}, workflow.id.toString(QUuid::WithoutBraces)); std::atomic<bool> linkedBeforeOutput{false};
    CaptureActionPorts ports; ports.save.publicationIntent = [&](const QString &, bool) {
      const auto query = executions.result(request.id); linkedBeforeOutput.store(query.value("lastRecordedState") == "processing" && !query.value("pipelineTaskId").toString().isEmpty());
    };
    CaptureController controller(dir.filePath("fallback"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    ActivationOperations ops; ops.captureBusy = [&] { return controller.busy(); };
    ops.prepareCapture = [&](const ActivationRequest &incoming) { return controller.prepareWorkflowCapture(workflow, incoming.id, "external-activation", executions.writer()); };
    ActivationDispatcher dispatcher(ops); QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted);
    QCOMPARE(portal.calls, 0); QCOMPARE(executions.result(request.id).value("lastRecordedState"), QJsonValue("admitted"));
    workflow.output.directory = dir.filePath("changed"); workflow.output.pattern = "changed"; workflow.name = "Changed";
    QVERIFY(dispatcher.dispatch(request).isEmpty()); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto receipt = controller.lastPipelineResult(); QVERIFY(receipt && receipt->file); QCOMPARE(receipt->file->path, dir.filePath("original/shot-30.png")); QCOMPARE(QImage(receipt->file->path).size(), QSize(30, 25));
    QCOMPARE(receipt->canonicalResult().value("extensions").toObject().value("com.xerahs.app").toObject().value("workflow_execution_id"), QJsonValue(request.id));
    QVERIFY(linkedBeforeOutput.load()); const auto query = NativeWorkflowExecutions(dir.filePath("executions")).result(request.id);
    QCOMPARE(query.value("state"), QJsonValue("succeeded")); QCOMPARE(query.value("pipelineTaskId"), QJsonValue(receipt->pipeline.taskId)); QCOMPARE(query.value("steps").toArray().size(), 2);
    QCOMPARE(query.value("origin"), QJsonValue("external-activation")); QVERIFY(!QJsonDocument(query).toJson().contains("Frozen private")); QVERIFY(!QJsonDocument(query).toJson().contains(dir.path().toUtf8()));
    const auto stored = journal->list(); QCOMPARE(stored.entries.size(), size_t(1)); QVERIFY(stored.entries[0].parsed.record); QVERIFY(stored.entries[0].parsed.record->terminal); QCOMPARE(stored.entries[0].taskId, receipt->pipeline.taskId);
    QCOMPARE(stored.entries[0].parsed.record->report.value("extensions").toObject().value("com.xerahs.app").toObject().value("workflow_execution_id"), QJsonValue(request.id));
    QCOMPARE(bytes(path), source); QVERIFY(!QFile::exists(dir.filePath("changed"))); QCOMPARE(portal.calls, 1);
  }
  void explicitRetryKeepsExecutionReferenceWithoutRewritingOriginalOutcome() {
    using namespace xerahs::workflows; using namespace xerahs::actions;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    NativeWorkflowExecutions executions(dir.filePath("executions")); auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("pipelines"));
    bool failCopy = true; int copies = 0; CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copies; return failCopy ? ActionOutcome{ActionState::Failed, QStringLiteral("action-failed"), {}} : ActionOutcome{}; };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    auto workflow = defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"save", "image-clipboard"};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces); auto run = controller.prepareWorkflowCapture(workflow, id, "global-shortcut", executions.writer()); QVERIFY(run); run(); run = {};
    QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto original = controller.lastPipelineResult(); QVERIFY(original && original->file); const auto savedBytes = bytes(original->file->path); const auto recordBytes = bytes(dir.filePath("executions/" + id + ".json"));
    QCOMPARE(executions.result(id).value("state"), QJsonValue("partial")); failCopy = false; QVERIFY(controller.retryFailedActions(original)); QTRY_VERIFY(!controller.busy());
    const auto retry = controller.lastPipelineResult(); QVERIFY(retry && retry != original); QCOMPARE(retry->pipeline.workflowExecutionId, id); QVERIFY(retry->pipeline.retry); QCOMPARE(retry->pipeline.retry->originalTaskId, original->pipeline.taskId);
    QCOMPARE(retry->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(retry->pipeline.orderedResults[0].action, QString("image-clipboard")); QCOMPARE(retry->pipeline.orderedResults[0].state, ActionState::Succeeded);
    QCOMPARE(bytes(original->file->path), savedBytes); QCOMPARE(bytes(dir.filePath("executions/" + id + ".json")), recordBytes); QCOMPARE(executions.result(id).value("pipelineTaskId"), QJsonValue(original->pipeline.taskId)); QCOMPARE(portal.calls, 1); QCOMPARE(copies, 2);
    const auto records = journal->list(); QCOMPARE(records.entries.size(), size_t(2)); for (const auto &entry : records.entries) { QVERIFY(entry.parsed.record); QCOMPARE(entry.parsed.record->report.value("extensions").toObject().value("com.xerahs.app").toObject().value("workflow_execution_id"), QJsonValue(id)); }
  }
  void executionPreservesPrePipelineOutcomes_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"denied", "portal-cancel", "cancel-before-acquisition", "cancel-pending", "cancel-selection", "source-failure", "composition-failure", "destroy-pending"}) QTest::newRow(mode) << QString::fromLatin1(mode);
  }
  void executionPreservesPrePipelineOutcomes() {
    using namespace xerahs::workflows;
    QFETCH(QString, mode); QTemporaryDir dir; FakeScreenshotPortal portal(server);
    const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto source = bytes(path);
    portal.uri = QUrl::fromLocalFile(path).toString();
    if (mode == "denied") portal.error = "org.freedesktop.portal.Error.NotAllowed";
    if (mode == "source-failure") portal.error = "org.freedesktop.portal.Error.Failed";
    if (mode == "portal-cancel") { portal.early = true; portal.code = 1; }
    if (mode == "cancel-selection" || mode == "composition-failure") portal.early = true;
    NativeWorkflowExecutions executions(dir.filePath("executions")); auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("pipelines"));
    auto workflow = defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"save"};
    SnapshotComposer composer; if (mode == "composition-failure") composer = [](const QImage &, const xerahs::capture::PhysicalRect &, const xerahs::capture::Topology &, const xerahs::capture::CompositeControl &) { ComposedSnapshot result; result.diagnostic = "capture-source-failed"; return result; };
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, composer, CaptureActionPorts{}, journal);
    QApplication::clipboard()->setText("untouched execution fixture clipboard");
    if (mode == "cancel-before-acquisition") connect(controller.get(), &CaptureController::busyChanged, controller.get(), [&](bool busy) { if (busy) controller->cancel(); });
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces); auto run = controller->prepareWorkflowCapture(workflow, id, "native-shell", executions.writer()); QVERIFY(run); run(); run = {};
    if (mode == "cancel-pending" || mode == "destroy-pending") {
      QTRY_COMPARE(portal.calls, 1);
      if (mode == "cancel-pending") { controller->cancel(); controller->cancel(); } else controller.reset();
    }
    if (mode == "cancel-selection" || mode == "composition-failure") {
      QTRY_VERIFY(overlayExists()); if (mode == "cancel-selection") controller->cancel(); else { controller->pointerDown({10, 10}); controller->pointerUp({40, 35}); }
    }
    if (controller) { QTRY_VERIFY(!controller->busy()); QVERIFY(!controller->lastPipelineResult()); }
    const auto query = NativeWorkflowExecutions(dir.filePath("executions")).result(id);
    QCOMPARE(query.value("state"), QJsonValue(mode == "denied" ? "permission-denied" : mode == "source-failure" || mode == "composition-failure" ? "failed" : mode == "destroy-pending" ? "outcome-unconfirmed" : "cancelled"));
    QVERIFY(!query.contains("pipelineTaskId")); QCOMPARE(query.value("terminal").toBool(), mode != "destroy-pending");
    QVERIFY(!QFile::exists(dir.filePath("outputs"))); QVERIFY(!QFile::exists(dir.filePath("pipelines"))); QVERIFY(!overlayExists()); QCOMPARE(bytes(path), source);
    QCOMPARE(QApplication::clipboard()->text(), QString("untouched execution fixture clipboard"));
    if (mode == "cancel-before-acquisition") QCOMPARE(portal.calls, 0);
  }
  void executionStorageFailureStopsNewActionsAndPreservesCompletedOutput_data() {
    QTest::addColumn<int>("failWrite"); QTest::newRow("admission") << 1; QTest::newRow("start") << 2; QTest::newRow("pipeline-link") << 3; QTest::newRow("terminal") << 4;
  }
  void executionStorageFailureStopsNewActionsAndPreservesCompletedOutput() {
    using namespace xerahs::workflows;
    QFETCH(int, failWrite); QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    NativeWorkflowExecutions executions(dir.filePath("executions")); const auto writer = executions.writer(); int writes = 0;
    auto guardedWriter = [&](const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) { return ++writes == failWrite ? xerahs::actions::JournalWrite{false, "private exception"} : writer(id, expected, bytes); };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }); QSignalSpy notices(&controller, &CaptureController::notify);
    auto workflow = defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"save"}; const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto run = controller.prepareWorkflowCapture(workflow, id, "command-line", guardedWriter);
    if (failWrite == 1) { QVERIFY(!run); QCOMPARE(portal.calls, 0); return; }
    QVERIFY(run); run(); run = {};
    if (failWrite > 2) { QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); }
    QTRY_VERIFY(!controller.busy()); QVERIFY(!notices.isEmpty()); bool storageFailure = false;
    for (const auto &notice : notices) storageFailure |= !notice[2].toBool() && notice[1].toString().contains("storage"); QVERIFY(storageFailure);
    QCOMPARE(executions.result(id).value("state"), QJsonValue("outcome-unconfirmed"));
    if (failWrite == 4) { QVERIFY(!notices.last()[2].toBool()); QVERIFY(notices.last()[1].toString().contains("storage")); const auto receipt = controller.lastPipelineResult(); QVERIFY(receipt && receipt->file); QVERIFY(QFile::exists(receipt->file->path)); QCOMPARE(QImage(receipt->file->path).size(), QSize(30, 25)); }
    else { QVERIFY(!QFile::exists(dir.filePath("outputs"))); QVERIFY(!controller.lastPipelineResult()); }
    if (failWrite == 2) QCOMPARE(portal.calls, 0);
  }
  void partialCompanionSaveHasHonestNotificationAndGuardedSharedRetry() {
    using namespace xerahs::actions;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto borrowed = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    int encodes = 0, annotations = 0, consumers = 0; bool collision = false; CaptureActionPorts ports;
    ports.annotation = [&](const LoadedSource &source, const CancellationToken &) {
      ++annotations; xerahs::editor::EditorSession session(*source.document); session.createRectangle({4, 4}, {20, 18});
      return CaptureAnnotationChoice{session.document(), {}, true};
    };
    ports.save.encode = [&](const QImage &image, QIODevice *output) { ++encodes; return image.save(output, "PNG"); };
    ports.save.beforePublish = [&](const QString &target) { if (target.endsWith(".png")) collision = QDir().mkdir(target + ".xann"); };
    ports.fileOutput = [&](const QString &, const CaptureFile &, const CancellationToken &) { ++consumers; return ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("output"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    ApplicationWindow home({}); controller.setActionSelectionParent(&home); connect(&controller, &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    CaptureRecoveryActions recovery(controller, &home, {}); QSignalSpy notifications(&controller, &CaptureController::notify);
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); auto *selection = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    selection->findChild<QCheckBox *>("capture-action-annotation")->setChecked(true);
    selection->findChild<QCheckBox *>("capture-action-path-clipboard")->setChecked(true);
    selection->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false); selection->accept();
    QTRY_VERIFY(!controller.busy()); const auto source = controller.lastPipelineResult(); QVERIFY(collision && source->file && source->annotationDocument);
    QVERIFY(recovery.retryAction()->isEnabled()); QVERIFY(notifications.last()[1].toString().contains("editable companion failed")); QVERIFY(!notifications.last()[2].toBool());
    const auto original = bytes(source->file->path); recovery.retryAction()->trigger(); QTRY_VERIFY(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()));
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box->text().contains("retries only its .xann companion"));
    QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); QTest::keyClick(box, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), source); QCOMPARE(encodes, 1);
    QVERIFY(QDir().rmdir(source->file->path + ".xann")); recovery.retryAction()->trigger(); QTRY_VERIFY(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()));
    box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); box->button(QMessageBox::Retry)->click();
    QTRY_VERIFY(controller.lastPipelineResult() != source); QTRY_VERIFY(!controller.busy()); const auto repaired = controller.lastPipelineResult();
    QCOMPARE(repaired->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(repaired->pipeline.orderedResults[0].state, ActionState::Succeeded);
    QCOMPARE(encodes, 1); QCOMPARE(annotations, 1); QCOMPARE(consumers, 1); QCOMPARE(portal.calls, 1); QCOMPARE(bytes(path), borrowed); QCOMPARE(bytes(source->file->path), original);
    QVERIFY(xerahs::editor::readXann(bytes(source->file->path + ".xann")).document); QCOMPARE(repaired->file->creatorTaskId, source->pipeline.taskId);
  }
  void selectedAnnotationReturnsToCaptureAndRecoveryKeepsTheEditableDocument() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto borrowed = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); CaptureActionPorts ports;
    ports.save.encode = [](const QImage &, QIODevice *) { return false; };
    CaptureController controller(dir.filePath("output"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    ApplicationWindow home({}); controller.setActionSelectionParent(&home); connect(&controller, &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    int flattened = 0, editable = 0; std::optional<xerahs::editor::AnnotationDocument> reopened;
    CaptureRecoveryActions recovery(controller, &home, [&](const QImage &, QWidget *) { ++flattened; return true; });
    recovery.setAnnotationOpener([&](const xerahs::editor::AnnotationDocument &document, QWidget *) { ++editable; reopened = document; return true; });
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); auto *selection = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    auto *annotation = selection->findChild<QCheckBox *>("capture-action-annotation"); QVERIFY(annotation && annotation->isEnabled()); annotation->setChecked(true);
    selection->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false); selection->accept();
    QTRY_VERIFY(qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget())); auto *editor = qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget());
    QCOMPARE(editor->parentWidget(), &home); QVERIFY(!overlayExists()); auto *canvas = editor->findChild<EditorCanvas *>(); QVERIFY(canvas);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({4, 4}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({20, 18}).toPoint()); editor->accept(); QTRY_VERIFY(!controller.busy());
    const auto result = controller.lastPipelineResult(); QVERIFY(result->annotationDocument); QCOMPARE(result->annotationDocument->annotations.size(), size_t(1)); QVERIFY(result->files.empty());
    QCOMPARE(result->pipeline.finalWorkingMedia, QString("annotation")); QVERIFY(recovery.editAction()->isEnabled()); recovery.editAction()->trigger();
    QCOMPARE(editable, 1); QCOMPARE(flattened, 0); QVERIFY(reopened); QCOMPARE(reopened->sourceImagePng, result->annotationDocument->sourceImagePng);
    QCOMPARE(reopened->annotations.size(), size_t(1)); QCOMPARE(portal.calls, 1); QCOMPARE(bytes(path), borrowed); QCOMPARE(controller.lastPipelineResult(), result);
    recovery.setAnnotationOpener({}); QVERIFY(!recovery.editAction()->isEnabled());
  }
  void nativeSaveAsAndSharedRetryUseRetainedPixelsWithoutAnotherPortalCapture() {
    QTemporaryDir dir, preferences;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, preferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto borrowed = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int encoded = 0, outputs = 0; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *out) { return ++encoded > 1 && image.save(out, "PNG"); };
    ports.fileOutput = [&](const QString &, const CaptureFile &, const xerahs::actions::CancellationToken &) { ++outputs; return xerahs::actions::ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    ApplicationWindow home({}); controller.setActionSelectionParent(&home); connect(&controller, &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    CaptureRecoveryActions recovery(controller, &home, {});
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); auto *selection = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    selection->findChild<QCheckBox *>("capture-action-save")->setChecked(false); selection->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false);
    selection->findChild<QCheckBox *>("capture-action-save-as")->setChecked(true); selection->findChild<QCheckBox *>("capture-action-path-clipboard")->setChecked(true); selection->accept();
    QTRY_VERIFY(qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget())); auto *dialog = qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget());
    QCOMPARE(dialog->parentWidget(), &home); QVERIFY(!overlayExists()); auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
    buttons->button(QDialogButtonBox::Save)->setFocus(); dialog->selectFile(dir.filePath("first.png"));
    QCOMPARE(dialog->selectedFiles().value(0), dir.filePath("first.png")); dialog->accept(); QTRY_VERIFY(!controller.busy());
    const auto original = controller.lastPipelineResult(); QVERIFY(original->files.empty()); QCOMPARE(original->workingMedia.size(), QSize(30, 25)); QCOMPARE(outputs, 0);
    QVERIFY(recovery.retryAction()->isEnabled()); recovery.retryAction()->trigger(); QTRY_VERIFY(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()));
    auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(confirmation->text().contains("Save image as new PNG")); confirmation->button(QMessageBox::Retry)->click();
    QTRY_VERIFY(qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget())); dialog = qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget());
    QCOMPARE(dialog->parentWidget(), &home); buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
    buttons->button(QDialogButtonBox::Save)->setFocus(); dialog->selectFile(dir.filePath("recovered.png"));
    QCOMPARE(dialog->selectedFiles().value(0), dir.filePath("recovered.png")); dialog->accept(); QTRY_VERIFY(!controller.busy());
    const auto result = controller.lastPipelineResult(); QCOMPARE(result->files.size(), size_t(1)); QCOMPARE(result->file->artifact, QString("save-as.file"));
    QCOMPARE(result->file->path, dir.filePath("recovered.png"));
    QCOMPARE(result->pipeline.orderedResults[1].inputArtifact, std::optional<QString>{"save-as.file"}); QCOMPARE(result->file->creatorTaskId, result->pipeline.taskId);
    QCOMPARE(QImage(result->file->path), original->workingMedia); QCOMPARE(portal.calls, 1); QCOMPARE(outputs, 1); QCOMPARE(bytes(path), borrowed);
    QVERIFY(journal->list().entries[0].parsed.record); QVERIFY(journal->list().entries[0].parsed.record->terminal);
  }
  void selectedFileOutputsAndSharedRetryKeepTheSameSavedArtifactWithoutNewCapture() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); QStringList outputs; bool denied = true;
    CaptureActionPorts ports; ports.fileOutput = [&](const QString &action, const CaptureFile &file, const xerahs::actions::CancellationToken &) {
      outputs.append(action);
      if (action == "reveal") return denied ? xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "reveal-unavailable", {}} : xerahs::actions::ActionOutcome{};
      copyCaptureFile(action, file); return xerahs::actions::ActionOutcome{}; // fixture acknowledges its local clipboard payload
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    ApplicationWindow home({}); controller.setActionSelectionParent(&home); connect(&controller, &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    CaptureRecoveryActions recovery(controller, &home, {});
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); auto *dialog = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    dialog->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false);
    for (const auto &id : {"file-clipboard", "path-clipboard", "reveal"}) {
      auto *box = dialog->findChild<QCheckBox *>(QString("capture-action-") + id); QVERIFY(box && box->isEnabled()); box->setChecked(true);
    }
    dialog->accept(); QTRY_VERIFY(!controller.busy()); const auto original = controller.lastPipelineResult(); QVERIFY(original->file);
    const auto saved = bytes(original->file->path); QCOMPARE(outputs, (QStringList{"file-clipboard", "path-clipboard", "reveal"}));
    QCOMPARE(QApplication::clipboard()->text(), original->file->path); QCOMPARE(original->pipeline.orderedResults.size(), size_t(5));
    QVERIFY(recovery.retryAction()->isEnabled()); recovery.retryAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(confirmation); QVERIFY(confirmation->text().contains("Show saved file in folder"));
    QTest::keyClick(confirmation, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget()); QCOMPARE(outputs.size(), 3);
    denied = false; recovery.retryAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); confirmation->button(QMessageBox::Retry)->click(); QTRY_VERIFY(!controller.busy());
    const auto result = controller.lastPipelineResult(); QCOMPARE(outputs, (QStringList{"file-clipboard", "path-clipboard", "reveal", "reveal"}));
    QCOMPARE(portal.calls, 1); QCOMPARE(result->file->path, original->file->path); QCOMPARE(bytes(original->file->path), saved);
    QCOMPARE(result->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(result->pipeline.orderedResults[0].inputArtifact, std::optional<QString>{"save.file"});
    QCOMPARE(journal->list().entries.size(), size_t(2));
  }
  void perRunSelectionCancelsBeforeEffectsAndDoesNotChangeTheNextDefaultCapture() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) { ++copies; return xerahs::actions::ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    QApplication::clipboard()->setText("selection fixture clipboard");
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget()));
    QVERIFY(controller.busy()); QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QVERIFY(!QFileInfo::exists(dir.filePath("state")));
    QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape); QTRY_VERIFY(!controller.busy());
    const auto cancelled = controller.lastPipelineResult(); QVERIFY(cancelled); QCOMPARE(cancelled->pipeline.state, xerahs::actions::PipelineState::Cancelled);
    QVERIFY(!cancelled->file); QCOMPARE(cancelled->workingMedia.size(), QSize(30, 25)); QCOMPARE(copies, 0); QCOMPARE(portal.calls, 1);
    QCOMPARE(QApplication::clipboard()->text(), QString("selection fixture clipboard"));
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    QVERIFY(!QApplication::activeModalWidget()); const auto next = controller.lastPipelineResult(); QVERIFY(next->file); QCOMPARE(next->pipeline.orderedResults.size(), size_t(2));
    QCOMPARE(next->pipeline.orderedResults[0].action, QString("save")); QCOMPARE(copies, 1); QCOMPARE(portal.calls, 2); QCOMPARE(bytes(path), before);
  }
  void acceptedSelectionKeepsCaptureIdentityBusyAdmissionAndActualArtifactFacts() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) { ++copies; return xerahs::actions::ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, SnapshotComposer{}, ports, journal);
    ApplicationWindow home({}); home.showMaximized(); home.showMinimized();
    controller->setActionSelectionParent(&home);
    connect(controller.get(), &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    controller->captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget()));
    auto *dialog = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    QVERIFY(home.isVisible()); QVERIFY(!home.isMinimized()); QVERIFY(home.isMaximized());
    controller->captureRegion(); controller->captureRegionWithActions(); QCOMPARE(portal.calls, 1);
    auto *copy = dialog->findChild<QCheckBox *>("capture-action-image-clipboard"); QVERIFY(copy); copy->setFocus(); QTest::keyClick(copy, Qt::Key_Space);
    dialog->accept(); QTRY_VERIFY(!controller->busy()); QTRY_VERIFY(home.isActiveWindow()); const auto result = controller->lastPipelineResult(); QVERIFY(result->file); QCOMPARE(copies, 0);
    QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(result->pipeline.orderedResults.size(), size_t(2));
    QCOMPARE(result->pipeline.orderedResults[0].action, QString("after-capture-window")); QCOMPARE(result->pipeline.orderedResults[1].action, QString("save"));
    QCOMPARE(journal->list().entries[0].parsed.record->selected, (QStringList{"after-capture-window", "save"}));
    controller->captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller->pointerDown({5, 5}); controller->pointerUp({15, 15});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); QPointer<QWidget> pending(QApplication::activeModalWidget()); controller.reset();
    QVERIFY(!pending); QTest::qWait(30); QCOMPARE(copies, 0); QCOMPARE(journal->list().entries.size(), size_t(1)); QVERIFY(QFileInfo::exists(result->file->path));
  }
  void nativeRetryUsesSharedKeyboardApprovalKeepsSuccessfulSaveAndStartsNoNewPortal() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto sourceBytes = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    int encoded = 0, copied = 0; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *output) { ++encoded; return image.save(output, "PNG"); };
    ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) {
      ++copied; return copied == 1 ? xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", {}} : xerahs::actions::ActionOutcome{};
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    QAction cancel(QStringLiteral("Cancel capture")); cancel.setEnabled(false); connect(&cancel, &QAction::triggered, &controller, &CaptureController::cancel);
    connect(&controller, &CaptureController::busyChanged, &cancel, [&](bool busy) { cancel.setEnabled(busy); });
    ApplicationWindow home({{"cancel-capture", &cancel}}); CaptureRecoveryActions recovery(controller, &home, {});
    home.addCaptureRecoveryActions(recovery.editAction(), recovery.discardAction(), recovery.reviewAction(), recovery.retryAction()); home.activate();
    auto *retry = home.findChild<QToolButton *>("retry-retained-capture"); QVERIFY(retry && !retry->isEnabled());
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto original = controller.lastPipelineResult(); QVERIFY(original && original->file); const auto saved = bytes(original->file->path);
    QTRY_VERIFY(retry->isEnabled()); retry->setFocus(); QTest::keyClick(retry, Qt::Key_Space); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); QCOMPARE(box->objectName(), QString("retryRetainedCaptureDialog"));
    QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); QTest::keyClick(box, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), original); QCOMPARE(copied, 1); QCOMPARE(encoded, 1);
    retry->setFocus(); QTest::keyClick(retry, Qt::Key_Space); QTRY_VERIFY(QApplication::activeModalWidget());
    box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Retry)->click();
    QTRY_VERIFY(!controller.busy()); const auto result = controller.lastPipelineResult(); QVERIFY(result != original && result->pipeline.retry);
    QCOMPARE(result->pipeline.retry->originalTaskId, original->pipeline.taskId); QCOMPARE(result->pipeline.orderedResults.size(), size_t(1));
    QCOMPARE(copied, 2); QCOMPARE(encoded, 1); QCOMPARE(portal.calls, 1); QCOMPARE(bytes(original->file->path), saved); QCOMPARE(bytes(path), sourceBytes);
    QVERIFY(!retry->isEnabled()); QCOMPARE(journal->list().entries.size(), size_t(2));
    recovery.reviewAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *review = QApplication::activeModalWidget(); auto *lineage = review->findChild<QLabel *>("captureRetryLineage"); QVERIFY(lineage);
    QCOMPARE(lineage->textFormat(), Qt::PlainText); QVERIFY(lineage->text().contains("Linked retry 1")); QVERIFY(lineage->text().contains("save"));
    QTest::keyClick(review, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), result); QCOMPARE(copied, 2); QCOMPARE(bytes(original->file->path), saved);
  }
  void staleRetryConfirmationCannotRepeatActionsForANewerReceipt() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) {
      ++copies; return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", {}};
    };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    CaptureRecoveryActions recovery(controller, nullptr, {}); QSignalSpy notices(&recovery, &CaptureRecoveryActions::notify);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto first = controller.lastPipelineResult(); recovery.retryAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QMessageBox> approval = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(approval);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    const auto second = controller.lastPipelineResult(); QVERIFY(second != first); approval->button(QMessageBox::Retry)->click(); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), second); QCOMPARE(copies, 2); QCOMPARE(portal.calls, 2); QCOMPARE(notices.count(), 1); QVERIFY(!notices.first().last().toBool());
    QVERIFY(!controller.retryFailedActions(first)); QCOMPARE(copies, 2);
  }
  void cancellationAtRetryAvailabilityKeepsPixelsAndSuccessfulFileAndOwnerDestructionCannotStartWork() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) {
      ++copies; return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", {}};
    };
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr,
        [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, SnapshotComposer{}, ports);
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35}); QTRY_VERIFY(!controller->busy());
    const auto original = controller->lastPipelineResult(); const auto saved = bytes(original->file->path);
    const auto connection = connect(controller.get(), &CaptureController::busyChanged, controller.get(), [&](bool busy) { if (busy) controller->cancel(); });
    QVERIFY(controller->retryFailedActions(original)); QTRY_VERIFY(!controller->busy()); disconnect(connection);
    const auto cancelled = controller->lastPipelineResult(); QCOMPARE(cancelled->pipeline.state, xerahs::actions::PipelineState::Cancelled);
    QCOMPARE(cancelled->workingMedia, original->workingMedia); QCOMPARE(bytes(cancelled->file->path), saved); QCOMPARE(copies, 1); QCOMPARE(portal.calls, 1);
    // Restore a definitive failed receipt through a second isolated capture,
    // then destroy the controller synchronously at admission of its retry.
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35}); QTRY_VERIFY(!controller->busy());
    const auto expected = controller->lastPipelineResult(); connect(controller.get(), &CaptureController::busyChanged, [&](bool busy) { if (busy) controller.reset(); });
    controller->retryFailedActions(expected); QVERIFY(!controller); QTest::qWait(30); QCOMPARE(copies, 2); QCOMPARE(portal.calls, 2); QVERIFY(QFileInfo::exists(expected->file->path));
  }
  void externalScrollingCaptureHoldsAdmissionAndSubmitsThroughThePipeline() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("pipelines"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, SnapshotComposer{}, CaptureActionPorts{}, journal);
    int cancels = 0; QSignalSpy busy(&controller, &CaptureController::busyChanged);
    QVERIFY(controller.acquireExternal([&] { ++cancels; }));
    QVERIFY(controller.busy()); QVERIFY(!controller.acquireExternal({}));
    controller.captureRegion(); QTest::qWait(20); QVERIFY(controller.selection() == std::nullopt);  // refused: admission is held
    controller.cancel(); QCOMPARE(cancels, 1); QVERIFY(controller.busy());
    controller.releaseExternal(); QVERIFY(!controller.busy()); QCOMPARE(busy.count(), 2);
    QVERIFY(!controller.submitExternalImage(QImage(8, 8, QImage::Format_ARGB32), "scrolling"));  // nothing held
    QVERIFY(controller.acquireExternal({}));
    QImage image(40, 90, QImage::Format_ARGB32); image.fill(Qt::darkCyan);
    QSignalSpy notified(&controller, &CaptureController::notify);
    QVERIFY(controller.submitExternalImage(image, "scrolling"));
    QTRY_VERIFY(!controller.busy());
    QVERIFY(controller.lastPipelineResult());
    QVERIFY(!notified.isEmpty()); QVERIFY(notified.last().at(0).toString().startsWith("Scrolling capture"));
    QVERIFY(controller.lastPipelineResult()->file); QCOMPARE(QImage(controller.lastPipelineResult()->file->path).size(), QSize(40, 90));
  }
  void init() {
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "screenshot-test-server");
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "screenshot-test-client");
    QVERIFY(server.isConnected() && client.isConnected()); QVERIFY(server.registerService(service));
  }
  void cleanup() {
    server.unregisterService(service);
    QDBusConnection::disconnectFromBus("screenshot-test-client");
    QDBusConnection::disconnectFromBus("screenshot-test-server");
  }
  void authenticResponseAndEarlyLegacyHandle_data() {
    QTest::addColumn<bool>("early"); QTest::addColumn<bool>("legacy");
    QTest::addColumn<bool>("localhost");
    QTest::newRow("after-handle") << false << false << false;
    QTest::newRow("before-first-handle") << true << false << false;
    QTest::newRow("early-legacy-handle") << true << true << false;
    QTest::newRow("localhost-file-uri") << true << false << true;
  }
  void authenticResponseAndEarlyLegacyHandle() {
    QFETCH(bool, early); QFETCH(bool, legacy); QFETCH(bool, localhost); QTemporaryDir dir; const auto path = fixtureImage(dir);
    const auto before = bytes(path); FakeScreenshotPortal portal(server); portal.early = early; portal.legacy = legacy;
    auto uri = QUrl::fromLocalFile(path); if (localhost) uri.setHost("localhost");
    portal.uri = uri.toString(); PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); source.request(); QTRY_COMPARE(portal.calls, 1);
    if (!early) portal.respond(); QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Captured); QCOMPARE(resultImage(done), QImage(path));
    QCOMPARE(resultImage(done).colorSpace(), QColorSpace(QColorSpace::SRgb)); QCOMPARE(done.at(0).at(2).toString(), QString{});
    QCOMPARE(portal.parentWindow, QString{}); QCOMPARE(portal.options.value("interactive"), QVariant(false));
    QCOMPARE(portal.options.value("modal"), QVariant(false)); QVERIFY(portal.options.value("handle_token").toString().startsWith("xerahs"));
    QCOMPARE(bytes(path), before); portal.respond(); source.cancel(); source.request(); QTest::qWait(20);
    QCOMPARE(done.count(), 1); QCOMPARE(portal.calls, 1); QVERIFY(portal.closed.isEmpty());
  }
  void userCancelIsDifferentFromPermissionDenial() {
    FakeScreenshotPortal portal(server); portal.code = 1; portal.early = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Cancelled);
    QVERIFY(resultImage(done).isNull()); QCOMPARE(done.at(0).at(2).toString(), QString{}); QCOMPARE(portal.calls, 1);
  }
  void methodErrorsAreStableAndValueFree_data() {
    QTest::addColumn<QString>("error"); QTest::addColumn<bool>("denied");
    QTest::newRow("permission") << QString("org.freedesktop.portal.Error.NotAllowed") << true;
    QTest::newRow("restricted") << QString("org.freedesktop.DBus.Error.AccessDenied") << true;
    QTest::newRow("source") << QString("org.freedesktop.DBus.Error.Failed") << false;
  }
  void methodErrorsAreStableAndValueFree() {
    QFETCH(QString, error); QFETCH(bool, denied); FakeScreenshotPortal portal(server); portal.error = error;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), denied ? Outcome::Denied : Outcome::Failed); QVERIFY(resultImage(done).isNull());
    QCOMPARE(done.at(0).at(2).toString(), denied ? QString("capture-permission-denied") : QString("capture-source-failed"));
    QCOMPARE(portal.calls, 1); source.request(); QCOMPARE(portal.calls, 1);
  }
  void invalidResultsFailWithoutMutatingFiles_data() {
    QTest::addColumn<QString>("kind");
    for (const auto *kind : {"missing", "wrong-type", "remote", "authority", "relative", "query", "fragment", "nul", "unknown-code", "corrupt", "oversized", "encoded-limit", "invalid-handle"})
      QTest::newRow(kind) << QString(kind);
  }
  void invalidResultsFailWithoutMutatingFiles() {
    QFETCH(QString, kind); QTemporaryDir dir; auto path = fixtureImage(dir);
    if (kind == u"corrupt") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("not an image") > 0); }
    if (kind == u"oversized") path = fixtureImage(dir, {int(xerahs::editor::kMaxDimension) + 1, 1});
    if (kind == u"encoded-limit") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(1073741825)); }
    const auto before = kind == u"encoded-limit" ? QByteArray{} : bytes(path);
    FakeScreenshotPortal portal(server); portal.uri = QUrl::fromLocalFile(path).toString(); portal.early = true;
    if (kind == u"missing") portal.uri = {};
    if (kind == u"wrong-type") portal.uri = 17;
    if (kind == u"remote") portal.uri = "https://example.invalid/private.png";
    if (kind == u"authority") portal.uri = "file://elsewhere" + path;
    if (kind == u"relative") portal.uri = "file:relative.png";
    if (kind == u"query") portal.uri = portal.uri.toString() + "?private=value";
    if (kind == u"fragment") portal.uri = portal.uri.toString() + "#private";
    if (kind == u"nul") portal.uri = "file:///tmp/fixture%00.png";
    if (kind == u"unknown-code") portal.code = 999;
    if (kind == u"invalid-handle") portal.invalidHandle = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed); QVERIFY(resultImage(done).isNull());
    QCOMPARE(done.at(0).at(2).toString(), QString("capture-source-failed"));
    if (kind == u"encoded-limit") QCOMPARE(QFileInfo(path).size(), qint64(1073741825)); else QCOMPARE(bytes(path), before);
  }
  void cancellingBeforeStartPreventsAllRequests() {
    FakeScreenshotPortal portal(server); PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.cancel(); source.request(); source.cancel(); QTest::qWait(20);
    QCOMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Cancelled); QCOMPARE(portal.calls, 0);
  }
  void cancelClosesThePendingAndLateLegacyHandleOnce() {
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request(); QTRY_COMPARE(portal.calls, 1);
    source.cancel(); source.cancel(); QCOMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Cancelled);
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle(); QTRY_COMPARE(portal.closed.count(portal.handle), 1);
    portal.respond(); QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void methodTimeoutFailsOnceAndLatePixelsCannotPublish() {
    QTemporaryDir dir; const auto path = fixtureImage(dir); FakeScreenshotPortal portal(server); portal.delayed = true;
    portal.uri = QUrl::fromLocalFile(path).toString(); PortalScreenshot source(client, {service, 500});
    QSignalSpy done(&source, &PortalScreenshot::finished); source.request(); QTRY_COMPARE(portal.calls, 1);
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle(); portal.respond(); QTest::qWait(20);
    QCOMPARE(done.count(), 1); QVERIFY(QFileInfo::exists(path));
  }
  void userInteractionDoesNotInheritTheMethodDeadline() {
    FakeScreenshotPortal portal(server); PortalScreenshot source(client, {service, 100}); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); QTRY_COMPARE(portal.calls, 1); QTest::qWait(150); QCOMPARE(done.count(), 0);
    source.cancel(); QTRY_COMPARE(portal.closed.count(portal.handle), 1); QCOMPARE(outcome(done), Outcome::Cancelled);
  }
  void ownerLossCannotBeReplacedByAnotherPortal() {
    FakeScreenshotPortal portal(server); PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); QTRY_COMPARE(portal.calls, 1); QVERIFY(server.unregisterService(service));
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
    QVERIFY(server.registerService(service)); portal.respond(); QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void ownerLossBeforeTheHandleStillClosesTheOldOwnersLegacyRequest() {
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); QTRY_COMPARE(portal.calls, 1); QVERIFY(server.unregisterService(service));
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle();
    QTRY_COMPARE(portal.closed.count(portal.handle), 1); portal.respond(); QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void wrongSenderAndWrongPathCannotGrantPixels() {
    QTemporaryDir dir; const auto path = fixtureImage(dir); FakeScreenshotPortal portal(server); portal.uri = QUrl::fromLocalFile(path).toString();
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request(); QTRY_COMPARE(portal.calls, 1);
    auto rogue = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "screenshot-test-rogue");
    auto fake = QDBusMessage::createSignal(portal.handle, requestInterface, "Response"); fake << uint(0) << QVariantMap{{"uri", portal.uri}};
    QVERIFY(rogue.send(fake)); portal.respond(root + "/request/another/handle"); QTest::qWait(30); QCOMPARE(done.count(), 0);
    portal.respond(); QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Captured);
    QDBusConnection::disconnectFromBus("screenshot-test-rogue");
  }
  void missingAndDisconnectedTransportFailWithoutPromptLoops() {
    QVERIFY(server.unregisterService(service));
    PortalScreenshot absent(client); QSignalSpy missing(&absent, &PortalScreenshot::finished); absent.request();
    QTRY_COMPARE(missing.count(), 1); QCOMPARE(outcome(missing), Outcome::Failed);
    PortalScreenshot disconnected(QDBusConnection("disconnected-fixture")); QSignalSpy done(&disconnected, &PortalScreenshot::finished);
    disconnected.request(); QCOMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
  }
  void destructionClosesAnOutstandingInteraction() {
    FakeScreenshotPortal portal(server); auto *source = new PortalScreenshot(client);
    source->request(); QTRY_COMPARE(portal.calls, 1); QTest::qWait(20); delete source; QTRY_COMPARE(portal.closed.count(portal.handle), 1);
  }
  void actualControllerCancellationReleasesBusyAndClosesLateLegacyRequest() {
    QTemporaryDir dir; const auto path = fixtureImage(dir); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true; portal.uri = QUrl::fromLocalFile(path).toString();
    QApplication::clipboard()->setText("fixture clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy busy(&controller, &CaptureController::busyChanged), notified(&controller, &CaptureController::notify);
    controller.captureRegion(); controller.captureRegion(); QTRY_COMPARE(portal.calls, 1); QVERIFY(controller.busy());
    controller.cancel(); QVERIFY(!controller.busy()); QCOMPARE(busy.count(), 2); QCOMPARE(notified.count(), 0);
    portal.finishHandle(); QTRY_COMPARE(portal.closed.count(portal.handle), 1); portal.respond(); QTest::qWait(30);
    QCOMPARE(busy.count(), 2); QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
    QCOMPARE(QApplication::clipboard()->text(), QString("fixture clipboard")); QCOMPARE(bytes(path), before);
    // The cancelled source cannot finish a new pending controller request.
    portal.delayed = false; controller.captureRegion(); QTRY_COMPARE(portal.calls, 2);
    portal.respond(root + "/request/legacy/result_1"); QTest::qWait(20); QVERIFY(controller.busy());
    controller.cancel(); QVERIFY(!controller.busy()); QCOMPARE(busy.count(), 4); QCOMPARE(notified.count(), 0);
  }
  void actualControllerDenialShowsNoOverlayAndNoPostCaptureActions() {
    FakeScreenshotPortal portal(server); portal.error = "org.freedesktop.portal.Error.NotAllowed"; QTemporaryDir dir;
    QApplication::clipboard()->setText("fixture denial clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(!controller.busy());
    QCOMPARE(notified.count(), 1); QCOMPARE(notified.at(0).at(1).toString(), QString("capture-permission-denied"));
    QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
    QCOMPARE(QApplication::clipboard()->text(), QString("fixture denial clipboard")); QCOMPARE(portal.calls, 1);
  }
  void cancellingAtTheFirstAvailabilitySignalPreventsPortalAcquisition() {
    FakeScreenshotPortal portal(server); QTemporaryDir dir;
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    connect(&controller, &CaptureController::busyChanged, &controller, [&](bool busy) { if (busy) controller.cancel(); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion();
    QVERIFY(!controller.busy()); QTest::qWait(20); QCOMPARE(portal.calls, 0); QCOMPARE(notified.count(), 0);
    QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QVERIFY(!overlayExists());
  }
  void controllerDestructionDoesNotAbandonALateLegacyHandle() {
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true; QTemporaryDir dir;
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr,
        [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller->captureRegion(); QTRY_COMPARE(portal.calls, 1); controller.reset();
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle();
    QTRY_COMPARE(portal.closed.count(portal.handle), 1); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
  void recordingSelectionReusesOverlayWithoutCaptureActions() {
    QTemporaryDir dir; FakeScreenshotPortal portal(server);
    portal.uri = QUrl::fromLocalFile(fixtureImage(dir, QApplication::primaryScreen()->geometry().size() * 2)).toString(); portal.early = true;
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    int selected = 0; QRectF rectangle;
    controller.selectRecordingRegion([&](QRectF r) { ++selected; rectangle = r; QVERIFY(!controller.busy()); QVERIFY(!overlayExists()); });
    QTRY_VERIFY(overlayExists()); controller.pointerDown({20, 40}); controller.pointerUp({222, 142});
    QTRY_VERIFY(!controller.busy()); QCOMPARE(selected, 1); QCOMPARE(rectangle.size(), QSizeF(101, 51));
    QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QVERIFY(!controller.lastPipelineResult());
    controller.selectRecordingRegion([&](QRectF) { ++selected; }); QTRY_VERIFY(overlayExists());
    QTest::keyClick(overlayWidget(), Qt::Key_Escape); QTRY_VERIFY(!controller.busy()); QCOMPARE(selected, 1);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.cancel(); QTRY_VERIFY(!controller.busy()); QCOMPARE(selected, 1);
  }
  void actualFrozenOverlayEscapeLeavesFilesAndClipboardUntouched() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.uri = QUrl::fromLocalFile(path).toString(); portal.early = true;
    QApplication::clipboard()->setText("fixture overlay clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    QWidget *overlay = nullptr; for (auto *widget : QApplication::topLevelWidgets()) if (widget->windowTitle() == u"XerahS region capture") overlay = widget;
    QVERIFY(overlay); QVERIFY(overlay->accessibleDescription().contains("Escape cancels"));
    QTest::keyClick(overlay, Qt::Key_Escape); QTRY_VERIFY(!controller.busy()); QVERIFY(!overlayExists());
    QCOMPARE(notified.count(), 0); QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QCOMPARE(bytes(path), before);
    QCOMPARE(QApplication::clipboard()->text(), QString("fixture overlay clipboard"));
  }
  void cancellationDuringActualCompositionNeverPublishesPixelsOrRunsActions_data() {
    QTest::addColumn<int>("cancelAt");
    QTest::newRow("before-worker") << 0; QTest::newRow("partial-rows") << 50; QTest::newRow("last-row") << 100;
  }
  void cancellationDuringActualCompositionNeverPublishesPixelsOrRunsActions() {
    QFETCH(int, cancelAt); QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    const auto before = bytes(path); FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); gate->threshold = cancelAt; ReleaseGate release{gate};
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, gatedComposition(gate));
    QSignalSpy progress(&controller, &CaptureController::compositionProgressChanged), notified(&controller, &CaptureController::notify);
    if (cancelAt == 0) connect(&controller, &CaptureController::compositionProgressChanged, &controller, [&](int percent) { if (percent == 0) controller.cancel(); });
    QApplication::clipboard()->setText("composition fixture clipboard"); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    auto *overlay = overlayWidget(); QVERIFY(overlay);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(40, 35));
    if (cancelAt != 0) {
      QTRY_VERIFY(gate->entered.load()); QTRY_VERIFY(controller.compositionProgress().value_or(-1) >= cancelAt);
      bool heartbeat = false; QTimer::singleShot(0, &controller, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
      QVERIFY(!gate->done.load()); QTest::keyClick(overlay, Qt::Key_Escape);
    }
    QTRY_VERIFY(!controller.busy()); QVERIFY(!overlayExists()); QCOMPARE(notified.count(), 0); QVERIFY(!progress.isEmpty());
    QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QCOMPARE(QApplication::clipboard()->text(), QString("composition fixture clipboard"));
    if (cancelAt == 0) QCOMPARE(gate->calls.load(), 0);
    else { QVERIFY(!gate->done.load()); gate->released.store(true); QTRY_VERIFY(gate->done.load()); }
    QTest::qWait(30); QCOMPARE(notified.count(), 0); QCOMPARE(bytes(path), before);
  }
  void aLateCancelledWorkerCannotFinishANewerPendingCapture() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, gatedComposition(gate));
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load());
    controller.cancel(); QTRY_VERIFY(!controller.busy());
    portal.early = false; controller.captureRegion(); QTRY_COMPARE(portal.calls, 2); QVERIFY(controller.busy());
    gate->released.store(true); QTRY_VERIFY(gate->done.load()); QTest::qWait(30);
    QVERIFY(controller.busy()); QVERIFY(!overlayExists()); QCOMPARE(notified.count(), 0); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
    controller.cancel(); QTRY_VERIFY(!controller.busy());
  }
  void destructionReleasesTheUiWithoutWaitingForACompositionWorker() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr,
        [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, gatedComposition(gate));
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35});
    QTRY_VERIFY(gate->entered.load()); QElapsedTimer elapsed; elapsed.start(); controller.reset();
    QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->done.load()); QVERIFY(!overlayExists());
    gate->released.store(true); QTRY_VERIFY(gate->done.load()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
  void progressReceiverCanDestroyTheControllerBeforeWorkerStart() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    connect(controller.get(), &CaptureController::compositionProgressChanged, [&](int) { controller.reset(); });
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35});
    QVERIFY(!controller); QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
  void configuredWorkflowSnapshotsActionsBeforeAcquisition_data() {
    QTest::addColumn<bool>("useDefault"); QTest::newRow("configured-default") << true; QTest::newRow("explicit-selection") << false;
  }
  void configuredWorkflowSnapshotsActionsBeforeAcquisition() {
    QFETCH(bool, useDefault); QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries[0]; workflow.name = "Save only"; workflow.actions = QStringList{"save"};
    workflow.output.directory = dir.filePath("original/nested"); workflow.output.pattern = "original-%widthx%height";
    int copies = 0; CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) { ++copies; return xerahs::actions::ActionOutcome{}; };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    controller.setDefaultWorkflowResolver([&] { return xerahs::workflows::WorkflowChoice{workflow, {}}; });
    if (useDefault) controller.captureRegion(); else controller.captureWorkflow(workflow);
    workflow.name = "Copy only"; workflow.actions = QStringList{"image-clipboard"};
    workflow.output.directory = dir.filePath("changed"); workflow.output.pattern = "changed-%widthx%height";
    QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto saved = controller.lastPipelineResult(); QVERIFY(saved && saved->file); QCOMPARE(copies, 0); QCOMPARE(saved->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(saved->pipeline.orderedResults[0].action, QString("save"));
    QCOMPARE(saved->pipeline.workflowSnapshot.value("name"), QJsonValue("Save only")); QCOMPARE(QImage(saved->file->path).size(), QSize(30, 25));
    QCOMPARE(saved->file->path, dir.filePath("original/nested/original-30x25.png"));
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    const auto copied = controller.lastPipelineResult(); QVERIFY(copied && copied != saved); QVERIFY(copied->files.empty()); QCOMPARE(copies, 1); QCOMPARE(copied->pipeline.workflowSnapshot.value("name"), QJsonValue("Copy only"));
    QCOMPARE(copied->saveDirectory, dir.filePath("changed")); QCOMPARE(copied->filenamePattern, QString("changed-%widthx%height"));
    QCOMPARE(QDir(dir.filePath("original/nested")).entryList({"*.png"}, QDir::Files).size(), 1); QVERIFY(!QFile::exists(dir.filePath("changed")));
    workflow.actions = QStringList{"save"}; controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({1, 1}); controller.pointerUp({8, 7}); QTRY_VERIFY(!controller.busy());
    const auto newer = controller.lastPipelineResult(); QVERIFY(newer && newer->file); QCOMPARE(newer->file->path, dir.filePath("changed/changed-7x6.png")); QCOMPARE(portal.calls, 3);
  }
  void wholeSourceWorkflowSavesWithoutAnOverlay_data() {
    QTest::addColumn<QString>("source"); QTest::newRow("full-screen") << QString("full-screen"); QTest::newRow("monitor") << QString("monitor");
  }
  void wholeSourceWorkflowSavesWithoutAnOverlay() {
    QFETCH(QString, source); QTemporaryDir dir; const auto size = QApplication::primaryScreen()->geometry().size(); const auto path = fixtureImage(dir, size);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries[0]; workflow.capture = source; workflow.actions = QStringList{"save"};
    workflow.output.directory = dir.filePath("whole"); workflow.output.pattern = "whole-%widthx%height";
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notify(&controller, &CaptureController::notify); controller.captureWorkflow(workflow);
    QTRY_VERIFY(!controller.busy()); QVERIFY(!overlayExists()); QCOMPARE(portal.calls, 1); QCOMPARE(controller.captureSource(), source);
    const auto saved = controller.lastPipelineResult(); QVERIFY(saved && saved->file);
    QCOMPARE(QImage(saved->file->path).size(), size); QCOMPARE(QImage(saved->file->path).pixel(3, 2), QImage(path).pixel(3, 2));
    QCOMPARE(notify.count(), 1); QVERIFY(notify[0][2].toBool());
    QVERIFY(notify[0][0].toString().startsWith(xerahs::workflows::captureSourceLabel(source) + " captured"));
  }
  void activeWindowCropsToTheWindowOrFails_data() {
    QTest::addColumn<QString>("mode");
    QTest::newRow("window") << QString("window");
    QTest::newRow("none") << QString("none");
    QTest::newRow("outside") << QString("outside");
  }
  void activeWindowCropsToTheWindowOrFails() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto size = QApplication::primaryScreen()->geometry().size();
    QVERIFY(size.width() >= 40 && size.height() >= 60);
    QImage image(size, QImage::Format_ARGB32); image.fill(QColor(42, 71, 100)); image.setColorSpace(QColorSpace::SRgb);
    image.setPixel(12, 22, qRgb(11, 22, 33)); image.setPixel(39, 59, qRgb(44, 55, 66));
    const auto path = dir.filePath("snapshot.png"); QVERIFY(image.save(path));
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries[0]; workflow.capture = "active-window"; workflow.actions = QStringList{"save"};
    workflow.output.directory = dir.filePath("whole"); workflow.output.pattern = "window-%widthx%height";
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    auto windows = std::make_unique<FixedActiveWindow>();
    if (mode == "window") windows->window = xerahs::capture::SnapTarget{{10, 20, 40, 60}};
    else if (mode == "outside") windows->window = xerahs::capture::SnapTarget{{qint64(size.width()) + 10, 0, qint64(size.width()) + 20, 10}};
    controller.setWindowDiscovery(std::move(windows));
    QSignalSpy notify(&controller, &CaptureController::notify); controller.captureWorkflow(workflow);
    QTRY_VERIFY(!controller.busy()); QVERIFY(!overlayExists()); QCOMPARE(portal.calls, 1); QCOMPARE(controller.captureSource(), QString("active-window"));
    if (mode == "window") {
      const auto saved = controller.lastPipelineResult(); QVERIFY(saved && saved->file);
      const QImage cropped(saved->file->path); QCOMPARE(cropped.size(), QSize(30, 40));
      QCOMPARE(cropped.pixel(2, 2), qRgb(11, 22, 33)); QCOMPARE(cropped.pixel(29, 39), qRgb(44, 55, 66));
      QCOMPARE(notify.count(), 1); QVERIFY(notify[0][2].toBool());
      QVERIFY(notify[0][0].toString().startsWith("Active window captured"));
      return;
    }
    QVERIFY(!controller.lastPipelineResult()); QVERIFY(QDir(dir.filePath("whole")).entryList(QDir::Files).isEmpty());
    QCOMPARE(notify.count(), 1); QVERIFY(!notify[0][2].toBool());
    QCOMPARE(notify[0][1].toString(), QString("No active window to capture"));
  }
  void unsupportedConfiguredWorkflowIsRejectedBeforePortalAcquisition() {
    QTemporaryDir dir; int acquisitions = 0; CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *) -> PortalScreenshot * { ++acquisitions; return nullptr; });
    auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries[0]; workflow.actions = QStringList{"ocr"};
    QSignalSpy notify(&controller, &CaptureController::notify); controller.captureWorkflow(workflow);
    QCOMPARE(acquisitions, 0); QVERIFY(!controller.busy()); QCOMPARE(notify.count(), 1); QVERIFY(!notify[0][2].toBool()); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
  }
  void invalidConfiguredOutputIsRejectedBeforePortalAcquisition_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"relative", "counter", "malformed", "file"}) QTest::newRow(mode) << QString(mode);
  }
  void admittedWorkflowUsesSavedSnapshotAndRealControllerOutput() {
    using namespace xerahs::shell; using namespace xerahs::workflows;
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    xerahs::editor::FilePreferenceStorage storage(dir.filePath("workflows.json")); CaptureWorkflowStore store(storage); auto catalog = *store.current().catalog;
    auto &profile = catalog.entries[0]; profile.actions = QStringList{"save"}; profile.name = "Admitted profile"; profile.output.directory = dir.filePath("first"); profile.output.pattern = "first-%width";
    QVERIFY(store.save(catalog, store.revision()).saved); const auto workflowId = profile.id;
    CaptureController controller(dir.filePath("fallback"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    ActivationOperations operations; operations.captureBusy = [&] { return controller.busy(); };
    operations.prepareWorkflow = [&](const QString &id) -> std::function<void()> {
      const auto choice = store.resolve(QUuid(id), CaptureActions::selectableActions()); if (!choice.workflow) return {};
      return [&, snapshot = *choice.workflow] { controller.captureWorkflow(snapshot); };
    };
    ActivationDispatcher dispatcher(operations); const auto original = makeActivationRequest(ActivationAction::RunWorkflow, {}, workflowId.toString(QUuid::WithoutBraces));
    const auto request = parseActivation(serializeActivation(original)); QVERIFY(request.request);
    QCOMPARE(dispatcher.admit(*request.request), ActivationReplyState::Accepted);
    profile.name = "Changed later"; profile.output.directory = dir.filePath("second"); profile.output.pattern = "second-%width"; QVERIFY(store.save(catalog, store.revision()).saved);
    QVERIFY(dispatcher.dispatch(*request.request).isEmpty()); QCOMPARE(dispatcher.admit(original), ActivationReplyState::Busy);
    QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    auto saved = controller.lastPipelineResult(); QVERIFY(saved && saved->file); QCOMPARE(saved->file->path, dir.filePath("first/first-30.png")); QCOMPARE(QImage(saved->file->path).size(), QSize(30, 25));
    QCOMPARE(saved->pipeline.workflowSnapshot.value("name"), QJsonValue("Admitted profile")); QVERIFY(!QFile::exists(dir.filePath("second")));
    const auto next = makeActivationRequest(ActivationAction::RunWorkflow, {}, original.workflowId); QCOMPARE(dispatcher.admit(next), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(next).isEmpty());
    QTRY_VERIFY(overlayExists()); controller.pointerDown({1, 1}); controller.pointerUp({8, 7}); QTRY_VERIFY(!controller.busy());
    saved = controller.lastPipelineResult(); QVERIFY(saved && saved->file); QCOMPARE(saved->file->path, dir.filePath("second/second-7.png")); QCOMPARE(portal.calls, 2);
    profile.actions = QStringList{"ocr"}; QVERIFY(store.save(catalog, store.revision()).saved); QCOMPARE(dispatcher.admit(next), ActivationReplyState::Rejected); QCOMPARE(portal.calls, 2);
  }
  void invalidConfiguredOutputIsRejectedBeforePortalAcquisition() {
    QFETCH(QString, mode); QTemporaryDir dir; int acquisitions = 0; CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *) -> PortalScreenshot * { ++acquisitions; return nullptr; });
    auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries[0]; workflow.output.directory = mode == "relative" ? "relative" : dir.filePath("chosen");
    workflow.output.pattern = mode == "counter" ? "%i{257}" : mode == "malformed" ? "%ra{0}" : mode == "file" ? "%rf{/private/input}" : "valid";
    QSignalSpy notify(&controller, &CaptureController::notify); controller.captureWorkflow(workflow);
    QCOMPARE(acquisitions, 0); QVERIFY(!controller.busy()); QCOMPARE(notify.count(), 1); QVERIFY(!QFile::exists(dir.filePath("chosen")));
  }
  void successfulCompositionRunsOnePipelineAndSavesOnePhysicalImage() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); controller.pointerUp({70, 50});
    QTRY_VERIFY(!controller.busy()); QCOMPARE(notified.count(), 1);
    const auto files = QDir(controller.saveDirectory()).entryList({"*.png"}, QDir::Files); QCOMPARE(files.size(), 1);
    const QImage output(QDir(controller.saveDirectory()).filePath(files.first())); QCOMPARE(output.size(), QSize(30, 25));
    QTest::qWait(30); QCOMPARE(notified.count(), 1); QVERIFY(!overlayExists());
  }
  void actualOverlayEscapeDuringSavingKeepsTheCompletedFileAndCancelsClipboard() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    CaptureActionPorts ports;
    ports.save.beforePublish = [gate](const QString &) {
      gate->entered.store(true); QElapsedTimer deadline; deadline.start();
      while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      if (!gate->released.load()) throw 1;
    };
    QApplication::clipboard()->setText("saving fixture clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load());
    QWidget *overlay = nullptr;
    for (auto *widget : QApplication::topLevelWidgets()) if (widget->windowTitle() == u"XerahS region capture") overlay = widget;
    QVERIFY(overlay); QTest::keyClick(overlay, Qt::Key_Escape);
    QVERIFY(controller.busy()); QVERIFY(controller.pipelineActivity().contains("Cancellation requested"));
    bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    gate->released.store(true); QTRY_VERIFY(!controller.busy()); QCOMPARE(notified.count(), 1); QVERIFY(!overlayExists());
    const auto result = controller.lastPipelineResult(); QVERIFY(result); QVERIFY(result->file);
    QCOMPARE(result->pipeline.state, xerahs::actions::PipelineState::Cancelled);
    QCOMPARE(result->pipeline.orderedResults[0].state, xerahs::actions::ActionState::Succeeded); QVERIFY(result->pipeline.orderedResults[0].cancellationArrivedDuring);
    QCOMPARE(result->pipeline.orderedResults[1].state, xerahs::actions::ActionState::Cancelled);
    QCOMPARE(QImage(result->file->path).size(), QSize(30, 25)); QCOMPARE(result->workingMedia.size(), QSize(30, 25));
    QCOMPARE(QApplication::clipboard()->text(), QString("saving fixture clipboard")); QCOMPARE(bytes(path), before);
    QVERIFY(notified.first().first().toString().contains("cancelled"));
  }
  void sharedReviewCommandShowsAnImmutableReadOnlyReportAndHasNoOutputSideEffects() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureActionPorts ports;
    ports.clipboard = [](const QImage &, const xerahs::actions::CancellationToken &) {
      return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, QString("clipboard-unavailable"), {}};
    };
    CaptureController controller(dir.filePath("private-<img>"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    auto recovery = std::make_unique<CaptureRecoveryActions>(controller, nullptr, CapturedImageOpener{});
    ApplicationWindow home({}); home.addCaptureRecoveryActions(recovery->editAction(), recovery->discardAction(), recovery->reviewAction()); home.activate();
    auto *review = home.findChild<QToolButton *>("review-retained-capture"); QVERIFY(review && !review->isEnabled());
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); QVERIFY(retained && retained->file);
    const auto saved = bytes(retained->file->path); QApplication::clipboard()->setText("review fixture private clipboard");
    QVERIFY(review->isEnabled()); QVERIFY(!recovery->editAction()->isEnabled());
    review->setFocus(); QTest::keyClick(review, Qt::Key_Space); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *dialog = QApplication::activeModalWidget(); QCOMPARE(dialog->objectName(), QString("retainedCaptureResultDialog"));
    auto *report = dialog->findChild<QPlainTextEdit *>("captureResultJson"); QVERIFY(report && report->isReadOnly()); QVERIFY(!report->isVisible());
    QCOMPARE(QJsonDocument::fromJson(report->toPlainText().toUtf8()).object(), retained->canonicalResult());
    auto *outcomes = dialog->findChild<QTreeWidget *>("captureActionResults"); QVERIFY(outcomes); QCOMPARE(outcomes->topLevelItemCount(), 2);
    QCOMPARE(outcomes->topLevelItem(0)->text(1), QString("succeeded")); QCOMPARE(outcomes->topLevelItem(1)->text(2), QString("clipboard-unavailable"));
    QPushButton *toggle = nullptr; for (auto *button : dialog->findChildren<QPushButton *>()) if (button->text() == "Show report details") toggle = button;
    QVERIFY(toggle); toggle->click(); QVERIFY(report->isVisible()); const auto original = report->toPlainText();
    report->setFocus(); QTest::keyClicks(report, "cannot edit"); QCOMPARE(report->toPlainText(), original);
    QCOMPARE(controller.lastPipelineResult(), retained); QCOMPARE(QApplication::clipboard()->text(), QString("review fixture private clipboard"));
    QTest::keyClick(dialog, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget()); QVERIFY(review->isEnabled());
    recovery->reviewAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QWidget> owned = QApplication::activeModalWidget(); recovery.reset(); QTRY_VERIFY(!owned);
    QCOMPARE(controller.lastPipelineResult(), retained); QCOMPARE(bytes(retained->file->path), saved); QCOMPARE(bytes(path), before);
    QCOMPARE(QApplication::clipboard()->text(), QString("review fixture private clipboard"));
  }
  void recoveryActionsUseActualSharedKeyboardCommandsAndKeepFailedMediaUntilExplicitDiscard() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureActionPorts ports; ports.save.encode = [](const QImage &, QIODevice *) { return false; };
    ports.clipboard = [](const QImage &, const xerahs::actions::CancellationToken &) {
      return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, QString("clipboard-unavailable"), {}};
    };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    QImage opened; int calls = 0;
    CaptureRecoveryActions recovery(controller, nullptr, [&](const QImage &image, QWidget *) { ++calls; opened = image.copy(); return true; });
    ApplicationWindow home({}); home.addCaptureRecoveryActions(recovery.editAction(), recovery.discardAction()); home.activate();
    auto *edit = home.findChild<QToolButton *>("edit-retained-capture"); auto *discard = home.findChild<QToolButton *>("discard-retained-capture");
    QVERIFY(edit && discard && !edit->isEnabled() && !discard->isEnabled());
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); QVERIFY(retained && !retained->file); QCOMPARE(retained->workingMedia.size(), QSize(30, 25));
    QVERIFY(edit->isEnabled() && discard->isEnabled()); edit->setFocus(); QTest::keyClick(edit, Qt::Key_Space);
    QCOMPARE(calls, 1); QCOMPARE(opened, retained->workingMedia); QCOMPARE(controller.lastPipelineResult(), retained);
    recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); QCOMPARE(box->objectName(), QString("discardRetainedCaptureDialog"));
    QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); QVERIFY(!edit->isEnabled() && !discard->isEnabled());
    box->button(QMessageBox::Cancel)->click(); QTRY_VERIFY(!QApplication::activeModalWidget()); QCOMPARE(controller.lastPipelineResult(), retained);
    QVERIFY(edit->isEnabled()); recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Discard)->click();
    QTRY_VERIFY(!controller.lastPipelineResult()); QVERIFY(!edit->isEnabled() && !discard->isEnabled());
    QCOMPARE(opened, retained->workingMedia); QCOMPARE(bytes(path), before);
  }
  void discardApprovalCannotClearANewerResultAndNeverRemovesSavedFiles() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    CaptureRecoveryActions recovery(controller, nullptr, {}); QSignalSpy notices(&recovery, &CaptureRecoveryActions::notify);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto first = controller.lastPipelineResult(); QVERIFY(first && first->file); const auto firstFile = bytes(first->file->path);
    QVERIFY(!recovery.editAction()->isEnabled()); QVERIFY(recovery.discardAction()->isEnabled());
    recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QMessageBox> approval = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(approval);
    // A new production capture completes while the old target's confirmation
    // remains open. Its owned receipt cannot be consumed by that approval.
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    const auto second = controller.lastPipelineResult(); QVERIFY(second && second != first && second->file);
    approval->button(QMessageBox::Discard)->click(); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), second); QCOMPARE(notices.count(), 1); QVERIFY(!notices.first().last().toBool());
    QCOMPARE(bytes(first->file->path), firstFile);
    recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Discard)->click();
    QTRY_VERIFY(!controller.lastPipelineResult()); QVERIFY(QFileInfo::exists(second->file->path)); QCOMPARE(bytes(first->file->path), firstFile);
  }
  void recoveryCallbackCancellationExceptionsAndReentrancyDoNotConsumeTheReceipt() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); int calls = 0; CaptureRecoveryActions *running = nullptr;
    CaptureRecoveryActions recovery(controller, nullptr, [&](const QImage &, QWidget *) {
      ++calls; running->editAction()->trigger(); running->discardAction()->trigger(); if (calls == 2) throw std::runtime_error("private pixels"); return false;
    }); running = &recovery; QSignalSpy notices(&recovery, &CaptureRecoveryActions::notify);
    recovery.editAction()->trigger(); QCOMPARE(calls, 1); QCOMPARE(notices.count(), 0); QCOMPARE(controller.lastPipelineResult(), retained);
    recovery.editAction()->trigger(); QCOMPARE(calls, 2); QCOMPARE(notices.count(), 1); QVERIFY(notices.first().first().toString().contains("action-failed"));
    QVERIFY(!notices.first().first().toString().contains("private pixels")); QCOMPARE(controller.lastPipelineResult(), retained); QVERIFY(!QApplication::activeModalWidget());
  }
  void recoveryOwnerDestructionCancelsPendingApprovalAndKeepsTheReceipt() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); QVERIFY(retained && retained->file);
    auto recovery = std::make_unique<CaptureRecoveryActions>(controller, nullptr, CapturedImageOpener{});
    recovery->discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QWidget> approval = QApplication::activeModalWidget(); recovery.reset(); QTRY_VERIFY(!approval);
    QCOMPARE(controller.lastPipelineResult(), retained); QVERIFY(QFileInfo::exists(retained->file->path));
    recovery = std::make_unique<CaptureRecoveryActions>(controller, nullptr, [&](const QImage &, QWidget *) { recovery.reset(); return false; });
    recovery->editAction()->trigger(); QVERIFY(!recovery); QCOMPARE(controller.lastPipelineResult(), retained);
  }
};
QTEST_MAIN(PortalScreenshotTest)
#include "PortalScreenshotTest.moc"
