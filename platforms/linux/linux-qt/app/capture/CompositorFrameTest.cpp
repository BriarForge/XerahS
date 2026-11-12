#include "capture/CompositorFrame.h"

#include "ext-image-capture-source-v1-server-protocol.h"
#include "ext-image-copy-capture-v1-server-protocol.h"
#include "wlr-screencopy-unstable-v1-server-protocol.h"

#include <QTest>
#include <QElapsedTimer>

#include <wayland-server.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using xerahs::app::CompositorFrame;
using xerahs::app::CompositorFrameCapture;
using xerahs::app::CompositorFrameRequest;
using xerahs::app::CompositorOutputFacts;
using xerahs::app::LayoutDecisionKind;
using xerahs::app::ProtocolChoice;
using xerahs::app::chooseCaptureProtocol;
using xerahs::app::kCompositorCaptureFailed;
using xerahs::app::kCompositorCaptureTimedOut;
using xerahs::app::kCompositorCaptureUnsupported;
using xerahs::app::kCompositorLayoutUnproven;
using xerahs::app::kCompositorSourceMismatch;
using xerahs::app::proveSingleOutputLayout;

namespace {

struct Pool {
  void *map = nullptr;
  int32_t size = 0;
};

struct ShmBuffer {
  uint8_t *data = nullptr;
  int32_t width = 0;
  int32_t height = 0;
  int32_t stride = 0;
};

struct Scene {
  int outputs = 1;
  bool sendGeometry = true;
  bool sendMode = true;
  bool sendScale = true;
  bool sendName = true;
  bool sendDone = true;
  int logicalX = -40;
  int logicalY = 12;
  int scale = 2;
  int transform = 0;
  int modeW = 6;
  int modeH = 4;
  int bufferW = 6;
  int bufferH = 4;
  bool advertiseExt = true;
  bool advertiseWlr = false;
  bool xrgb = false;
  bool yBands = false;
  bool failFrame = false;
  bool hang = false;
  bool sendTransform = true;
  int frameTransform = 0;
  bool sendFlags = true;
  uint32_t wlrFlags = 0;
  uint32_t stridePadding = 0;
  int sourceCreates = 0;
  int extSessions = 0;
  int extCursorSessions = 0;
  int extFrames = 0;
  int lastExtOptions = -1;
  int damageWidth = -1;
  int damageHeight = -1;
  int wlrCaptures = 0;
  int wlrRegionCaptures = 0;
  int wlrCopies = 0;
  int wlrDamageCopies = 0;
  int lastOverlay = -1;
  int boundOutputs = 0;
};

void storePixel(uint8_t *pixel, uint32_t word) {
  pixel[0] = uint8_t(word);
  pixel[1] = uint8_t(word >> 8);
  pixel[2] = uint8_t(word >> 16);
  pixel[3] = uint8_t(word >> 24);
}

void paintBuffer(ShmBuffer *buffer, const Scene &scene, bool cursor) {
  for (int y = 0; y < buffer->height; ++y) {
    for (int x = 0; x < buffer->width; ++x) {
      uint32_t color = 0xFF0A141Eu;
      if (scene.yBands && y == 0) color = 0xFF0000FFu;
      if (scene.yBands && y == buffer->height - 1) color = 0xFFFF0000u;
      if (!scene.yBands && x == 0 && y == 0) color = 0xFF070707u;
      if (!scene.yBands && x == 1 && y == 1) color = scene.xrgb ? 0x00050607u : 0x80010203u;
      if (!scene.yBands && cursor && x == 2 && y == 1) color = 0xFFFF00FFu;
      storePixel(buffer->data + y * buffer->stride + x * 4, color);
    }
  }
}

void destroyPool(wl_resource *resource) {
  auto *pool = static_cast<Pool *>(wl_resource_get_user_data(resource));
  if (pool && pool->map && pool->map != MAP_FAILED) munmap(pool->map, size_t(pool->size));
  delete pool;
}

void destroyBuffer(wl_resource *resource) { delete static_cast<ShmBuffer *>(wl_resource_get_user_data(resource)); }

void shmCreateBuffer(wl_client *client, wl_resource *resource, uint32_t id, int32_t offset, int32_t width, int32_t height, int32_t stride, uint32_t) {
  auto *pool = static_cast<Pool *>(wl_resource_get_user_data(resource));
  auto *buffer = new ShmBuffer;
  buffer->data = static_cast<uint8_t *>(pool->map) + offset;
  buffer->width = width;
  buffer->height = height;
  buffer->stride = stride;
  wl_resource *created = wl_resource_create(client, &wl_buffer_interface, 1, id);
  static const struct wl_buffer_interface implementation{[](wl_client *, wl_resource *object) { wl_resource_destroy(object); }};
  wl_resource_set_implementation(created, &implementation, buffer, destroyBuffer);
}

void shmDestroyPool(wl_client *, wl_resource *resource) {
  wl_resource_destroy(resource);
}

void shmResize(wl_client *, wl_resource *, int32_t) {}

void shmCreatePool(wl_client *client, wl_resource *, uint32_t id, int32_t fd, int32_t size) {
  auto *pool = new Pool;
  pool->size = size;
  pool->map = mmap(nullptr, size_t(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  wl_resource *created = wl_resource_create(client, &wl_shm_pool_interface, 1, id);
  static const struct wl_shm_pool_interface implementation{shmCreateBuffer, shmDestroyPool, shmResize};
  wl_resource_set_implementation(created, &implementation, pool, destroyPool);
}

void bindShm(wl_client *client, void *, uint32_t version, uint32_t id) {
  wl_resource *resource = wl_resource_create(client, &wl_shm_interface, int(version), id);
  static const struct wl_shm_interface implementation{shmCreatePool
#ifdef WL_SHM_RELEASE_SINCE_VERSION
    , [](wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }
#endif
  };
  wl_resource_set_implementation(resource, &implementation, nullptr, nullptr);
  wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
  wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
}

void bindOutput(wl_client *client, void *data, uint32_t version, uint32_t id) {
  auto *scene = static_cast<Scene *>(data);
  const int index = scene->boundOutputs++;
  wl_resource *resource = wl_resource_create(client, &wl_output_interface, int(version), id);
  static const struct wl_output_interface implementation{[](wl_client *, wl_resource *) {}};
  wl_resource_set_implementation(resource, &implementation, scene, nullptr);
  if (scene->sendGeometry) {
    wl_output_send_geometry(resource, scene->logicalX + index * 100, scene->logicalY, 300, 200, WL_OUTPUT_SUBPIXEL_UNKNOWN, "Mock", "Output", scene->transform);
  }
  if (scene->sendMode) wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, scene->modeW, scene->modeH, 60000);
  if (version >= 2 && scene->sendScale) wl_output_send_scale(resource, scene->scale);
  if (version >= 4 && scene->sendName) {
    char name[8];
    std::snprintf(name, sizeof name, "OUT-%c", char('A' + index));
    wl_output_send_name(resource, name);
    wl_output_send_description(resource, "mock output");
  }
  if (version >= 2 && scene->sendDone) wl_output_send_done(resource);
}

void sourceDestroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }

void extCreateSource(wl_client *client, wl_resource *resource, uint32_t id, wl_resource *) {
  auto *scene = static_cast<Scene *>(wl_resource_get_user_data(resource));
  scene->sourceCreates++;
  wl_resource *created = wl_resource_create(client, &ext_image_capture_source_v1_interface, 1, id);
  static const struct ext_image_capture_source_v1_interface implementation{sourceDestroy};
  wl_resource_set_implementation(created, &implementation, scene, nullptr);
}

void bindSources(wl_client *client, void *data, uint32_t version, uint32_t id) {
  wl_resource *resource = wl_resource_create(client, &ext_output_image_capture_source_manager_v1_interface, int(version), id);
  static const struct ext_output_image_capture_source_manager_v1_interface implementation{
      extCreateSource, [](wl_client *, wl_resource *object) { wl_resource_destroy(object); }};
  wl_resource_set_implementation(resource, &implementation, data, nullptr);
}

void extFrameDestroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }

