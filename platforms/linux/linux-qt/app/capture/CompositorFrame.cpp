#include "capture/CompositorFrame.h"

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <wayland-client.h>

#include <QVector>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <errno.h>
#include <functional>
#include <memory>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace xerahs::app {
namespace {

constexpr quint64 kMaximumDimension = 100000;
constexpr quint64 kMaximumPixels = 268435456;
constexpr quint32 kWlrYInvert = 1;

CompositorFrame refused(CompositorFrame::Status status, const QString &diagnostic) {
  CompositorFrame frame;
  frame.status = status;
  frame.diagnostic = diagnostic;
  return frame;
}

bool bufferExtentAllowed(quint32 width, quint32 height) {
  if (width == 0 || height == 0 || width > kMaximumDimension || height > kMaximumDimension) return false;
  return quint64(width) * quint64(height) <= kMaximumPixels && quint64(width) * 4u * quint64(height) <= SIZE_MAX;
}

enum class PumpResult { Ready, TimedOut, Failed, Cancelled };

class Deadline {
public:
  explicit Deadline(const CompositorFrameRequest &request) : m_end(std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(request.timeoutMs, 0))), m_cancelled(request.cancelled) {}
  bool cancelled() const { return m_cancelled && m_cancelled(); }
  int remaining() const {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(m_end - std::chrono::steady_clock::now()).count();
    return left <= 0 ? 0 : int(left);
  }

private:
  std::chrono::steady_clock::time_point m_end;
  std::function<bool()> m_cancelled;
};

PumpResult pump(wl_display *display, Deadline &deadline, const std::function<bool()> &ready) {
  if (deadline.cancelled()) return PumpResult::Cancelled;
  while (!ready()) {
    if (deadline.cancelled()) return PumpResult::Cancelled;
    while (wl_display_prepare_read(display) != 0) {
      if (deadline.cancelled()) return PumpResult::Cancelled;
      if (deadline.remaining() <= 0) return PumpResult::TimedOut;
      if (wl_display_dispatch_pending(display) < 0) return PumpResult::Failed;
      if (ready()) return PumpResult::Ready;
    }
    if (ready()) {
      wl_display_cancel_read(display);
      return PumpResult::Ready;
    }
    const int remaining = deadline.remaining();
    if (remaining <= 0) {
      wl_display_cancel_read(display);
      return PumpResult::TimedOut;
    }
    const int flushed = wl_display_flush(display);
    if (flushed < 0 && errno == EAGAIN) {
      wl_display_cancel_read(display);
      pollfd writable{wl_display_get_fd(display), POLLOUT, 0};
      const int wrote = poll(&writable, 1, std::min(remaining, 50));
      if (wrote < 0 && errno == EINTR) continue;
      if (wrote < 0) return PumpResult::Failed;
      if (wrote == 0 && deadline.remaining() <= 0) return PumpResult::TimedOut;
      continue;
    }
    if (flushed < 0) {
      wl_display_cancel_read(display);
      return PumpResult::Failed;
    }
    pollfd readable{wl_display_get_fd(display), POLLIN, 0};
    const int polled = poll(&readable, 1, std::min(remaining, 50));
    if (polled < 0 && errno == EINTR) {
      wl_display_cancel_read(display);
      continue;
    }
    if (polled <= 0) {
      wl_display_cancel_read(display);
      if (polled < 0) return PumpResult::Failed;
      if (deadline.remaining() > 0) continue;
      return PumpResult::TimedOut;
    }
    if (wl_display_read_events(display) < 0) return PumpResult::Failed;
    if (wl_display_dispatch_pending(display) < 0) return PumpResult::Failed;
  }
  return PumpResult::Ready;
}

CompositorFrame fromPump(PumpResult result) {
  if (result == PumpResult::Cancelled) return refused(CompositorFrame::Status::Cancelled, {});
  if (result == PumpResult::TimedOut) return refused(CompositorFrame::Status::TimedOut, kCompositorCaptureTimedOut);
  return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
}

