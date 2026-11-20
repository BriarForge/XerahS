// Qt is the approved pre-acquisition topology authority (2026-11-04);
// RC-TOPOLOGY-AUTHORITY-2026-11-20 decides what that authority means: the
// logical space is QScreen/xdg_output geometry, per-display physical bounds
// are each display's own-scale extents, and the packed global arrangement is
// a documented degraded disposition endorsed only by compositor-proven grid
// tilings (see product-contract/decisions/RC-topology-authority.md).
#pragma once
#include "capture/RegionGeometry.h"
#include "capture/ScreenTopology.h"
#include <QImage>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <vector>
namespace xerahs::app {
struct CaptureDisplay {
  QPointer<QScreen> screen;
  xerahs::capture::Display mapping;
  QString outputName;
};
using CaptureDisplays = std::vector<CaptureDisplay>;
QString screenIdentity(const QString &name, const QString &manufacturer, const QString &model, const QString &serial);
CaptureDisplays screenTopology();
xerahs::capture::Topology displayMappings(const CaptureDisplays &screens);

// One compositor-native frame for one named output, as captured per
// connection (each capture proves the whole live layout itself).
struct CapturedDisplayFrame {
  QString outputName;
  QImage image;
  qint32 logicalX = 0;
  qint32 logicalY = 0;
  qint32 bufferWidth = 0;
  qint32 bufferHeight = 0;
  qint32 scale = 0;
  qint32 transform = -1;
  bool cursorPaintRequested = false;
  bool cursorPaintedIntoFrame = false;
  bool singleOutputBufferOriginProven = false;
};

// RC-012/CORE-014 on the decided authority: places the captured frames at
// the session's packed physical origins after cross-checking every frame
// against the frozen display it names - compositor name, logical position,
// scale and mode-verified buffer bounds must all agree, and every
// capturable display must be covered exactly once. The result is sized to
// the session's bounding box; a mismatch refuses whole with
// compositor-source-mismatch rather than compositing misaligned pixels.
struct ComposedDesktop { bool ok = false; QImage image; QString diagnostic; };
ComposedDesktop composeCapturedFrames(const CaptureDisplays &displays, const std::vector<CapturedDisplayFrame> &frames);
}
