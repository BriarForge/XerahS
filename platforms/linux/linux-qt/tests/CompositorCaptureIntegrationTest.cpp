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

#include <QApplication>
#include <QColorSpace>
#include <QDir>
#include <QFileDevice>
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
  void setFirst(CompositorFrame frame) { std::lock_guard lock(mutex); first = std::move(frame); }
  void setSecond(CompositorFrame frame) { std::lock_guard lock(mutex); second = std::move(frame); }
  CompositorFrame operator()(const CompositorFrameRequest &request) {
    const int id = calls.fetch_add(1) + 1;
    includeCursor.store(request.includeCursor);
    hasCancelled.store(static_cast<bool>(request.cancelled));
    if (id == 1) cancelledAtEntry.store(request.cancelled && request.cancelled());
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
      screens = {{nullptr, mapped[0], QStringLiteral("OUT-1")}, {nullptr, mapped[1], QStringLiteral("OUT-2")}};
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
};

QTEST_MAIN(CompositorCaptureIntegrationTest)
#include "CompositorCaptureIntegrationTest.moc"