struct MappedBuffer {
  void *data = nullptr;
  size_t size = 0;
  wl_shm_pool *pool = nullptr;
  wl_buffer *buffer = nullptr;
  ~MappedBuffer() {
    if (buffer) wl_buffer_destroy(buffer);
    if (pool) wl_shm_pool_destroy(pool);
    if (data && data != MAP_FAILED) munmap(data, size);
  }
};

void bufferRelease(void *, wl_buffer *) {}
const wl_buffer_listener kBufferListener{bufferRelease};

std::unique_ptr<MappedBuffer> allocateBuffer(wl_shm *shm, int32_t width, int32_t height, uint32_t format, uint32_t requestedStride = 0) {
  if (!shm || !bufferExtentAllowed(uint32_t(width), uint32_t(height))) return {};
  const quint64 rowBytes = requestedStride ? requestedStride : quint64(width) * 4u;
  const quint64 totalBytes = rowBytes * quint64(height);
  if (rowBytes < quint64(width) * 4u || rowBytes > INT32_MAX || totalBytes > kMaximumPixels * 4u) return {};
  const int32_t stride = int32_t(rowBytes);
  const size_t bytes = size_t(totalBytes);
  const int fd = memfd_create("xerahs-compositor-frame", MFD_CLOEXEC);
  if (fd < 0) return {};
  if (ftruncate(fd, off_t(bytes)) != 0) {
    close(fd);
    return {};
  }
  void *mapped = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    close(fd);
    return {};
  }
  auto buffer = std::make_unique<MappedBuffer>();
  buffer->data = mapped;
  buffer->size = bytes;
  buffer->pool = wl_shm_create_pool(shm, fd, int32_t(bytes));
  close(fd);
  if (!buffer->pool) return {};
  buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0, width, height, stride, format);
  if (!buffer->buffer) return {};
  wl_buffer_add_listener(buffer->buffer, &kBufferListener, nullptr);
  return buffer;
}

QImage imageFromShm(const uint8_t *bytes, int width, int height, int stride, uint32_t format, bool yInvert,
                    const std::function<bool()> &cancelled) {
  if (!bytes || stride < width * 4 || !bufferExtentAllowed(uint32_t(width), uint32_t(height))) return {};
  if (cancelled && cancelled()) return {};
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) return {};
  const bool forceOpaque = format == WL_SHM_FORMAT_XRGB8888;
  for (int y = 0; y < height; ++y) {
    if (cancelled && cancelled()) return {};
    const int sourceY = yInvert ? height - 1 - y : y;
    const uint8_t *row = bytes + size_t(sourceY) * size_t(stride);
    auto *destination = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const uint8_t *pixel = row + size_t(x) * 4u;
      uint32_t word = uint32_t(pixel[0]) | (uint32_t(pixel[1]) << 8) | (uint32_t(pixel[2]) << 16) | (uint32_t(pixel[3]) << 24);
      if (forceOpaque) word |= 0xFF000000u;
      destination[x] = word;
    }
  }
  return image;
}

struct OutputRecord {
  CompositorOutputFacts facts;
  wl_output *proxy = nullptr;
  bool removed = false;
  ~OutputRecord() {
    if (proxy) wl_output_destroy(proxy);
  }
};

struct Client {
  wl_shm *shm = nullptr;
  ext_output_image_capture_source_manager_v1 *sources = nullptr;
  ext_image_copy_capture_manager_v1 *extCopy = nullptr;
  zwlr_screencopy_manager_v1 *wlrCopy = nullptr;
  uint32_t wlrVersion = 0;
  std::deque<std::unique_ptr<OutputRecord>> outputs;
  bool syncDone = false;

  bool extConstraintsDone = false;
  bool extStopped = false;
  uint32_t extBufferWidth = 0;
  uint32_t extBufferHeight = 0;
  bool extSawSize = false;
  bool extArgb = false;
  bool extXrgb = false;

