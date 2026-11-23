// Opt-in compositor cursor capture through the real controller, crop, and save
// path. The frame reader is injected. Nothing here opens a portal or a
// compositor protocol socket.
#include "CaptureController.h"
#include "PortalScreenshot.h"
#include "RegionOverlay.h"
#include "WindowDiscovery.h"
#include "capture/ScreenTopology.h"
#include "capture/CompositorErrors.h"
#include "settings/ApplicationSettings.h"
#include "workflows/CaptureWorkflows.h"

#include <QApplication>
#include <QColorSpace>
#include <QDir>
#include <QFileDevice>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <mutex>

using namespace xerahs::app;
using namespace xerahs::capture;
using namespace xerahs::settings;

namespace {
constexpr QRgb kStamp = qRgb(7, 11, 251);
constexpr QRgb kGround = qRgb(20, 30, 40);
constexpr QRect kStampRect(40, 28, 6, 5);
const LogicalRect kLogical{80, 40, 240, 130};
constexpr double kScale = 2;

QList<RegionOverlay *> overlays() {
  QList<RegionOverlay *> result;
  for (auto *widget : QApplication::topLevelWidgets())
    if (auto *overlay = qobject_cast<RegionOverlay *>(widget); overlay && overlay->isVisible()) result.append(overlay);
  return result;
}
int stampCount(const QImage &image) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (image.pixel(x, y) == kStamp) ++count;
  return count;
}
QImage scene(QSize size, bool stamp) {
  QImage image(size, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace::SRgb);
  image.fill(kGround);
  if (!stamp) return image;
  for (int y = kStampRect.top(); y < kStampRect.top() + kStampRect.height(); ++y)
    for (int x = kStampRect.left(); x < kStampRect.left() + kStampRect.width(); ++x) image.setPixel(x, y, kStamp);
  return image;
}
CaptureDisplays oneOutput(const LogicalRect &logical, double scale, const QString &outputName, const QString &serial = QStringLiteral("cursor")) {
  const auto mapped = snapshotScreenTopology({{QStringLiteral("Panel"), QStringLiteral("Maker"), QStringLiteral("Model"), serial, logical, scale, 0, {}, true}});
  if (mapped.size() != 1) return {};
  CaptureDisplays displays(1);
  displays[0].mapping = mapped[0];
  displays[0].outputName = outputName;
  return displays;
}
// A mixed-scale grid the packed topology arranges side by side: OUT-1 is
// logical 100x80 at scale 1 (physical 100x80 at the packed origin), OUT-2 is
// logical 150x80 at scale 2 (physical 300x160 beside it).
CaptureDisplays twoOutputs() {
  const auto mapped = snapshotScreenTopology({
      {QStringLiteral("Panel"), QStringLiteral("Maker"), QStringLiteral("Model"), QStringLiteral("1"), {0, 0, 100, 80}, 1, 0, {}, true},
      {QStringLiteral("Panel"), QStringLiteral("Maker"), QStringLiteral("Model"), QStringLiteral("2"), {100, 0, 250, 80}, 2, 0, {}, true}});
  if (mapped.size() != 2) return {};
  CaptureDisplays displays(2);
  displays[0].mapping = mapped[0];
  displays[0].outputName = QStringLiteral("OUT-1");
  displays[1].mapping = mapped[1];
  displays[1].outputName = QStringLiteral("OUT-2");
  return displays;
}
CapturedDisplayFrame acceptedFrame(const CaptureDisplay &display, const QImage &image) {
  CapturedDisplayFrame frame;
  frame.outputName = display.outputName;
  frame.image = image;
  frame.logicalX = qint32(display.mapping.logical.left);
  frame.logicalY = qint32(display.mapping.logical.top);
  frame.bufferWidth = qint32(display.mapping.physical.width());
  frame.bufferHeight = qint32(display.mapping.physical.height());
  frame.scale = qint32(display.mapping.scale);
  frame.transform = 0;
  frame.cursorPaintRequested = true;
  frame.cursorPaintedIntoFrame = true;
  frame.singleOutputBufferOriginProven = true;
  return frame;
}
CompositorFrame accepted(const CaptureDisplay &display, const QImage &image) {
  CompositorFrame frame;
  frame.status = CompositorFrame::Status::Captured;
  frame.image = image;
  frame.outputName = display.outputName;
  frame.logicalX = qint32(display.mapping.logical.left);
  frame.logicalY = qint32(display.mapping.logical.top);
  frame.bufferWidth = qint32(display.mapping.physical.width());
  frame.bufferHeight = qint32(display.mapping.physical.height());
  frame.scale = qint32(display.mapping.scale);
  frame.transform = 0;
  frame.cursorPaintRequested = true;
  frame.cursorPaintedIntoFrame = true;
  frame.singleOutputBufferOriginProven = true;
  return frame;
}
// CORE-014: a frozen display built from the real primary screen, so the
// monitor source's screenAt/primaryScreen identity resolution runs the
// production matching path against the real screen's stable identity. The
// compositor name on Wayland equals the QScreen name; offscreen Qt reports
// no name, so the fixture supplies the compositor address the injected
// frame reader answers to (an unnamed display is never capturable by
// compositor identity, and the honest refusal is covered by the
// nameless-output refusal row).
CaptureDisplays realScreenDisplays() {
  auto *screen = QGuiApplication::primaryScreen();
  if (!screen) return {};
  const auto geometry = screen->geometry();
  const LogicalRect logical{double(geometry.x()), double(geometry.y()),
                            double(geometry.x() + geometry.width()), double(geometry.y() + geometry.height())};
  const auto mapped = snapshotScreenTopology({{screen->name(), screen->manufacturer(), screen->model(),
                                               screen->serialNumber(), logical, screen->devicePixelRatio(), 0, {}, true}});
  if (mapped.size() != 1) return {};
  CaptureDisplays displays(1);
  displays[0].mapping = mapped[0];
  displays[0].outputName = QStringLiteral("Offscreen-Primary");
  return displays;
}
// CORE-014: a window discovery stub with a known active window, so the
// active-window source's target rect is deterministic. Without one the
// source reports the honest target-lost refusal.
class FixedWindows final : public WindowDiscovery {
public:
  std::vector<SnapTarget> targets(const Topology &, qint64) override { ++targetCalls; return {}; }
  std::optional<SnapTarget> activeWindow(const Topology &, qint64) override {
    ++windowCalls;
    if (!hasWindow) return std::nullopt;
    return SnapTarget{rect};
  }
  int targetCalls = 0, windowCalls = 0;
  bool hasWindow = true;
  PhysicalRect rect{16, 12, 80, 60};
};
class EmptyWindows final : public WindowDiscovery {
public:
  int calls = 0;
  std::vector<SnapTarget> targets(const Topology &, qint64) override { ++calls; return {}; }
  std::optional<SnapTarget> activeWindow(const Topology &, qint64) override { return std::nullopt; }
};
struct FrameReader {
  std::mutex mutex;
  CompositorFrame first, second;
  std::atomic<int> calls{0}, releaseThrough{1000000}, finished{0};
  std::atomic<bool> includeCursor{false}, hasCancelled{false}, cancelledAtEntry{false}, sawCancel{false};
  QStringList requestedNames;   // guarded by the mutex
  void setFirst(CompositorFrame frame) { std::lock_guard lock(mutex); first = std::move(frame); }
  void setSecond(CompositorFrame frame) { std::lock_guard lock(mutex); second = std::move(frame); }
  CompositorFrame operator()(const CompositorFrameRequest &request) {
    const int id = calls.fetch_add(1) + 1;
    includeCursor.store(request.includeCursor);
    hasCancelled.store(static_cast<bool>(request.cancelled));
    if (id == 1) cancelledAtEntry.store(request.cancelled && request.cancelled());
    { std::lock_guard lock(mutex); requestedNames.append(request.outputName); }
    while (releaseThrough.load() < id) {
      if (request.cancelled && request.cancelled()) sawCancel.store(true);
      QThread::msleep(2);
    }
    std::lock_guard lock(mutex);
    auto frame = id == 1 ? first : second;
    finished.fetch_add(1);
    return frame;
  }
};
struct Harness {
  QTemporaryDir dir;
  QString outputs = dir.filePath("outputs");
  QString profilePath = dir.filePath("xdg/config/XerahS/application-settings.json");
  ApplicationSettingsStore store;
  CaptureDisplays screens;
  QImage portalImage;
  PortalScreenshot *portal = nullptr;
  int portalCalls = 0;
  bool deliverPortal = false;
  FrameReader reader;
  EmptyWindows *windows = nullptr;
  std::unique_ptr<CaptureController> controller;
  explicit Harness(CaptureDisplays displays)
      : store(profilePath, outputs), screens(std::move(displays)),
        controller(std::make_unique<CaptureController>(outputs, nullptr, [this](QObject *owner) {
          ++portalCalls;
          portal = new PortalScreenshot(QDBusConnection(QStringLiteral("no-transport-fixture")), {}, owner);
          return portal;
        })) {
    const auto root = dir.filePath("xdg");
    for (const char *name : {"config", "state", "data", "cache", "runtime"}) QDir().mkpath(root + QLatin1Char('/') + name);
    const auto runtime = root + QStringLiteral("/runtime");
    QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("XDG_CONFIG_HOME", (root + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_STATE_HOME", (root + QStringLiteral("/state")).toUtf8());
    qputenv("XDG_DATA_HOME", (root + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (root + QStringLiteral("/cache")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", runtime.toUtf8());
    controller->setTopologyReader([this] { return screens; });
    controller->setRegionSettingsResolver([this] { return regionSessionSettings(store.current().values); });
    controller->setCompositorCapture([reader = &this->reader](const CompositorFrameRequest &request) { return (*reader)(request); });
    auto discovery = std::make_unique<EmptyWindows>();
    windows = discovery.get();
    controller->setWindowDiscovery(std::move(discovery));
    QObject::connect(controller.get(), &CaptureController::busyChanged, controller.get(), [this](bool busy) {
      if (busy && deliverPortal && portal) emit portal->finished(PortalScreenshot::Outcome::Captured, portalImage, {});
    });
  }
  bool save(const QJsonObject &values) { return store.save(values).isEmpty(); }
  void drag(qint64 right, qint64 bottom) { controller->pointerDown({8, 8}); controller->pointerUp({right, bottom}); }
};
}  // namespace

class CompositorCaptureIntegrationTest final : public QObject {
  Q_OBJECT
private slots:
  void defaultProfileOmitsTheCursorAndUsesTheScreenshotFactory() {
    Harness harness(oneOutput({0, 0, 200, 120}, 1, QStringLiteral("Hypr-Synthetic-1")));
    QVERIFY(!harness.screens.empty());
    const auto fresh = regionSessionSettings(harness.store.current().values);
    QVERIFY(fresh.quickConfirm); QVERIFY(fresh.snapping); QVERIFY(!fresh.includeCursor);
    QVERIFY(harness.save(harness.store.current().values));
    ApplicationSettingsStore reopened(harness.profilePath, harness.outputs);
    const auto persisted = regionSessionSettings(reopened.current().values);
    QVERIFY(persisted.quickConfirm); QVERIFY(persisted.snapping); QVERIFY(!persisted.includeCursor);
    harness.portalImage = scene({200, 120}, false);
    harness.deliverPortal = true;
    harness.controller->captureRegion();
    QTRY_COMPARE(overlays().size(), 1);
    QCOMPARE(harness.portalCalls, 1);
    QCOMPARE(harness.reader.calls.load(), 0);
    QCOMPARE(harness.windows->calls, 1);
    harness.drag(80, 60);
    QTRY_VERIFY(!harness.controller->busy());
    const auto receipt = harness.controller->lastPipelineResult();
    QVERIFY(receipt && receipt->file);
    QVERIFY(receipt->file->path.startsWith(harness.outputs));
    const QImage saved(receipt->file->path);
    QCOMPARE(saved, harness.portalImage.copy(8, 8, 72, 52));
    QCOMPARE(stampCount(saved), 0);
    QCOMPARE(overlays().size(), 0);
  }

  void optInUsesTheWorkerBeforeTheOverlayAndSavesTheStampOnce() {
    Harness harness(oneOutput(kLogical, kScale, QStringLiteral("Hypr-Synthetic-1")));
    QVERIFY(!harness.screens.empty());
    const auto &mapping = harness.screens[0].mapping;
    QCOMPARE(mapping.scale, kScale);
    QCOMPARE(mapping.logical.left, 80.0); QCOMPARE(mapping.logical.top, 40.0);
    QCOMPARE(mapping.physical.width(), qint64(320)); QCOMPARE(mapping.physical.height(), qint64(180));
    QVERIFY(harness.screens[0].screen.isNull());
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));
    QVERIFY(regionSessionSettings(harness.store.current().values).includeCursor);
    const QImage frame = scene({320, 180}, true);
    QCOMPARE(stampCount(frame), kStampRect.width() * kStampRect.height());
    harness.reader.setFirst(accepted(harness.screens[0], frame));
    harness.reader.releaseThrough.store(0);
    harness.deliverPortal = true;
    harness.controller->captureRegion();
    QTRY_COMPARE(harness.reader.calls.load(), 1);
    QCOMPARE(harness.reader.finished.load(), 0);
    QVERIFY(harness.reader.includeCursor.load());
    QVERIFY(harness.reader.hasCancelled.load());
    QVERIFY(!harness.reader.cancelledAtEntry.load());
    QCOMPARE(overlays().size(), 0);
    QCOMPARE(harness.portalCalls, 0);
    QCOMPARE(harness.windows->calls, 0);
    harness.reader.releaseThrough.store(1);
    QTRY_COMPARE(overlays().size(), 1);
    QCOMPARE(harness.windows->calls, 1);
    QCOMPARE(harness.reader.calls.load(), 1);
    QCOMPARE(harness.portalCalls, 0);
    harness.drag(140, 100);
    QTRY_VERIFY(!harness.controller->busy());
    const auto receipt = harness.controller->lastPipelineResult();
    QVERIFY(receipt && receipt->file);
    QVERIFY(receipt->file->path.startsWith(harness.outputs));
    const QImage saved(receipt->file->path);
    QCOMPARE(saved.size(), QSize(132, 92));
    QCOMPARE(saved, frame.copy(8, 8, 132, 92));
    QCOMPARE(stampCount(saved), kStampRect.width() * kStampRect.height());
    QCOMPARE(harness.reader.calls.load(), 1);
    QCOMPARE(harness.portalCalls, 0);
  }

  void unsupportedAndMismatchedSourcesRefuseWithoutPortalOrFiles_data() {
    QTest::addColumn<QString>("kind");
    for (const char *kind : {"two-outputs", "nameless-output", "unsupported", "source-mismatch", "foreign-output", "cursor-not-painted",
         "unproven-origin", "scale-mismatch", "logical-origin", "buffer-size"})
      QTest::newRow(kind) << QString::fromLatin1(kind);
  }
  void unsupportedAndMismatchedSourcesRefuseWithoutPortalOrFiles() {
    QFETCH(QString, kind);
    const bool layout = kind == QLatin1String("two-outputs") || kind == QLatin1String("nameless-output");
    CaptureDisplays screens;
    if (kind == QLatin1String("two-outputs")) {
      const auto mapped = snapshotScreenTopology({
          {QStringLiteral("Panel"), QStringLiteral("Maker"), QStringLiteral("Model"), QStringLiteral("1"), {0, 0, 100, 80}, 1, 0, {}, true},
          {QStringLiteral("Panel"), QStringLiteral("Maker"), QStringLiteral("Model"), QStringLiteral("2"), {100, 0, 200, 80}, 1, 0, {}, true}});
      QCOMPARE(mapped.size(), size_t(2));
      // RC-TOPOLOGY-AUTHORITY-2026-11-20: a multi-display session only
      // refuses a priori when a display has no compositor name to capture
      // by; named grids attempt the per-output captures.
      screens = {{nullptr, mapped[0], QStringLiteral("OUT-1")}, {nullptr, mapped[1], QString()}};
    } else {
      screens = oneOutput(kLogical, kScale, kind == QLatin1String("nameless-output") ? QString() : QStringLiteral("Hypr-Synthetic-1"));
    }
    Harness harness(std::move(screens));
    QVERIFY(!harness.screens.empty());
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));
    const QImage poison = scene({320, 180}, false);
    QImage filled = poison; filled.fill(kStamp);
    auto frame = accepted(harness.screens.front(), filled);
    QString diagnostic;
    if (kind == QLatin1String("unsupported")) { frame.status = CompositorFrame::Status::Unsupported; frame.diagnostic = kCompositorCaptureUnsupported; diagnostic = kCompositorCaptureUnsupported; }
    else if (kind == QLatin1String("source-mismatch")) { frame.status = CompositorFrame::Status::SourceMismatch; frame.diagnostic = kCompositorSourceMismatch; diagnostic = kCompositorSourceMismatch; }
    else if (kind == QLatin1String("foreign-output")) { frame.outputName = QStringLiteral("other-output"); frame.diagnostic = QStringLiteral("ignore-me"); diagnostic = diagnostic::captureTopologyMismatch; }
    else if (kind == QLatin1String("cursor-not-painted")) { frame.cursorPaintRequested = false; frame.cursorPaintedIntoFrame = false; diagnostic = diagnostic::captureTopologyMismatch; }
    else if (kind == QLatin1String("unproven-origin")) { frame.singleOutputBufferOriginProven = false; diagnostic = diagnostic::captureTopologyMismatch; }
    else if (kind == QLatin1String("scale-mismatch")) { frame.scale = 1; diagnostic = diagnostic::captureTopologyMismatch; }
    else if (kind == QLatin1String("logical-origin")) { frame.logicalX += 40; diagnostic = diagnostic::captureTopologyMismatch; }
    else if (kind == QLatin1String("buffer-size")) { frame.bufferWidth += 1; diagnostic = diagnostic::captureTopologyMismatch; }
    else diagnostic = kCompositorLayoutUnproven;
    if (!layout) harness.reader.setFirst(frame);
    harness.deliverPortal = true;
    harness.portalImage = poison;
    QSignalSpy notices(harness.controller.get(), &CaptureController::notify);
    harness.controller->captureRegion();
    QTRY_VERIFY(!harness.controller->busy());
    QCOMPARE(harness.reader.calls.load(), layout ? 0 : 1);
    QCOMPARE(harness.portalCalls, 0);
    QCOMPARE(overlays().size(), 0);
    QCOMPARE(harness.windows->calls, 0);
    QVERIFY(!harness.controller->lastPipelineResult());
    QCOMPARE(QDir(harness.outputs).entryList(QDir::Files).size(), 0);
    QCOMPARE(notices.size(), 1);
    const bool sourceRefusal = layout || kind == QLatin1String("unsupported") || kind == QLatin1String("source-mismatch");
    QCOMPARE(notices[0][1].toString(), compositorCaptureFailureMessage(sourceRefusal ? diagnostic : kCompositorSourceMismatch));
  }