struct FrameState {
  Scene *scene = nullptr;
  ShmBuffer *buffer = nullptr;
};

void extAttachState(wl_client *, wl_resource *resource, wl_resource *buffer) {
  auto *state = static_cast<FrameState *>(wl_resource_get_user_data(resource));
  state->buffer = static_cast<ShmBuffer *>(wl_resource_get_user_data(buffer));
}

void extDamageState(wl_client *, wl_resource *resource, int32_t, int32_t, int32_t width, int32_t height) {
  auto *state = static_cast<FrameState *>(wl_resource_get_user_data(resource));
  state->scene->damageWidth = width;
  state->scene->damageHeight = height;
}

void extCaptureState(wl_client *, wl_resource *resource) {
  auto *state = static_cast<FrameState *>(wl_resource_get_user_data(resource));
  Scene *scene = state->scene;
  if (scene->hang) return;
  if (!scene->failFrame && state->buffer) paintBuffer(state->buffer, *scene, (scene->lastExtOptions & 1) != 0);
  if (scene->failFrame) {
    ext_image_copy_capture_frame_v1_send_failed(resource, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
    return;
  }
  if (scene->sendTransform) ext_image_copy_capture_frame_v1_send_transform(resource, uint32_t(scene->frameTransform));
  ext_image_copy_capture_frame_v1_send_damage(resource, 0, 0, scene->bufferW, scene->bufferH);
  ext_image_copy_capture_frame_v1_send_presentation_time(resource, 0, 5, 250000000);
  ext_image_copy_capture_frame_v1_send_ready(resource);
}

void extCreateFrame(wl_client *client, wl_resource *resource, uint32_t id) {
  auto *scene = static_cast<Scene *>(wl_resource_get_user_data(resource));
  scene->extFrames++;
  auto *state = new FrameState{scene, nullptr};
  wl_resource *created = wl_resource_create(client, &ext_image_copy_capture_frame_v1_interface, 1, id);
  static const struct ext_image_copy_capture_frame_v1_interface implementation{extFrameDestroy, extAttachState, extDamageState, extCaptureState};
  wl_resource_set_implementation(created, &implementation, state, [](wl_resource *object) { delete static_cast<FrameState *>(wl_resource_get_user_data(object)); });
}

void extCreateSession(wl_client *client, wl_resource *resource, uint32_t id, wl_resource *, uint32_t options) {
  auto *scene = static_cast<Scene *>(wl_resource_get_user_data(resource));
  scene->extSessions++;
  scene->lastExtOptions = int(options);
  wl_resource *created = wl_resource_create(client, &ext_image_copy_capture_session_v1_interface, 1, id);
  static const struct ext_image_copy_capture_session_v1_interface implementation{
      extCreateFrame, [](wl_client *, wl_resource *object) { wl_resource_destroy(object); }};
  wl_resource_set_implementation(created, &implementation, scene, nullptr);
  ext_image_copy_capture_session_v1_send_buffer_size(created, uint32_t(scene->bufferW), uint32_t(scene->bufferH));
  ext_image_copy_capture_session_v1_send_shm_format(created, scene->xrgb ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888);
  ext_image_copy_capture_session_v1_send_done(created);
}

void extCreateCursor(wl_client *client, wl_resource *resource, uint32_t id, wl_resource *, wl_resource *) {
  auto *scene = static_cast<Scene *>(wl_resource_get_user_data(resource));
  scene->extCursorSessions++;
  wl_resource *created = wl_resource_create(client, &ext_image_copy_capture_cursor_session_v1_interface, 1, id);
  static const struct ext_image_copy_capture_cursor_session_v1_interface implementation{
      [](wl_client *, wl_resource *object) { wl_resource_destroy(object); }, [](wl_client *, wl_resource *, uint32_t) {}};
  wl_resource_set_implementation(created, &implementation, scene, nullptr);
}

void bindExt(wl_client *client, void *data, uint32_t version, uint32_t id) {
  wl_resource *resource = wl_resource_create(client, &ext_image_copy_capture_manager_v1_interface, int(version), id);
  static const struct ext_image_copy_capture_manager_v1_interface implementation{
      extCreateSession, extCreateCursor, [](wl_client *, wl_resource *object) { wl_resource_destroy(object); }};
  wl_resource_set_implementation(resource, &implementation, data, nullptr);
}

struct WlrFrame {
  Scene *scene = nullptr;
  ShmBuffer *buffer = nullptr;
};

void wlrCopy(wl_client *, wl_resource *resource, wl_resource *buffer) {
  auto *state = static_cast<WlrFrame *>(wl_resource_get_user_data(resource));
  state->scene->wlrCopies++;
  state->buffer = static_cast<ShmBuffer *>(wl_resource_get_user_data(buffer));
  if (!state->buffer || uint32_t(state->buffer->stride) != uint32_t(state->scene->bufferW * 4) + state->scene->stridePadding) {
    zwlr_screencopy_frame_v1_send_failed(resource); return;
  }
  if (state->scene->hang) return;
  if (state->scene->failFrame) {
    zwlr_screencopy_frame_v1_send_failed(resource);
    return;
  }
  if (state->buffer) paintBuffer(state->buffer, *state->scene, state->scene->lastOverlay == 1);
  if (state->scene->sendFlags) zwlr_screencopy_frame_v1_send_flags(resource, state->scene->wlrFlags);
  zwlr_screencopy_frame_v1_send_ready(resource, 0, 9, 1000);
}

void wlrCopyDamage(wl_client *, wl_resource *resource, wl_resource *) {
  static_cast<WlrFrame *>(wl_resource_get_user_data(resource))->scene->wlrDamageCopies++;
}

void wlrStart(wl_client *client, Scene *scene, uint32_t id, int32_t overlay, bool region) {
  if (region) scene->wlrRegionCaptures++;
  else scene->wlrCaptures++;
  scene->lastOverlay = overlay;
  auto *state = new WlrFrame{scene, nullptr};
  wl_resource *created = wl_resource_create(client, &zwlr_screencopy_frame_v1_interface, 3, id);
  static const struct zwlr_screencopy_frame_v1_interface implementation{
      wlrCopy, [](wl_client *, wl_resource *object) { wl_resource_destroy(object); }, wlrCopyDamage};
  wl_resource_set_implementation(created, &implementation, state, [](wl_resource *object) { delete static_cast<WlrFrame *>(wl_resource_get_user_data(object)); });
  zwlr_screencopy_frame_v1_send_buffer(created, scene->xrgb ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888, uint32_t(scene->bufferW),
                                       uint32_t(scene->bufferH), uint32_t(scene->bufferW * 4) + scene->stridePadding);
  zwlr_screencopy_frame_v1_send_buffer_done(created);
}

void bindWlr(wl_client *client, void *data, uint32_t version, uint32_t id) {
  wl_resource *resource = wl_resource_create(client, &zwlr_screencopy_manager_v1_interface, int(version), id);
  static const struct zwlr_screencopy_manager_v1_interface implementation{
      [](wl_client *client, wl_resource *resource, uint32_t frame, int32_t overlay, wl_resource *) {
        wlrStart(client, static_cast<Scene *>(wl_resource_get_user_data(resource)), frame, overlay, false);
      },
      [](wl_client *client, wl_resource *resource, uint32_t frame, int32_t overlay, wl_resource *, int32_t, int32_t, int32_t, int32_t) {
        wlrStart(client, static_cast<Scene *>(wl_resource_get_user_data(resource)), frame, overlay, true);
      },
      [](wl_client *, wl_resource *object) { wl_resource_destroy(object); }};
  wl_resource_set_implementation(resource, &implementation, data, nullptr);
}

struct RunningServer {
  Scene scene;
  std::atomic<bool> stopping{false};
  wl_display *display = nullptr;
  int clientFd = -1;
  std::thread thread;

  explicit RunningServer(Scene seed) : scene(std::move(seed)) {}
  ~RunningServer() { stop(); }

  bool start() {
    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) return false;
    display = wl_display_create();
    if (!display) {
      close(fds[0]);
      close(fds[1]);
      return false;
    }
    wl_global_create(display, &wl_shm_interface, 1, &scene, bindShm);
    for (int output = 0; output < scene.outputs; ++output) wl_global_create(display, &wl_output_interface, 4, &scene, bindOutput);
    if (scene.advertiseExt) {
      wl_global_create(display, &ext_output_image_capture_source_manager_v1_interface, 1, &scene, bindSources);
      wl_global_create(display, &ext_image_copy_capture_manager_v1_interface, 1, &scene, bindExt);
    }
    if (scene.advertiseWlr) wl_global_create(display, &zwlr_screencopy_manager_v1_interface, 3, &scene, bindWlr);
    if (!wl_client_create(display, fds[1])) {
      close(fds[0]);
      return false;
    }
    clientFd = fds[0];
    thread = std::thread([this] {
      while (!stopping.load()) {
        wl_display_flush_clients(display);
        wl_event_loop_dispatch(wl_display_get_event_loop(display), 20);
      }
    });
    return true;
  }

  void stop() {
    stopping.store(true);
    if (thread.joinable()) thread.join();
    if (display) wl_display_destroy(display);
    display = nullptr;
    if (clientFd >= 0) close(clientFd);
    clientFd = -1;
  }
};