  bool frameReady = false;
  bool frameFailed = false;
  bool frameSawTransform = false;
  uint32_t frameTransform = 0;
  bool frameSawPresentation = false;
  uint32_t presentationHi = 0;
  uint32_t presentationLo = 0;
  uint32_t presentationNsec = 0;

  bool wlrSawBuffer = false;
  bool wlrBufferDone = false;
  bool wlrSawFlags = false;
  bool wlrFailed = false;
  bool wlrReady = false;
  uint32_t wlrFormat = 0;
  uint32_t wlrWidth = 0;
  uint32_t wlrHeight = 0;
  uint32_t wlrStride = 0;
  uint32_t wlrFlags = 0;

  ~Client() {
    outputs.clear();
    if (wlrCopy) zwlr_screencopy_manager_v1_destroy(wlrCopy);
    if (extCopy) ext_image_copy_capture_manager_v1_destroy(extCopy);
    if (sources) ext_output_image_capture_source_manager_v1_destroy(sources);
    if (shm) wl_shm_destroy(shm);
  }
};

void outputGeometry(void *data, wl_output *, int32_t x, int32_t y, int32_t, int32_t, int32_t, const char *, const char *, int32_t transform) {
  auto *output = static_cast<OutputRecord *>(data);
  output->facts.sawGeometry = true;
  output->facts.logicalX = x;
  output->facts.logicalY = y;
  output->facts.transform = transform;
}

void outputMode(void *data, wl_output *, uint32_t flags, int32_t width, int32_t height, int32_t) {
  if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) return;
  auto *output = static_cast<OutputRecord *>(data);
  if (output->facts.sawCurrentMode && (output->facts.modeWidth != width || output->facts.modeHeight != height)) output->facts.conflictingMode = true;
  output->facts.sawCurrentMode = true;
  output->facts.modeWidth = width;
  output->facts.modeHeight = height;
}

void outputDone(void *data, wl_output *) { static_cast<OutputRecord *>(data)->facts.sawDone = true; }

void outputScale(void *data, wl_output *, int32_t factor) {
  auto *output = static_cast<OutputRecord *>(data);
  output->facts.sawScale = true;
  output->facts.scale = factor;
}

void outputName(void *data, wl_output *, const char *name) {
  auto *output = static_cast<OutputRecord *>(data);
  const QString value = QString::fromUtf8(name ? name : "");
  if (output->facts.sawName && output->facts.name != value) output->facts.conflictingMode = true;
  output->facts.sawName = true;
  output->facts.name = value;
}

void outputDescription(void *, wl_output *, const char *) {}

const wl_output_listener kOutputListener{outputGeometry, outputMode, outputDone, outputScale, outputName, outputDescription};

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
  auto *client = static_cast<Client *>(data);
  if (std::strcmp(interface, wl_shm_interface.name) == 0) {
    client->shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
    const uint32_t bound = std::min(version, 4u);
    auto record = std::make_unique<OutputRecord>();
    record->proxy = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface, bound));
    wl_output_add_listener(record->proxy, &kOutputListener, record.get());
    client->outputs.push_back(std::move(record));
  } else if (std::strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
    client->sources = static_cast<ext_output_image_capture_source_manager_v1 *>(
        wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1));
  } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
    client->extCopy = static_cast<ext_image_copy_capture_manager_v1 *>(
        wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
  } else if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
    client->wlrVersion = std::min(version, 3u);
    client->wlrCopy = static_cast<zwlr_screencopy_manager_v1 *>(
        wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, client->wlrVersion));
  }
}

void registryRemove(void *data, wl_registry *, uint32_t) {
  auto *client = static_cast<Client *>(data);
  // Identity of the removed name is not recoverable from this callback's arguments
  // once the proxy is bound, so any removal makes the sampled layout stale.
  for (auto &output : client->outputs) output->removed = true;
}

const wl_registry_listener kRegistryListener{registryGlobal, registryRemove};

