// Opt-in compositor-native frozen frame for one Wayland output.
// Cursor inclusion defaults off. A requested cursor is whatever the compositor
// paints into this same buffer. This client never reads QCursor, never opens a
// cursor-capture session, and never treats a cleared paint flag as proof that
// a software cursor was removed from the output texture.
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>

namespace xerahs::app {

inline const QString kCompositorCaptureUnsupported = QStringLiteral("compositor-capture-unsupported");
inline const QString kCompositorLayoutUnproven = QStringLiteral("compositor-layout-unproven");
inline const QString kCompositorSourceMismatch = QStringLiteral("compositor-source-mismatch");
inline const QString kCompositorCaptureTimedOut = QStringLiteral("compositor-capture-timed-out");
inline const QString kCompositorCaptureFailed = QStringLiteral("capture-source-failed");

// Facts learned from one wl_output. Pixel size is the current mode, not the
// logical position. logicalX/logicalY are compositor global coordinates and
// are not a desktop physical origin.
struct CompositorOutputFacts {
  bool sawGeometry = false;
  bool sawCurrentMode = false;
  bool sawScale = false;
  bool sawDone = false;
  bool sawName = false;
  bool conflictingMode = false;
  QString name;
  qint32 logicalX = 0;
  qint32 logicalY = 0;
  qint32 modeWidth = 0;
  qint32 modeHeight = 0;
  qint32 scale = 0;
  qint32 transform = 0;
};

enum class LayoutDecisionKind { Proven, Unproven };

struct LayoutDecision {
  LayoutDecisionKind kind = LayoutDecisionKind::Unproven;
  QString diagnostic;
  QString outputName;
  qint32 logicalX = 0;
  qint32 logicalY = 0;
  qint32 bufferWidth = 0;
  qint32 bufferHeight = 0;
  qint32 scale = 0;
  qint32 transform = 0;
};

// One named, done, unrotated output with a positive integer scale and a
// positive current mode. Zero outputs, extra outputs, rotation, a missing
// identity, or an incomplete burst are unproven. This does not place that
// output inside a multi-monitor desktop.
LayoutDecision proveSingleOutputLayout(const QVector<CompositorOutputFacts> &outputs);

struct ProtocolChoice {
  enum class Kind { None, ExtImageCopy, WlrScreencopy };
  Kind kind = Kind::None;
  // ext_image_copy_capture_manager_v1.options. paint_cursors is 1. No other bit is set.
  quint32 extOptions = 0;
  // zwlr_screencopy_manager_v1.capture_output overlay_cursor. 0 or 1.
  qint32 wlrOverlayCursor = 0;
  // A separate cursor session is a different frame and is never requested.
  bool opensCursorSession = false;
};

// ext-image-copy-capture wins when both protocols exist because paint_cursors
// is the inclusion bit. wlr overlay_cursor is only a fallback request to
// composite onto the frame. Neither choice proves a software cursor is absent
// when the flag is off.
ProtocolChoice chooseCaptureProtocol(bool includeCursor, bool extImageCopyAvailable, bool wlrScreencopyAvailable);

struct CompositorFrameRequest {
  // Region capture's cursor inclusion default. Off until a caller opts in.
  bool includeCursor = false;
  int timeoutMs = 4000;
  std::function<bool()> cancelled;
};

struct CompositorFrame {
  enum class Status {
    Captured,
    Unsupported,
    LayoutUnproven,
    SourceMismatch,
    Failed,
    TimedOut,
    Cancelled
  };

  Status status = Status::Failed;
  QString diagnostic;
  QImage image;
  QString outputName;
  QString protocol;
  qint32 logicalX = 0;
  qint32 logicalY = 0;
  qint32 bufferWidth = 0;
  qint32 bufferHeight = 0;
  qint32 scale = 0;
  qint32 transform = -1;
  bool cursorPaintRequested = false;
  // True only when the successful request carried paint_cursors or overlay_cursor.
  // It is not a glyph detection. The pixels are the compositor's buffer.
  bool cursorPaintedIntoFrame = false;
  // Always false. An unset paint flag does not prove a software cursor was excluded.
  bool softwareCursorExclusionProven = false;
  // Always false. No separate cursor image, hotspot, or position is returned.
  bool separateCursorMetadata = false;
  // Image (0,0) is this output's buffer origin. It is not a virtual-desktop origin.
  bool singleOutputBufferOriginProven = false;
  quint64 presentationSec = 0;
  quint32 presentationNsec = 0;
};

class CompositorFrameCapture {
public:
  // Connects to WAYLAND_DISPLAY. Does not open a portal dialog.
  static CompositorFrame capture(const CompositorFrameRequest &request = {});
  // Dup's fd and owns the duplicate. The caller's descriptor stays open.
  static CompositorFrame captureOnFd(int fd, const CompositorFrameRequest &request = {});
};

}  // namespace xerahs::app
