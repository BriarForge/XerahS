#include "ScreenTopology.h"
#include "capture/CompositorFrame.h"
#include "capture/ScreenTopology.h"
#include <QColorSpace>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace xerahs::app {
QString screenIdentity(const QString &name, const QString &manufacturer, const QString &model, const QString &serial) {
  return xerahs::capture::stableDisplayIdentity(name, manufacturer, model, serial);
}
double effectiveWaylandScale(double qtDpr, int logicalWidth, int logicalHeight, const CompositorOutputFacts &facts) {
  if (!facts.sawCurrentMode || !facts.sawGeometry || !facts.sawDone || facts.conflictingMode ||
      facts.modeWidth <= 0 || facts.modeHeight <= 0 || logicalWidth <= 0 || logicalHeight <= 0) {
    return qtDpr;
  }
  const bool swapped = (facts.transform == 1 || facts.transform == 3 || facts.transform == 5 || facts.transform == 7);
  const qint64 physW = swapped ? facts.modeHeight : facts.modeWidth;
  const qint64 physH = swapped ? facts.modeWidth : facts.modeHeight;
  const auto matchesMode = [&](double scale) {
    return std::isfinite(scale) && scale > 0 &&
           qint64(std::floor(double(logicalWidth) * scale + 0.000001)) == physW &&
           qint64(std::floor(double(logicalHeight) * scale + 0.000001)) == physH;
  };
  if (matchesMode(qtDpr)) return qtDpr;
  const double sx = double(physW) / double(logicalWidth);
  const double sy = double(physH) / double(logicalHeight);
  if (std::isfinite(sx) && std::isfinite(sy) && sx > 0 && sy > 0 && std::abs(sx - sy) <= 0.01) {
    const double candidate = std::max(sx, sy);
    if (matchesMode(candidate)) return candidate;
  }
  return qtDpr;
}
CaptureDisplays screenTopology() {
  const auto screens = QGuiApplication::screens();
  const auto waylandOutputs = cachedWaylandOutputs();
  std::vector<xerahs::capture::ScreenDescription> descriptions;
  for (auto *screen : screens) {
    const auto g = screen->geometry();
    const CompositorOutputFacts *match = nullptr;
    int matchCount = 0;
    const QString screenName = screen->name();
    if (!screenName.isEmpty()) {
      for (const auto &facts : waylandOutputs) {
        if (facts.name == screenName) {
          match = &facts;
          ++matchCount;
        }
      }
    }
    const double scale = (matchCount == 1 && match)
        ? effectiveWaylandScale(screen->devicePixelRatio(), g.width(), g.height(), *match)
        : screen->devicePixelRatio();
    descriptions.push_back({screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber(),
        {double(g.x()), double(g.y()), double(g.x() + g.width()), double(g.y() + g.height())}, scale,
        int(screen->orientation()), QStringLiteral("Qt screen depth %1; color primaries/transfer/HDR unavailable").arg(screen->depth()), true});
  }
  const auto topology = xerahs::capture::snapshotScreenTopology(descriptions);
  CaptureDisplays result;
  if (topology.size() != size_t(screens.size())) return result;
  for (qsizetype n = 0; n < screens.size(); ++n) result.push_back({screens[n], topology[size_t(n)], screens[n]->name()});
  return result;
}
xerahs::capture::Topology displayMappings(const CaptureDisplays &screens) {
  xerahs::capture::Topology result;
  for (const auto &screen : screens) result.push_back(screen.mapping);
  return result;
}

ComposedDesktop composeCapturedFrames(const CaptureDisplays &displays, const std::vector<CapturedDisplayFrame> &frames) {
  ComposedDesktop result;
  if (displays.empty()) {
    result.diagnostic = QStringLiteral("compositor-layout-unproven");
    return result;
  }
  const auto mismatch = [&result](const QString &detail = QStringLiteral("compositor-source-mismatch")) {
    result.diagnostic = detail;
    return result;
  };
  // Every capturable display must be covered exactly once, by its own name;
  // an unnamed display cannot be captured by compositor identity.
  std::vector<const CapturedDisplayFrame *> match;
  match.resize(displays.size(), nullptr);
  for (const CapturedDisplayFrame &frame : frames) {
    if (frame.outputName.isEmpty()) return mismatch();   // an unnamed frame owns no display
    bool assigned = false;
    for (size_t index = 0; index < displays.size(); ++index) {
      if (displays[index].outputName != frame.outputName) continue;
      if (match[index]) return mismatch();   // one display, two frames: refuse whole
      match[index] = &frame;
      assigned = true;
    }
    if (!assigned) return mismatch();   // a frame no display owns
  }
  const auto bounds = xerahs::capture::boundingBox(displayMappings(displays));
  if (!bounds.positive()) {
    result.diagnostic = QStringLiteral("capture-topology-mismatch");
    return result;
  }
  QImage composed(int(bounds.width()), int(bounds.height()), QImage::Format_ARGB32);
  if (composed.isNull()) {
    result.diagnostic = QStringLiteral("capture-source-failed");
    return result;
  }
  bool colorSpaceSeen = false;
  for (size_t index = 0; index < displays.size(); ++index) {
    const CaptureDisplay &display = displays[index];
    const CapturedDisplayFrame *frame = match[index];
    if (!display.mapping.captureAvailable) continue;
    if (display.outputName.isEmpty()) return mismatch(QStringLiteral("compositor-layout-unproven"));
    if (!frame) return mismatch();   // RC-005: every capturable display is covered
    // The cross-check: the compositor's own facts must agree with the frozen
    // session display - unrotated, own scale, mode-verified bounds, and the
    // compositor logical position equals the session's logical origin.
    if (frame->transform != 0 || qint32(std::ceil(display.mapping.scale - 0.000001)) != frame->scale ||
        frame->bufferWidth != qint32(display.mapping.physical.width()) ||
        frame->bufferHeight != qint32(display.mapping.physical.height()) ||
        frame->logicalX != qint32(display.mapping.logical.left) ||
        frame->logicalY != qint32(display.mapping.logical.top) || !frame->singleOutputBufferOriginProven) {
      return mismatch();
    }
    if (frame->image.width() != frame->bufferWidth || frame->image.height() != frame->bufferHeight) return mismatch();
    // Paint at the session's packed origin; the frames stay the compositor's
    // own pixels, only their placement comes from the decided arrangement.
    const int x = int(display.mapping.physical.left - bounds.left);
    const int y = int(display.mapping.physical.top - bounds.top);
    const QImage pixels = frame->image.convertToFormat(QImage::Format_ARGB32);
    if (pixels.isNull()) {
      result.diagnostic = QStringLiteral("capture-source-failed");
      return result;
    }
    for (int row = 0; row < pixels.height(); ++row)
      std::memcpy(composed.scanLine(y + row) + size_t(x) * sizeof(QRgb), pixels.constScanLine(row),
                  size_t(pixels.width()) * sizeof(QRgb));
    if (!colorSpaceSeen) {
      composed.setColorSpace(pixels.colorSpace());
      colorSpaceSeen = true;
    }
  }
  result.ok = true;
  result.image = composed;
  return result;
}
}