void syncDone(void *data, wl_callback *callback, uint32_t) {
  static_cast<Client *>(data)->syncDone = true;
  wl_callback_destroy(callback);
}

const wl_callback_listener kSyncListener{syncDone};

PumpResult roundTrip(Client &client, wl_display *display, Deadline &deadline) {
  client.syncDone = false;
  wl_callback *callback = wl_display_sync(display);
  if (!callback) return PumpResult::Failed;
  wl_callback_add_listener(callback, &kSyncListener, &client);
  return pump(display, deadline, [&client] { return client.syncDone; });
}

void extBufferSize(void *data, ext_image_copy_capture_session_v1 *, uint32_t width, uint32_t height) {
  auto *client = static_cast<Client *>(data);
  client->extSawSize = true;
  client->extBufferWidth = width;
  client->extBufferHeight = height;
}

void extShmFormat(void *data, ext_image_copy_capture_session_v1 *, uint32_t format) {
  auto *client = static_cast<Client *>(data);
  if (format == WL_SHM_FORMAT_ARGB8888) client->extArgb = true;
  if (format == WL_SHM_FORMAT_XRGB8888) client->extXrgb = true;
}

void extDmabufDevice(void *, ext_image_copy_capture_session_v1 *, wl_array *) {}
void extDmabufFormat(void *, ext_image_copy_capture_session_v1 *, uint32_t, wl_array *) {}
void extConstraintsDone(void *data, ext_image_copy_capture_session_v1 *) { static_cast<Client *>(data)->extConstraintsDone = true; }
void extStopped(void *data, ext_image_copy_capture_session_v1 *) { static_cast<Client *>(data)->extStopped = true; }

const ext_image_copy_capture_session_v1_listener kExtSessionListener{
    extBufferSize, extShmFormat, extDmabufDevice, extDmabufFormat, extConstraintsDone, extStopped};

void frameTransform(void *data, ext_image_copy_capture_frame_v1 *, uint32_t transform) {
  auto *client = static_cast<Client *>(data);
  client->frameSawTransform = true;
  client->frameTransform = transform;
}

void frameDamage(void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t, int32_t, int32_t) {}

void framePresentation(void *data, ext_image_copy_capture_frame_v1 *, uint32_t hi, uint32_t lo, uint32_t nsec) {
  auto *client = static_cast<Client *>(data);
  client->frameSawPresentation = true;
  client->presentationHi = hi;
  client->presentationLo = lo;
  client->presentationNsec = nsec;
}

void frameReady(void *data, ext_image_copy_capture_frame_v1 *) { static_cast<Client *>(data)->frameReady = true; }
void frameFailed(void *data, ext_image_copy_capture_frame_v1 *, uint32_t) { static_cast<Client *>(data)->frameFailed = true; }

const ext_image_copy_capture_frame_v1_listener kExtFrameListener{frameTransform, frameDamage, framePresentation, frameReady, frameFailed};

void wlrBuffer(void *data, zwlr_screencopy_frame_v1 *, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
  auto *client = static_cast<Client *>(data);
  client->wlrSawBuffer = true;
  client->wlrFormat = format;
  client->wlrWidth = width;
  client->wlrHeight = height;
  client->wlrStride = stride;
}

void wlrFlags(void *data, zwlr_screencopy_frame_v1 *, uint32_t flags) {
  auto *client = static_cast<Client *>(data);
  client->wlrSawFlags = true;
  client->wlrFlags = flags;
}

void wlrReady(void *data, zwlr_screencopy_frame_v1 *, uint32_t hi, uint32_t lo, uint32_t nsec) {
  auto *client = static_cast<Client *>(data);
  client->wlrReady = true;
  client->frameSawPresentation = true;
  client->presentationHi = hi;
  client->presentationLo = lo;
  client->presentationNsec = nsec;
}