CompositorFrame captureScene(Scene scene, const CompositorFrameRequest &request, Scene *observed = nullptr) {
  RunningServer server(std::move(scene));
  if (!server.start()) return {};
  const CompositorFrame frame = CompositorFrameCapture::captureOnFd(server.clientFd, request);
  server.stop();
  if (observed) *observed = server.scene;
  return frame;
}

quint32 pixelAt(const QImage &image, int x, int y) {
  return *reinterpret_cast<const quint32 *>(image.constScanLine(y) + x * 4);
}

CompositorOutputFacts provenOutput() {
  CompositorOutputFacts output;
  output.sawGeometry = output.sawCurrentMode = output.sawScale = output.sawDone = output.sawName = true;
  output.name = QStringLiteral("OUT-A");
  output.logicalX = -40;
  output.logicalY = 12;
  output.modeWidth = 6;
  output.modeHeight = 4;
  output.scale = 2;
  output.transform = 0;
  return output;
}

}  // namespace

class CompositorFrameTest : public QObject {
  Q_OBJECT
private slots:
  void cursorInclusionDefaultsOff();
  void extPaintFlagIsPreferredAndNeverOpensACursorSession();
  void wlrOverlayIsOnlyTheFallback();
  void singleUnrotatedOutputIsProvenFromItsMode();
  void extraRotatedNamelessAndIncompleteLayoutsAreRefused();
  void extPaintsTheCursorIntoTheSameBuffer();
  void extOffKeepsASoftwareCursorAndDoesNotInventOne();
  void extXrgbForcesOpaqueAlpha();
  void extRejectsAMismatchedBufferBeforeCapture();
  void extRejectsANonNormalOrMissingFrameTransform();
  void extFailurePublishesNoPixels();
  void twoOutputsNeverOpenASession();
  void wlrPaintsAndFlipsOnlyProvenFlags();
  void wlrAcceptsAdvertisedPaddedRows();
  void extWinsWhenBothProtocolsAreAdvertised();
  void missingProtocolAndClosedFdFailClosed();
  void hungCompositorTimesOutWithoutPixels();
  void cancellationStopsAHungCompositor();
  void absentDisplayNameDoesNotCapture();
};

