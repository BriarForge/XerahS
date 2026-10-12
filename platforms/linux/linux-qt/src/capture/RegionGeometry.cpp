#include "capture/RegionGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xerahs::capture {

namespace {

qint64 roundPhysical(double value) {
  // RC-022: IEEE 754 double, then floor(value + 0.000001).
  return static_cast<qint64>(std::floor(value + 0.000001));
}

// RC-023: right and bottom are valid exclusive edges, so clamping is inclusive.
PhysicalPoint clampTo(const PhysicalRect &bounds, PhysicalPoint point) {
  return {std::clamp(point.x, bounds.left, bounds.right), std::clamp(point.y, bounds.top, bounds.bottom)};
}

bool reaches(const PhysicalRect &bounds, PhysicalPoint point) {
  return point.x >= bounds.left && point.x <= bounds.right && point.y >= bounds.top && point.y <= bounds.bottom;
}

}  // namespace

std::optional<PhysicalRect> intersection(const PhysicalRect &a, const PhysicalRect &b) {
  const PhysicalRect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
                       std::min(a.bottom, b.bottom)};
  if (r.right <= r.left || r.bottom <= r.top) return std::nullopt;
  return r;
}

const Display *findDisplay(const Topology &topology, const QString &id) {
  for (const Display &display : topology) {
    if (display.id == id) return &display;
  }
  return nullptr;
}

PhysicalRect boundingBox(const Topology &topology) {
  if (topology.empty()) return {};
  PhysicalRect box = topology.front().physical;
  for (const Display &display : topology) {
    box.left = std::min(box.left, display.physical.left);
    box.top = std::min(box.top, display.physical.top);
    box.right = std::max(box.right, display.physical.right);
    box.bottom = std::max(box.bottom, display.physical.bottom);
  }
  return box;
}

PhysicalPoint mapToPhysical(const Display &display, double x, double y) {
  const PhysicalPoint mapped{
      roundPhysical(static_cast<double>(display.physical.left) + (x - display.logical.left) * display.scale),
      roundPhysical(static_cast<double>(display.physical.top) + (y - display.logical.top) * display.scale)};
  return clampTo(display.physical, mapped);
}

PhysicalPoint clampPhysical(const Topology &topology, PhysicalPoint point) {
  for (const Display &display : topology) {
    if (reaches(display.physical, point)) return point;
  }
  // Pending clarification (ROOT-ESCALATE-001): RC-023 clamps to the reporting
  // display, but a physical-space event names none. A point outside every
  // display is clamped to the bounding box of all displays.
  return clampTo(boundingBox(topology), point);
}