void wlrFailed(void *data, zwlr_screencopy_frame_v1 *) { static_cast<Client *>(data)->wlrFailed = true; }
void wlrDamage(void *, zwlr_screencopy_frame_v1 *, uint32_t, uint32_t, uint32_t, uint32_t) {}
void wlrDmabuf(void *, zwlr_screencopy_frame_v1 *, uint32_t, uint32_t, uint32_t) {}
void wlrBufferDone(void *data, zwlr_screencopy_frame_v1 *) { static_cast<Client *>(data)->wlrBufferDone = true; }

const zwlr_screencopy_frame_v1_listener kWlrFrameListener{wlrBuffer, wlrFlags, wlrReady, wlrFailed, wlrDamage, wlrDmabuf, wlrBufferDone};

void stampPresentation(CompositorFrame &frame, const Client &client) {
  if (!client.frameSawPresentation || client.presentationNsec > 999999999u) return;
  frame.presentationSec = (quint64(client.presentationHi) << 32) | quint64(client.presentationLo);
  frame.presentationNsec = client.presentationNsec;
}

CompositorFrame finishImage(const CompositorFrameRequest &request, const ProtocolChoice &choice, const LayoutDecision &layout,
                            const Client &client, QImage image) {
  if (request.cancelled && request.cancelled()) return refused(CompositorFrame::Status::Cancelled, {});
  if (image.isNull() || image.width() != layout.bufferWidth || image.height() != layout.bufferHeight) {
    return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  }
  CompositorFrame frame;
  frame.status = CompositorFrame::Status::Captured;
  frame.image = std::move(image);
  frame.outputName = layout.outputName;
  frame.protocol = choice.kind == ProtocolChoice::Kind::ExtImageCopy ? QStringLiteral("ext-image-copy-capture-v1")
                                                                     : QStringLiteral("wlr-screencopy-unstable-v1");
  frame.logicalX = layout.logicalX;
  frame.logicalY = layout.logicalY;
  frame.bufferWidth = layout.bufferWidth;
  frame.bufferHeight = layout.bufferHeight;
  frame.scale = layout.scale;
  frame.transform = layout.transform;
  frame.cursorPaintRequested = request.includeCursor;
  frame.cursorPaintedIntoFrame = request.includeCursor;
  frame.singleOutputBufferOriginProven = true;
  stampPresentation(frame, client);
  return frame;
}

CompositorFrame captureExt(Client &client, wl_display *display, Deadline &deadline, const CompositorFrameRequest &request,
                           const ProtocolChoice &choice, const LayoutDecision &layout, wl_output *output) {
  ext_image_capture_source_v1 *source = ext_output_image_capture_source_manager_v1_create_source(client.sources, output);
  ext_image_copy_capture_session_v1 *session = ext_image_copy_capture_manager_v1_create_session(client.extCopy, source, choice.extOptions);
  if (!source || !session) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  ext_image_copy_capture_session_v1_add_listener(session, &kExtSessionListener, &client);
  const PumpResult constraints = pump(display, deadline, [&client] { return client.extConstraintsDone || client.extStopped; });
  if (constraints != PumpResult::Ready) return fromPump(constraints);
  if (client.extStopped) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  if (!client.extSawSize || int32_t(client.extBufferWidth) != layout.bufferWidth || int32_t(client.extBufferHeight) != layout.bufferHeight) {
    return refused(CompositorFrame::Status::SourceMismatch, kCompositorSourceMismatch);
  }
  // WL_SHM_FORMAT_ARGB8888 is 0. A missing format is the absence of both flags.
  if (!client.extArgb && !client.extXrgb) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  const uint32_t format = client.extArgb ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888;
  auto shm = allocateBuffer(client.shm, layout.bufferWidth, layout.bufferHeight, format);
  if (!shm) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  ext_image_copy_capture_frame_v1 *frameObject = ext_image_copy_capture_session_v1_create_frame(session);
  if (!frameObject) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  ext_image_copy_capture_frame_v1_add_listener(frameObject, &kExtFrameListener, &client);
  ext_image_copy_capture_frame_v1_attach_buffer(frameObject, shm->buffer);
  ext_image_copy_capture_frame_v1_damage_buffer(frameObject, 0, 0, layout.bufferWidth, layout.bufferHeight);
  ext_image_copy_capture_frame_v1_capture(frameObject);
  const PumpResult copied = pump(display, deadline, [&client] { return client.frameReady || client.frameFailed || client.extStopped; });
  if (copied != PumpResult::Ready) return fromPump(copied);
  if (!client.frameReady || client.frameFailed || client.extStopped) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  // The buffer transform is part of the same frame. A missing or non-normal
  // transform means the buffer origin is not the desktop top-left.
  if (!client.frameSawTransform || client.frameTransform != WL_OUTPUT_TRANSFORM_NORMAL) {
    return refused(CompositorFrame::Status::SourceMismatch, kCompositorSourceMismatch);
  }
  QImage image = imageFromShm(static_cast<const uint8_t *>(shm->data), layout.bufferWidth, layout.bufferHeight,
                              layout.bufferWidth * 4, format, false, request.cancelled);
  return finishImage(request, choice, layout, client, std::move(image));
}