void CompositorFrameTest::cursorInclusionDefaultsOff() {
  const CompositorFrameRequest request;
  QVERIFY(!request.includeCursor);
  const ProtocolChoice choice = chooseCaptureProtocol(request.includeCursor, true, true);
  QCOMPARE(choice.kind, ProtocolChoice::Kind::ExtImageCopy);
  QCOMPARE(choice.extOptions, 0u);
  QCOMPARE(choice.wlrOverlayCursor, 0);
  QVERIFY(!choice.opensCursorSession);
}

void CompositorFrameTest::extPaintFlagIsPreferredAndNeverOpensACursorSession() {
  const ProtocolChoice on = chooseCaptureProtocol(true, true, true);
  QCOMPARE(on.kind, ProtocolChoice::Kind::ExtImageCopy);
  QCOMPARE(on.extOptions, 1u);
  QCOMPARE(on.wlrOverlayCursor, 0);
  QVERIFY(!on.opensCursorSession);
  const ProtocolChoice neither = chooseCaptureProtocol(true, false, false);
  QCOMPARE(neither.kind, ProtocolChoice::Kind::None);
  QVERIFY(!neither.opensCursorSession);
}

void CompositorFrameTest::wlrOverlayIsOnlyTheFallback() {
  const ProtocolChoice on = chooseCaptureProtocol(true, false, true);
  QCOMPARE(on.kind, ProtocolChoice::Kind::WlrScreencopy);
  QCOMPARE(on.wlrOverlayCursor, 1);
  QVERIFY(!on.opensCursorSession);
  QCOMPARE(chooseCaptureProtocol(false, false, true).wlrOverlayCursor, 0);
}