  void frozenProfileSettingsSurviveChangesUntilTheNextCapture() {
    Harness harness(oneOutput(kLogical, kScale, QStringLiteral("Hypr-Synthetic-1")));
    QVERIFY(!harness.screens.empty());
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));
    const QImage stamped = scene({320, 180}, true);
    harness.reader.setFirst(accepted(harness.screens[0], stamped));
    harness.reader.releaseThrough.store(0);
    harness.deliverPortal = true;
    harness.controller->captureRegion();
    QTRY_COMPARE(harness.reader.calls.load(), 1);
    QCOMPARE(overlays().size(), 0);
    values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), false);
    values.insert(QStringLiteral("regionCapture.quickConfirm"), false);
    values.insert(QStringLiteral("regionCapture.snapping"), false);
    QVERIFY(harness.save(values));
    ApplicationSettingsStore reopened(harness.profilePath, harness.outputs);
    const auto changed = regionSessionSettings(reopened.current().values);
    QVERIFY(!changed.includeCursor); QVERIFY(!changed.quickConfirm); QVERIFY(!changed.snapping);
    harness.reader.releaseThrough.store(1);
    QTRY_COMPARE(overlays().size(), 1);
    QCOMPARE(harness.windows->calls, 1);
    QCOMPARE(harness.portalCalls, 0);
    harness.drag(140, 100);
    QTRY_VERIFY(!harness.controller->busy());
    const auto first = harness.controller->lastPipelineResult();
    QVERIFY(first && first->file);
    const QImage saved(first->file->path);
    QCOMPARE(saved, stamped.copy(8, 8, 132, 92));
    QCOMPARE(stampCount(saved), kStampRect.width() * kStampRect.height());
    QCOMPARE(harness.reader.calls.load(), 1);
    harness.portalImage = scene({320, 180}, false);
    harness.portalImage.fill(qRgb(90, 80, 70));
    harness.controller->captureRegion();
    QTRY_COMPARE(overlays().size(), 1);
    QCOMPARE(harness.portalCalls, 1);
    QCOMPARE(harness.reader.calls.load(), 1);
    QCOMPARE(harness.windows->calls, 1);
    harness.drag(140, 100);
    QVERIFY(harness.controller->selectionSettled());
    QVERIFY(harness.controller->busy());
    QCOMPARE(harness.controller->lastPipelineResult()->file->path, first->file->path);
    QVERIFY(harness.controller->key(Qt::Key_Return, Qt::NoModifier, harness.screens[0].mapping));
    QTRY_VERIFY(!harness.controller->busy());
    const auto second = harness.controller->lastPipelineResult();
    QVERIFY(second && second->file && second->file->path != first->file->path);
    const QImage portalSaved(second->file->path);
    QCOMPARE(portalSaved, harness.portalImage.copy(8, 8, 132, 92));
    QCOMPARE(stampCount(portalSaved), 0);
    QCOMPARE(harness.reader.calls.load(), 1);
  }

  void cancelAndStaleCompletionCannotPublishIntoANewerCapture() {
    Harness harness(oneOutput(kLogical, kScale, QStringLiteral("Hypr-Synthetic-1")));
    QVERIFY(!harness.screens.empty());
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));
    const QImage poison = scene({320, 180}, false);
    QImage flooded = poison; flooded.fill(kStamp);
    const QImage good = scene({320, 180}, true);
    harness.reader.setFirst(accepted(harness.screens[0], flooded));
    harness.reader.setSecond(accepted(harness.screens[0], good));
    harness.reader.releaseThrough.store(0);
    harness.deliverPortal = true;
    harness.controller->captureRegion();
    QTRY_COMPARE(harness.reader.calls.load(), 1);
    harness.controller->cancel();
    QTRY_VERIFY(!harness.controller->busy());
    QTRY_VERIFY(harness.reader.sawCancel.load());
    QCOMPARE(harness.reader.finished.load(), 0);
    QCOMPARE(harness.portalCalls, 0);
    QCOMPARE(QDir(harness.outputs).entryList(QDir::Files).size(), 0);
    harness.controller->captureRegion();
    QTRY_COMPARE(harness.reader.calls.load(), 2);
    QCOMPARE(overlays().size(), 0);
    harness.reader.releaseThrough.store(1);
    QTRY_COMPARE(harness.reader.finished.load(), 1);
    QTest::qWait(30);
    QVERIFY(harness.controller->busy());
    QCOMPARE(overlays().size(), 0);
    QVERIFY(!harness.controller->lastPipelineResult());
    QCOMPARE(QDir(harness.outputs).entryList(QDir::Files).size(), 0);
    QCOMPARE(harness.portalCalls, 0);
    harness.reader.releaseThrough.store(2);
    QTRY_COMPARE(overlays().size(), 1);
    harness.drag(140, 100);
    QTRY_VERIFY(!harness.controller->busy());
    const auto receipt = harness.controller->lastPipelineResult();
    QVERIFY(receipt && receipt->file);
    const QImage saved(receipt->file->path);
    QCOMPARE(saved, good.copy(8, 8, 132, 92));
    QCOMPARE(stampCount(saved), kStampRect.width() * kStampRect.height());
    QVERIFY(stampCount(saved) < saved.width() * saved.height());
  }

  void ownerLossCannotPublishIntoANewerCapture() {
    auto older = std::make_unique<Harness>(oneOutput(kLogical, kScale, QStringLiteral("Hypr-Synthetic-1")));
    QVERIFY(!older->screens.empty());
    auto values = older->store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(older->save(values));
    QImage flooded = scene({320, 180}, false); flooded.fill(kStamp);
    older->reader.setFirst(accepted(older->screens[0], flooded));
    older->reader.releaseThrough.store(0);
    older->controller->captureRegion();
    QTRY_COMPARE(older->reader.calls.load(), 1);
    const auto abandoned = older->outputs;
    older->controller.reset();
    Harness newer(oneOutput(kLogical, kScale, QStringLiteral("Hypr-Synthetic-1")));
    newer.portalImage = scene({320, 180}, false);
    newer.deliverPortal = true;
    newer.controller->captureRegion();
    QTRY_COMPARE(overlays().size(), 1);
    QCOMPARE(newer.portalCalls, 1);
    QCOMPARE(newer.reader.calls.load(), 0);
    older->reader.releaseThrough.store(1);
    QTRY_COMPARE(older->reader.finished.load(), 1);
    QTest::qWait(30);
    QCOMPARE(QDir(abandoned).entryList(QDir::Files).size(), 0);
    QCOMPARE(overlays().size(), 1);
    QVERIFY(newer.controller->busy());
    newer.drag(140, 100);
    QTRY_VERIFY(!newer.controller->busy());
    const auto receipt = newer.controller->lastPipelineResult();
    QVERIFY(receipt && receipt->file);
    QCOMPARE(QImage(receipt->file->path), newer.portalImage.copy(8, 8, 132, 92));
    QCOMPARE(stampCount(QImage(receipt->file->path)), 0);
    older.reset();
  }

  // RC-012/CORE-014 on RC-TOPOLOGY-AUTHORITY-2026-11-20: the compose seam
  // places captured frames at the session's packed origins after
  // cross-checking every frame against its frozen display, and refuses any
  // disagreement whole.
  void composeCapturedFramesPlacesPackedOriginsAndRefusesMismatches() {
    const CaptureDisplays displays = twoOutputs();
    QVERIFY(displays.size() == 2);
    QCOMPARE(displays[0].mapping.physical.left, qint64(0));
    QCOMPARE(displays[0].mapping.physical.width(), qint64(100));
    QCOMPARE(displays[1].mapping.physical.left, qint64(100));
    QCOMPARE(displays[1].mapping.physical.width(), qint64(300));
    QCOMPARE(displays[1].mapping.physical.height(), qint64(160));
    QCOMPARE(displays[1].mapping.scale, 2.0);

    QImage left(100, 80, QImage::Format_ARGB32);
    left.fill(qRgb(201, 10, 11));
    QImage right(300, 160, QImage::Format_ARGB32);
    right.fill(qRgb(12, 202, 13));
    const auto composed = composeCapturedFrames(
        displays, {acceptedFrame(displays[0], left), acceptedFrame(displays[1], right)});
    QVERIFY2(composed.ok, qPrintable(composed.diagnostic));
    QCOMPARE(composed.image.width(), 400);
    QCOMPARE(composed.image.height(), 160);
    QCOMPARE(composed.image.pixel(50, 40), qRgb(201, 10, 11));
    QCOMPARE(composed.image.pixel(150, 80), qRgb(12, 202, 13));
    QCOMPARE(composed.image.pixel(399, 159), qRgb(12, 202, 13));

    // Every disagreement refuses whole, never a misaligned composite.
    auto scaleOff = acceptedFrame(displays[1], right);
    scaleOff.scale = 1;
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), scaleOff}).ok);
    auto logicalOff = acceptedFrame(displays[1], right);
    logicalOff.logicalX += 40;
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), logicalOff}).ok);
    auto boundsOff = acceptedFrame(displays[1], right);
    boundsOff.bufferWidth += 1;
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), boundsOff}).ok);
    auto rotated = acceptedFrame(displays[1], right);
    rotated.transform = 1;
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), rotated}).ok);
    auto unproven = acceptedFrame(displays[1], right);
    unproven.singleOutputBufferOriginProven = false;
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), unproven}).ok);
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left)}).ok);   // missing display
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), acceptedFrame(displays[0], left)}).ok);   // duplicate
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), acceptedFrame(displays[1], right),
                                             acceptedFrame(displays[1], right)})
                 .ok);   // extra frame
    auto foreign = acceptedFrame(displays[1], right);
    foreign.outputName = QStringLiteral("OUT-Z");
    QVERIFY(!composeCapturedFrames(displays, {acceptedFrame(displays[0], left), foreign}).ok);
    auto unnamed = displays;
    unnamed[1].outputName.clear();
    QVERIFY(!composeCapturedFrames(unnamed, {acceptedFrame(displays[0], left), acceptedFrame(displays[1], right)}).ok);
  }

  void twoOutputsComposeOneDesktopOnAProvenGrid() {
    Harness harness(twoOutputs());
    QVERIFY(harness.screens.size() == 2);
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));

    QImage left = scene({100, 80}, false);
    left.fill(qRgb(201, 10, 11));
    QImage right = scene({300, 160}, false);
    right.fill(qRgb(12, 202, 13));
    harness.reader.setFirst(accepted(harness.screens[0], left));
    harness.reader.setSecond(accepted(harness.screens[1], right));
    harness.deliverPortal = true;
    harness.controller->captureRegion();
    QTRY_COMPARE(harness.reader.calls.load(), 2);
    QCOMPARE(harness.portalCalls, 0);
    {
      std::lock_guard<std::mutex> lock(harness.reader.mutex);
      const QStringList expected{QStringLiteral("OUT-1"), QStringLiteral("OUT-2")};
      QCOMPARE(harness.reader.requestedNames, expected);
    }
    // RC-005: the frozen frame covers every capturable display, so the
    // session shows one overlay per display.
    QTRY_COMPARE(overlays().size(), 2);
    // A cross-monitor drag: the confirmed rectangle spans both displays, so
    // the crop mixes both frames' pixels at the packed boundary (x=100).
    harness.drag(240, 60);
    QTRY_VERIFY(!harness.controller->busy());
    const auto receipt = harness.controller->lastPipelineResult();
    QVERIFY(receipt && receipt->file);
    const QImage saved(receipt->file->path);
    QCOMPARE(saved.size(), QSize(232, 52));
    QCOMPARE(saved.pixel(50, 20), qRgb(201, 10, 11));
    // The packed boundary is at composite x=100, so saved x=92: the left of
    // the boundary carries OUT-1's pixels and the right carries OUT-2's.
    QCOMPARE(saved.pixel(91, 20), qRgb(201, 10, 11));
    QCOMPARE(saved.pixel(92, 20), qRgb(12, 202, 13));
    QCOMPARE(saved.pixel(200, 40), qRgb(12, 202, 13));
    QCOMPARE(harness.reader.calls.load(), 2);
    QCOMPARE(harness.portalCalls, 0);
  }

  void composedMismatchRefusesTheWholeCapture() {
    Harness harness(twoOutputs());
    QVERIFY(harness.screens.size() == 2);
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));
    QImage left = scene({100, 80}, false);
    QImage right = scene({300, 160}, false);
    // OUT-2's frame carries the wrong scale: the compose seam must refuse
    // the whole desktop rather than composite misaligned pixels.
    auto wrong = accepted(harness.screens[1], right);
    wrong.scale = 1;
    harness.reader.setFirst(accepted(harness.screens[0], left));
    harness.reader.setSecond(wrong);
    harness.deliverPortal = true;
    QSignalSpy notices(harness.controller.get(), &CaptureController::notify);
    harness.controller->captureRegion();
    QTRY_VERIFY(!harness.controller->busy());
    QCOMPARE(harness.reader.calls.load(), 2);
    QCOMPARE(harness.portalCalls, 0);
    QCOMPARE(overlays().size(), 0);
    QVERIFY(!harness.controller->lastPipelineResult());
    QCOMPARE(QDir(harness.outputs).entryList(QDir::Files).size(), 0);
    QCOMPARE(notices.size(), 1);
    QCOMPARE(notices[0][1].toString(),
             compositorCaptureFailureMessage(QStringLiteral("compositor-source-mismatch")));
  }

  // CORE-014: named behavioral evidence for the cursor option on the three
  // whole-source captures. Every source routes through the compositor-native
  // path when the option is on, is captured with the cursor paint requested,
  // and its identified target is what the crop carries: the whole desktop
  // (full-screen), the display under the cursor (monitor, resolved through
  // the real screen's identity), the focused window (active-window, injected),
  // or the honest target-lost refusal when no window exists.
  void wholeSourcesCaptureThroughTheCompositorCursorPath_data() {
    QTest::addColumn<QString>("source");
    QTest::newRow("full-screen") << QStringLiteral("full-screen");
    QTest::newRow("monitor") << QStringLiteral("monitor");
    QTest::newRow("active-window") << QStringLiteral("active-window");
    QTest::newRow("active-window-lost") << QStringLiteral("active-window-lost");
  }

  void wholeSourcesCaptureThroughTheCompositorCursorPath() {
    // Regression guard for the succeeded-title formatting: the capture
    // notification path must not emit a spurious QString::arg warning.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("^QString::arg: ")));
    QFETCH(QString, source);
    const bool lost = source == QLatin1String("active-window-lost");
    const QString capture = lost ? QStringLiteral("active-window") : source;
    CaptureDisplays screens = capture == QLatin1String("monitor") ? realScreenDisplays()
                                                                 : oneOutput(kLogical, kScale, QStringLiteral("Hypr-Synthetic-1"));
    QVERIFY(!screens.empty());
    const auto &mapping = screens.front().mapping;
    auto windows = std::make_unique<FixedWindows>();
    windows->hasWindow = !lost;
    Harness harness(std::move(screens));
    QVERIFY(!harness.screens.empty());
    harness.controller->setWindowDiscovery(std::move(windows));
    auto values = harness.store.current().values;
    values.insert(QStringLiteral("regionCapture.includeCursor"), true);
    QVERIFY(harness.save(values));

    const QImage frame = scene({int(mapping.physical.width()), int(mapping.physical.height())}, true);
    harness.reader.setFirst(accepted(harness.screens.front(), frame));
    harness.deliverPortal = true;
    auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries[0];
    workflow.capture = capture;
    workflow.actions = QStringList{QStringLiteral("save")};
    workflow.output.directory = harness.outputs;
    workflow.output.pattern = QStringLiteral("whole-%widthx%height");
    QSignalSpy notices(harness.controller.get(), &CaptureController::notify);
    harness.controller->captureWorkflow(workflow);
    QTRY_VERIFY(!harness.controller->busy());
    QCOMPARE(harness.reader.calls.load(), 1);
    QVERIFY(harness.reader.includeCursor.load());
    QCOMPARE(harness.portalCalls, 0);
    QCOMPARE(overlays().size(), 0);
    if (lost) {
      // CORE-014: the target-lost result is explicit; nothing is captured.
      QVERIFY(!harness.controller->lastPipelineResult());
      QCOMPARE(QDir(harness.outputs).entryList(QDir::Files).size(), 0);
      QCOMPARE(notices.size(), 1);
      QVERIFY(notices[0][1].toString().contains(QStringLiteral("No active window")));
      return;
    }
    const auto receipt = harness.controller->lastPipelineResult();
    QVERIFY(receipt && receipt->file);
    const QImage saved(receipt->file->path);
    if (capture == QLatin1String("active-window")) {
      QCOMPARE(saved.size(), QSize(64, 48));
      QCOMPARE(saved, frame.copy(16, 12, 64, 48));
    } else {
      // Full-screen takes the whole desktop; monitor takes the whole
      // display - the same pixels with one display.
      QCOMPARE(saved.size(), QSize(int(mapping.physical.width()), int(mapping.physical.height())));
      QCOMPARE(saved, frame);
    }
    QCOMPARE(stampCount(saved), kStampRect.width() * kStampRect.height());
  }
};

QTEST_MAIN(CompositorCaptureIntegrationTest)
#include "CompositorCaptureIntegrationTest.moc"