CompositorFrame captureWlr(Client &client, wl_display *display, Deadline &deadline, const CompositorFrameRequest &request,
                           const ProtocolChoice &choice, const LayoutDecision &layout, wl_output *output) {
  // capture_output_region is logical xdg-output space, so it is not used.
  // copy_with_damage waits for a later change, so the frozen request is copy.
  zwlr_screencopy_frame_v1 *frameObject = zwlr_screencopy_manager_v1_capture_output(client.wlrCopy, choice.wlrOverlayCursor, output);
  if (!frameObject) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  zwlr_screencopy_frame_v1_add_listener(frameObject, &kWlrFrameListener, &client);
  const bool version3 = client.wlrVersion >= 3;
  const PumpResult described = pump(display, deadline, [&client, version3] {
    return client.wlrFailed || (version3 ? client.wlrBufferDone : client.wlrSawBuffer);
  });
  if (described != PumpResult::Ready) return fromPump(described);
  if (client.wlrFailed) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  if (!client.wlrSawBuffer || int32_t(client.wlrWidth) != layout.bufferWidth || int32_t(client.wlrHeight) != layout.bufferHeight ||
      client.wlrStride < client.wlrWidth * 4u) {
    return refused(CompositorFrame::Status::SourceMismatch, kCompositorSourceMismatch);
  }
  if (client.wlrFormat != WL_SHM_FORMAT_ARGB8888 && client.wlrFormat != WL_SHM_FORMAT_XRGB8888) {
    return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  }
  auto shm = allocateBuffer(client.shm, layout.bufferWidth, layout.bufferHeight, client.wlrFormat, client.wlrStride);
  if (!shm) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  zwlr_screencopy_frame_v1_copy(frameObject, shm->buffer);
  const PumpResult copied = pump(display, deadline, [&client] { return client.wlrReady || client.wlrFailed; });
  if (copied != PumpResult::Ready) return fromPump(copied);
  if (!client.wlrReady || client.wlrFailed) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  if (!client.wlrSawFlags || (client.wlrFlags & ~kWlrYInvert) != 0) {
    return refused(CompositorFrame::Status::SourceMismatch, kCompositorSourceMismatch);
  }
  QImage image = imageFromShm(static_cast<const uint8_t *>(shm->data), layout.bufferWidth, layout.bufferHeight, int(client.wlrStride),
                              client.wlrFormat, (client.wlrFlags & kWlrYInvert) != 0, request.cancelled);
  return finishImage(request, choice, layout, client, std::move(image));
}

