// REGION-CAPTURE-001: display topology, logical-to-physical mapping, keyboard
// adjustment, cross-monitor compositing, and last-region validation. Normative
// behaviour lives in product-contract/capabilities/REGION-CAPTURE-001/SPEC.md;
// requirement IDs in comments refer to that file.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace xerahs::capture {

// Half-open [left, top, right, bottom) in desktop physical pixels.
struct PhysicalRect {
  qint64 left = 0;
  qint64 top = 0;
  qint64 right = 0;
  qint64 bottom = 0;

  qint64 width() const { return right - left; }
  qint64 height() const { return bottom - top; }
  bool positive() const { return width() > 0 && height() > 0; }
  bool contains(qint64 x, qint64 y) const { return x >= left && x < right && y >= top && y < bottom; }
  bool operator==(const PhysicalRect &other) const {
    return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
  }
  bool operator!=(const PhysicalRect &other) const { return !(*this == other); }
};

std::optional<PhysicalRect> intersection(const PhysicalRect &a, const PhysicalRect &b);

struct LogicalRect {
  double left = 0;
  double top = 0;
  double right = 0;
  double bottom = 0;
};

// RC-003: one display of the session's topology snapshot.
struct Display {
  QString id;  // stable display identity
  PhysicalRect physical;
  LogicalRect logical;
  double scale = 1.0;
};

using Topology = std::vector<Display>;

const Display *findDisplay(const Topology &topology, const QString &id);
// RC-023: bounding box of every capturable display.
PhysicalRect boundingBox(const Topology &topology);

struct PhysicalPoint {
  qint64 x = 0;
  qint64 y = 0;
};

// RC-004, RC-022, RC-023: maps a logical point on the reporting display and
// clamps it to that display's physical bounds.
PhysicalPoint mapToPhysical(const Display &display, double x, double y);
// RC-023: clamps a physical point to the display under it, or to the bounding
// box when no display reports it.
PhysicalPoint clampPhysical(const Topology &topology, PhysicalPoint point);
// RC-006, RC-022: normalized rectangle of two mapped points.
PhysicalRect normalized(PhysicalPoint a, PhysicalPoint b);

// RC-010, RC-024 keyboard steps.
enum class Edge { Left, Top, Right, Bottom };
struct KeyboardStep {
  enum class Action { Move, Resize };
  Action action = Action::Move;
  qint64 dx = 0;  // move direction in steps
  qint64 dy = 0;
  Edge edge = Edge::Right;  // resize edge
  qint64 delta = 0;         // resize direction in steps
  bool precision = false;
};
constexpr qint64 kKeyboardStep = 1;
constexpr qint64 kPrecisionStep = 10;
PhysicalRect applyKeyboardStep(const PhysicalRect &rect, const KeyboardStep &step, const PhysicalRect &bounds);

// RC-012, RC-023: premultiplied RGBA8 pixels, row-major, no padding.
struct RgbaImage {
  qint64 width = 0;
  qint64 height = 0;
  QByteArray pixels;
};

// Returns the exact physical pixels of `source` on `display`, or nullopt when
// the capture source failed. The image MUST be source.width() x
// source.height(); the compositor never resamples (RC-012).
using DisplayGrabber = std::function<std::optional<RgbaImage>(const Display &display, const PhysicalRect &source)>;

struct CompositeResult {
  std::optional<RgbaImage> image;
  QStringList sourceDisplays;     // in desktop physical order
  std::optional<QString> diagnostic;
};

// RC-012, RC-023: composites every intersecting display into one image of the
// rectangle's physical size; pixels no display covers stay transparent black.
CompositeResult composite(const PhysicalRect &rect, const Topology &topology, const DisplayGrabber &grab);
qint64 transparentBlackPixels(const RgbaImage &image);

// RC-017, RC-027: persisted last region, format version 1.
struct LastRegionDisplay {
  QString id;
  PhysicalRect physical;
};
struct LastRegion {
  int version = 1;
  PhysicalRect rectangle;
  std::vector<LastRegionDisplay> displays;
};

LastRegion makeLastRegion(const PhysicalRect &rectangle, const Topology &topology);
// RC-027: the rectangle to capture, or nullopt (last-region-invalid) when the
// record no longer matches the current topology.
std::optional<PhysicalRect> validateLastRegion(const LastRegion &stored, const Topology &topology);

namespace diagnostic {
inline const QString capturePermissionDenied = QStringLiteral("capture-permission-denied");
inline const QString displayTopologyChanged = QStringLiteral("display-topology-changed");
inline const QString captureSourceFailed = QStringLiteral("capture-source-failed");
inline const QString lastRegionInvalid = QStringLiteral("last-region-invalid");
}  // namespace diagnostic

}  // namespace xerahs::capture