void CompositorFrameTest::singleUnrotatedOutputIsProvenFromItsMode() {
  const auto decision = proveSingleOutputLayout({provenOutput()});
  QCOMPARE(decision.kind, LayoutDecisionKind::Proven);
  QCOMPARE(decision.outputName, QStringLiteral("OUT-A"));
  QCOMPARE(decision.bufferWidth, 6);
  QCOMPARE(decision.bufferHeight, 4);
  QCOMPARE(decision.logicalX, -40);
  QCOMPARE(decision.logicalY, 12);
  QCOMPARE(decision.scale, 2);
  QCOMPARE(decision.transform, 0);
}

void CompositorFrameTest::extraRotatedNamelessAndIncompleteLayoutsAreRefused() {
  QVERIFY(proveSingleOutputLayout({}).kind == LayoutDecisionKind::Unproven);
  auto extra = provenOutput();
  auto second = provenOutput();
  second.name = QStringLiteral("OUT-B");
  QCOMPARE(proveSingleOutputLayout({extra, second}).diagnostic, kCompositorLayoutUnproven);
  auto rotated = provenOutput();
  rotated.transform = 1;
  QCOMPARE(proveSingleOutputLayout({rotated}).kind, LayoutDecisionKind::Unproven);
  auto unnamed = provenOutput();
  unnamed.sawName = false;
  unnamed.name.clear();
  QCOMPARE(proveSingleOutputLayout({unnamed}).kind, LayoutDecisionKind::Unproven);
  auto incomplete = provenOutput();
  incomplete.sawDone = false;
  QCOMPARE(proveSingleOutputLayout({incomplete}).kind, LayoutDecisionKind::Unproven);
  auto unscaled = provenOutput();
  unscaled.scale = 0;
  QCOMPARE(proveSingleOutputLayout({unscaled}).kind, LayoutDecisionKind::Unproven);
  auto conflict = provenOutput();
  conflict.conflictingMode = true;
  QCOMPARE(proveSingleOutputLayout({conflict}).kind, LayoutDecisionKind::Unproven);
  auto huge = provenOutput();
  huge.modeWidth = 100001;
  QCOMPARE(proveSingleOutputLayout({huge}).kind, LayoutDecisionKind::Unproven);
}