CompositorFrame captureDisplay(wl_display *display, const CompositorFrameRequest &request) {
  struct DisplayClose {
    wl_display *display = nullptr;
    ~DisplayClose() {
      if (display) wl_display_disconnect(display);
    }
  } guard{display};
  if (!display) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  Deadline deadline(request);
  Client client;
  wl_registry *registry = wl_display_get_registry(display);
  if (!registry) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  wl_registry_add_listener(registry, &kRegistryListener, &client);
  for (int trip = 0; trip < 2; ++trip) {
    const PumpResult synced = roundTrip(client, display, deadline);
    if (synced != PumpResult::Ready) return fromPump(synced);
  }
  QVector<CompositorOutputFacts> facts;
  OutputRecord *chosen = nullptr;
  for (const auto &output : client.outputs) {
    if (output->removed) continue;
    facts.push_back(output->facts);
    chosen = output.get();
  }
  const LayoutDecision layout = proveSingleOutputLayout(facts);
  if (layout.kind != LayoutDecisionKind::Proven || !chosen || !chosen->proxy) {
    return refused(CompositorFrame::Status::LayoutUnproven, kCompositorLayoutUnproven);
  }
  const bool extAvailable = client.extCopy && client.sources;
  const ProtocolChoice choice = chooseCaptureProtocol(request.includeCursor, extAvailable, client.wlrCopy != nullptr);
  if (choice.kind == ProtocolChoice::Kind::None) return refused(CompositorFrame::Status::Unsupported, kCompositorCaptureUnsupported);
  if (!client.shm) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  if (choice.opensCursorSession) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  if (choice.kind == ProtocolChoice::Kind::ExtImageCopy) return captureExt(client, display, deadline, request, choice, layout, chosen->proxy);
  return captureWlr(client, display, deadline, request, choice, layout, chosen->proxy);
}

}  // namespace

LayoutDecision proveSingleOutputLayout(const QVector<CompositorOutputFacts> &outputs) {
  LayoutDecision rejected;
  rejected.diagnostic = kCompositorLayoutUnproven;
  if (outputs.size() != 1) return rejected;
  const CompositorOutputFacts &output = outputs.front();
  if (!output.sawGeometry || !output.sawCurrentMode || !output.sawScale || !output.sawDone || !output.sawName) return rejected;
  if (output.conflictingMode || output.name.isEmpty() || output.transform != 0 || output.scale < 1) return rejected;
  if (output.modeWidth <= 0 || output.modeHeight <= 0) return rejected;
  if (!bufferExtentAllowed(uint32_t(output.modeWidth), uint32_t(output.modeHeight))) return rejected;
  LayoutDecision proven;
  proven.kind = LayoutDecisionKind::Proven;
  proven.outputName = output.name;
  proven.logicalX = output.logicalX;
  proven.logicalY = output.logicalY;
  proven.bufferWidth = output.modeWidth;
  proven.bufferHeight = output.modeHeight;
  proven.scale = output.scale;
  proven.transform = output.transform;
  return proven;
}

ProtocolChoice chooseCaptureProtocol(bool includeCursor, bool extImageCopyAvailable, bool wlrScreencopyAvailable) {
  ProtocolChoice choice;
  choice.opensCursorSession = false;
  if (extImageCopyAvailable) {
    choice.kind = ProtocolChoice::Kind::ExtImageCopy;
    choice.extOptions = includeCursor ? quint32(EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS) : 0u;
    return choice;
  }
  if (wlrScreencopyAvailable) {
    choice.kind = ProtocolChoice::Kind::WlrScreencopy;
    choice.wlrOverlayCursor = includeCursor ? 1 : 0;
  }
  return choice;
}

CompositorFrame CompositorFrameCapture::capture(const CompositorFrameRequest &request) {
  if (request.cancelled && request.cancelled()) return refused(CompositorFrame::Status::Cancelled, {});
  return captureDisplay(wl_display_connect(nullptr), request);
}

CompositorFrame CompositorFrameCapture::captureOnFd(int fd, const CompositorFrameRequest &request) {
  if (request.cancelled && request.cancelled()) return refused(CompositorFrame::Status::Cancelled, {});
  const int owned = dup(fd);
  if (owned < 0) return refused(CompositorFrame::Status::Failed, kCompositorCaptureFailed);
  return captureDisplay(wl_display_connect_to_fd(owned), request);
}

}  // namespace xerahs::app