PhysicalRect normalized(PhysicalPoint a, PhysicalPoint b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

PhysicalRect applyKeyboardStep(const PhysicalRect &rect, const KeyboardStep &step, const PhysicalRect &bounds) {
  const qint64 size = step.precision ? kPrecisionStep : kKeyboardStep;
  PhysicalRect r = rect;
  if (step.action == KeyboardStep::Action::Move) {
    // RC-024: a move that would cross the bounding box stops at the edge and
    // preserves size.
    const qint64 width = r.width();
    const qint64 height = r.height();
    r.left = std::clamp(r.left + step.dx * size, bounds.left, std::max(bounds.left, bounds.right - width));
    r.top = std::clamp(r.top + step.dy * size, bounds.top, std::max(bounds.top, bounds.bottom - height));
    r.right = r.left + width;
    r.bottom = r.top + height;
    return r;
  }
  // RC-024: a resize keeps width and height at least 1 and stays in bounds.
  const qint64 delta = step.delta * size;
  switch (step.edge) {
    case Edge::Left: r.left = std::clamp(r.left + delta, bounds.left, r.right - 1); break;
    case Edge::Top: r.top = std::clamp(r.top + delta, bounds.top, r.bottom - 1); break;
    case Edge::Right: r.right = std::clamp(r.right + delta, r.left + 1, bounds.right); break;
    case Edge::Bottom: r.bottom = std::clamp(r.bottom + delta, r.top + 1, bounds.bottom); break;
  }
  return r;
}

CompositeResult composite(const PhysicalRect &rect, const Topology &topology, const DisplayGrabber &grab,
                          const CompositeControl &control) {
  CompositeResult result;
  const auto abandon = [&]() {
    result.image.reset(); result.sourceDisplays.clear(); result.diagnostic.reset(); result.cancelled = true;
    return result;
  };
  if (rect.right <= rect.left || rect.bottom <= rect.top) {
    result.diagnostic = diagnostic::captureSourceFailed;
    return result;
  }
  const quint64 width = quint64(rect.right) - quint64(rect.left);
  const quint64 height = quint64(rect.bottom) - quint64(rect.top);
  if (width > kMaximumCaptureDimension || height > kMaximumCaptureDimension || width > kMaximumCapturePixels / height) {
    result.diagnostic = diagnostic::captureSourceFailed;
    return result;
  }
  try {
    const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
    if (cancelled()) return abandon();

    std::vector<const Display *> sources;
    for (const Display &display : topology) {
      if (display.physical.right <= display.physical.left || display.physical.bottom <= display.physical.top) {
        result.diagnostic = diagnostic::captureSourceFailed; return result;
      }
      if (intersection(rect, display.physical)) sources.push_back(&display);
    }
    // Desktop physical order: top to bottom, then left to right.
    std::stable_sort(sources.begin(), sources.end(), [](const Display *a, const Display *b) {
      if (a->physical.top != b->physical.top) return a->physical.top < b->physical.top;
      return a->physical.left < b->physical.left;
    });

    RgbaImage image;
    image.width = rect.width();
    image.height = rect.height();
    qint64 totalRows = image.height;
    for (const Display *display : sources) totalRows += intersection(rect, display->physical)->height();
    qint64 doneRows = 0; int reported = -1;
    const auto progress = [&] {
      const int value = int(doneRows * 100 / totalRows);
      if (value != reported) { reported = value; if (control.progress) control.progress(value); }
    };
    progress(); if (cancelled()) return abandon();
    // RC-023: transparent black wherever no display covers the rectangle.
    image.pixels.resize(static_cast<qsizetype>(image.width * image.height * 4));
    for (qint64 row = 0; row < image.height; ++row) {
      if (cancelled()) return abandon();
      std::memset(image.pixels.data() + row * image.width * 4, 0, std::size_t(image.width * 4));
      ++doneRows; progress();
    }

    for (const Display *display : sources) {
      const PhysicalRect source = *intersection(rect, display->physical);
      if (cancelled()) return abandon();
      const std::optional<RgbaImage> pixels = grab(*display, source);
      if (cancelled()) return abandon();
      if (!pixels || pixels->width != source.width() || pixels->height != source.height() ||
          pixels->pixels.size() != static_cast<qsizetype>(source.width() * source.height() * 4)) {
        // RC-012, RC-018: a missing or resampled source is a capture-source
        // failure; no partial image is returned.
        result.diagnostic = diagnostic::captureSourceFailed;
        result.sourceDisplays.clear();
        return result;
      }
      const qint64 rowBytes = source.width() * 4;
      for (qint64 row = 0; row < source.height(); ++row) {
        if (cancelled()) return abandon();
        const qint64 target = ((source.top - rect.top + row) * image.width + (source.left - rect.left)) * 4;
        std::memcpy(image.pixels.data() + target, pixels->pixels.constData() + row * rowBytes,
                    static_cast<std::size_t>(rowBytes));
        ++doneRows; progress();
      }
      result.sourceDisplays.append(display->id);
    }
    if (cancelled()) return abandon();
    result.image = std::move(image);
    return result;
  } catch (...) {
    result.image.reset(); result.sourceDisplays.clear(); result.cancelled = false;
    result.diagnostic = diagnostic::captureSourceFailed; return result;
  }
}

qint64 transparentBlackPixels(const RgbaImage &image) {
  qint64 count = 0;
  const char *data = image.pixels.constData();
  for (qint64 i = 0; i < image.width * image.height; ++i) {
    const char *pixel = data + i * 4;
    if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0) ++count;
  }
  return count;
}

LastRegion makeLastRegion(const PhysicalRect &rectangle, const Topology &topology) {
  LastRegion region;
  region.rectangle = rectangle;
  for (const Display &display : topology) {
    if (intersection(rectangle, display.physical)) region.displays.push_back({display.id, display.physical});
  }
  return region;
}

std::optional<PhysicalRect> validateLastRegion(const LastRegion &stored, const Topology &topology) {
  // Pending clarification (ROOT-ESCALATE-001): RC-027 names only missing or
  // moved displays. A record of another format version, with no positive
  // rectangle, or with no recorded display cannot be checked against the
  // topology, so it is also last-region-invalid.
  if (stored.version != 1 || !stored.rectangle.positive() || stored.displays.empty()) return std::nullopt;
  for (const LastRegionDisplay &recorded : stored.displays) {
    const Display *current = findDisplay(topology, recorded.id);
    if (!current || current->physical != recorded.physical) return std::nullopt;
  }
  return stored.rectangle;
}

}  // namespace xerahs::capture