void CompositorFrameTest::extPaintsTheCursorIntoTheSameBuffer() {
  Scene observed;
  CompositorFrameRequest request;
  request.includeCursor = true;
  request.timeoutMs = 2000;
  const CompositorFrame frame = captureScene(Scene{}, request, &observed);
  QCOMPARE(frame.status, CompositorFrame::Status::Captured);
  QCOMPARE(frame.diagnostic, QString());
  QCOMPARE(frame.protocol, QStringLiteral("ext-image-copy-capture-v1"));
  QCOMPARE(frame.image.size(), QSize(6, 4));
  QCOMPARE(pixelAt(frame.image, 2, 1), 0xFFFF00FFu);
  QCOMPARE(pixelAt(frame.image, 0, 0), 0xFF070707u);
  QCOMPARE(pixelAt(frame.image, 1, 1), 0x80010203u);
  QCOMPARE(frame.image.convertToFormat(QImage::Format_ARGB32).pixel(1, 1), 0x80020406u);
  QCOMPARE(pixelAt(frame.image, 5, 3), 0xFF0A141Eu);
  QCOMPARE(frame.logicalX, -40);
  QCOMPARE(frame.logicalY, 12);
  QCOMPARE(frame.scale, 2);
  QCOMPARE(frame.transform, 0);
  QVERIFY(frame.cursorPaintRequested);
  QVERIFY(frame.cursorPaintedIntoFrame);
  QVERIFY(frame.singleOutputBufferOriginProven);
  QVERIFY(!frame.softwareCursorExclusionProven);
  QVERIFY(!frame.separateCursorMetadata);
  QCOMPARE(frame.presentationSec, 5ull);
  QCOMPARE(frame.presentationNsec, 250000000u);
  QCOMPARE(observed.extSessions, 1);
  QCOMPARE(observed.extFrames, 1);
  QCOMPARE(observed.lastExtOptions, 1);
  QCOMPARE(observed.extCursorSessions, 0);
  QCOMPARE(observed.wlrCaptures, 0);
  QCOMPARE(observed.wlrRegionCaptures, 0);
  QCOMPARE(observed.damageWidth, 6);
  QCOMPARE(observed.damageHeight, 4);
}

void CompositorFrameTest::extOffKeepsASoftwareCursorAndDoesNotInventOne() {
  Scene observed;
  const CompositorFrame frame = captureScene(Scene{}, CompositorFrameRequest{}, &observed);
  QCOMPARE(frame.status, CompositorFrame::Status::Captured);
  QVERIFY(!frame.cursorPaintRequested);
  QVERIFY(!frame.cursorPaintedIntoFrame);
  QVERIFY(!frame.softwareCursorExclusionProven);
  QCOMPARE(pixelAt(frame.image, 2, 1), 0xFF0A141Eu);
  QCOMPARE(pixelAt(frame.image, 0, 0), 0xFF070707u);
  QCOMPARE(observed.lastExtOptions, 0);
  QCOMPARE(observed.extCursorSessions, 0);
}

void CompositorFrameTest::extXrgbForcesOpaqueAlpha() {
  Scene scene;
  scene.xrgb = true;
  const CompositorFrame frame = captureScene(scene, {});
  QCOMPARE(frame.status, CompositorFrame::Status::Captured);
  QCOMPARE(pixelAt(frame.image, 1, 1), 0xFF050607u);
  QCOMPARE(pixelAt(frame.image, 0, 0), 0xFF070707u);
}

void CompositorFrameTest::extRejectsAMismatchedBufferBeforeCapture() {
  Scene scene;
  scene.bufferW = 7;
  Scene observed;
  const CompositorFrame frame = captureScene(scene, {}, &observed);
  QCOMPARE(frame.status, CompositorFrame::Status::SourceMismatch);
  QCOMPARE(frame.diagnostic, kCompositorSourceMismatch);
  QVERIFY(frame.image.isNull());
  QCOMPARE(observed.extSessions, 1);
  QCOMPARE(observed.extFrames, 0);
}

void CompositorFrameTest::extRejectsANonNormalOrMissingFrameTransform() {
  Scene rotated;
  rotated.frameTransform = 1;
  Scene observed;
  const CompositorFrame bad = captureScene(rotated, {}, &observed);
  QCOMPARE(bad.status, CompositorFrame::Status::SourceMismatch);
  QVERIFY(bad.image.isNull());
  QCOMPARE(observed.extFrames, 1);
  Scene missing;
  missing.sendTransform = false;
  const CompositorFrame absent = captureScene(missing, {}, &observed);
  QCOMPARE(absent.status, CompositorFrame::Status::SourceMismatch);
  QVERIFY(absent.image.isNull());
}

void CompositorFrameTest::extFailurePublishesNoPixels() {
  Scene scene;
  scene.failFrame = true;
  const CompositorFrame frame = captureScene(scene, {});
  QCOMPARE(frame.status, CompositorFrame::Status::Failed);
  QCOMPARE(frame.diagnostic, kCompositorCaptureFailed);
  QVERIFY(frame.image.isNull());
  QVERIFY(!frame.separateCursorMetadata);
}

void CompositorFrameTest::twoOutputsNeverOpenASession() {
  Scene scene;
  scene.outputs = 2;
  Scene observed;
  const CompositorFrame frame = captureScene(scene, {}, &observed);
  QCOMPARE(frame.status, CompositorFrame::Status::LayoutUnproven);
  QCOMPARE(frame.diagnostic, kCompositorLayoutUnproven);
  QVERIFY(frame.image.isNull());
  QCOMPARE(observed.boundOutputs, 2);
  QCOMPARE(observed.sourceCreates, 0);
  QCOMPARE(observed.extSessions, 0);
  QCOMPARE(observed.wlrCaptures, 0);
}

void CompositorFrameTest::wlrPaintsAndFlipsOnlyProvenFlags() {
  Scene painted;
  painted.advertiseExt = false;
  painted.advertiseWlr = true;
  CompositorFrameRequest request;
  request.includeCursor = true;
  request.timeoutMs = 2000;
  Scene observed;
  const CompositorFrame frame = captureScene(painted, request, &observed);
  QCOMPARE(frame.status, CompositorFrame::Status::Captured);
  QCOMPARE(frame.protocol, QStringLiteral("wlr-screencopy-unstable-v1"));
  QCOMPARE(pixelAt(frame.image, 2, 1), 0xFFFF00FFu);
  QVERIFY(frame.cursorPaintedIntoFrame);
  QVERIFY(!frame.softwareCursorExclusionProven);
  QCOMPARE(observed.wlrCaptures, 1);
  QCOMPARE(observed.wlrRegionCaptures, 0);
  QCOMPARE(observed.wlrCopies, 1);
  QCOMPARE(observed.wlrDamageCopies, 0);
  QCOMPARE(observed.lastOverlay, 1);
  QCOMPARE(observed.extSessions, 0);
  QCOMPARE(frame.presentationSec, 9ull);

  Scene inverted = painted;
  inverted.yBands = true;
  inverted.wlrFlags = 1;
  const CompositorFrame flipped = captureScene(inverted, {});
  QCOMPARE(flipped.status, CompositorFrame::Status::Captured);
  QCOMPARE(pixelAt(flipped.image, 0, 0), 0xFFFF0000u);
  QCOMPARE(pixelAt(flipped.image, 0, 3), 0xFF0000FFu);

  Scene unknown = painted;
  unknown.wlrFlags = 2;
  const CompositorFrame refused = captureScene(unknown, {});
  QCOMPARE(refused.status, CompositorFrame::Status::SourceMismatch);
  QVERIFY(refused.image.isNull());

  Scene silent = painted;
  silent.sendFlags = false;
  const CompositorFrame unflagged = captureScene(silent, {});
  QCOMPARE(unflagged.status, CompositorFrame::Status::SourceMismatch);
  QVERIFY(unflagged.image.isNull());
}

void CompositorFrameTest::wlrAcceptsAdvertisedPaddedRows() {
  Scene scene; scene.advertiseExt = false; scene.advertiseWlr = true; scene.stridePadding = 16;
  CompositorFrameRequest request; request.includeCursor = true;
  const auto frame = captureScene(scene, request);
  QCOMPARE(frame.status, CompositorFrame::Status::Captured);
  QCOMPARE(frame.image.size(), QSize(6, 4));
  QCOMPARE(pixelAt(frame.image, 2, 1), 0xFFFF00FFu);
  QCOMPARE(pixelAt(frame.image, 5, 3), 0xFF0A141Eu);
}

void CompositorFrameTest::extWinsWhenBothProtocolsAreAdvertised() {
  Scene scene;
  scene.advertiseWlr = true;
  Scene observed;
  CompositorFrameRequest request;
  request.includeCursor = true;
  const CompositorFrame frame = captureScene(scene, request, &observed);
  QCOMPARE(frame.protocol, QStringLiteral("ext-image-copy-capture-v1"));
  QCOMPARE(observed.extSessions, 1);
  QCOMPARE(observed.wlrCaptures, 0);
  QCOMPARE(observed.extCursorSessions, 0);
}

void CompositorFrameTest::missingProtocolAndClosedFdFailClosed() {
  Scene scene;
  scene.advertiseExt = false;
  const CompositorFrame unsupported = captureScene(scene, {});
  QCOMPARE(unsupported.status, CompositorFrame::Status::Unsupported);
  QCOMPARE(unsupported.diagnostic, kCompositorCaptureUnsupported);
  QVERIFY(unsupported.image.isNull());
  const CompositorFrame closed = CompositorFrameCapture::captureOnFd(-1, {});
  QCOMPARE(closed.status, CompositorFrame::Status::Failed);
  QCOMPARE(closed.diagnostic, kCompositorCaptureFailed);
  QVERIFY(closed.image.isNull());
}

void CompositorFrameTest::hungCompositorTimesOutWithoutPixels() {
  Scene scene;
  scene.hang = true;
  CompositorFrameRequest request;
  request.timeoutMs = 250;
  Scene observed;
  const CompositorFrame frame = captureScene(scene, request, &observed);
  QCOMPARE(frame.status, CompositorFrame::Status::TimedOut);
  QCOMPARE(frame.diagnostic, kCompositorCaptureTimedOut);
  QVERIFY(frame.image.isNull());
  QCOMPARE(observed.extFrames, 1);
  QCOMPARE(observed.extCursorSessions, 0);
}

void CompositorFrameTest::cancellationStopsAHungCompositor() {
  Scene scene; scene.hang = true;
  QElapsedTimer elapsed; elapsed.start();
  CompositorFrameRequest request; request.timeoutMs = 5000;
  request.cancelled = [&elapsed] { return elapsed.elapsed() >= 75; };
  const auto frame = captureScene(scene, request);
  QCOMPARE(frame.status, CompositorFrame::Status::Cancelled);
  QVERIFY(frame.image.isNull()); QVERIFY(frame.diagnostic.isEmpty());
  QVERIFY(elapsed.elapsed() < 1000);
  request.cancelled = [] { return true; };
  QCOMPARE(CompositorFrameCapture::captureOnFd(-1, request).status, CompositorFrame::Status::Cancelled);
}

void CompositorFrameTest::absentDisplayNameDoesNotCapture() {
  const QByteArray display = qgetenv("WAYLAND_DISPLAY");
  const QByteArray socket = qgetenv("WAYLAND_SOCKET");
  qunsetenv("WAYLAND_SOCKET");
  qputenv("WAYLAND_DISPLAY", "xerahs-compositor-frame-absent");
  CompositorFrameRequest request;
  request.timeoutMs = 200;
  const CompositorFrame frame = CompositorFrameCapture::capture(request);
  if (socket.isNull()) qunsetenv("WAYLAND_SOCKET");
  else qputenv("WAYLAND_SOCKET", socket);
  if (display.isNull()) qunsetenv("WAYLAND_DISPLAY");
  else qputenv("WAYLAND_DISPLAY", display);
  QCOMPARE(frame.status, CompositorFrame::Status::Failed);
  QCOMPARE(frame.diagnostic, kCompositorCaptureFailed);
  QVERIFY(frame.image.isNull());
  QVERIFY(!frame.cursorPaintedIntoFrame);
}

QTEST_GUILESS_MAIN(CompositorFrameTest)
#include "CompositorFrameTest.moc"
