#include "EditorWindowPlacement.h"
#include "EditorPreferences.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace xerahs::editor {
namespace {
bool finiteRect(const QRectF &rect) {
  const double lower = std::numeric_limits<int>::lowest(), upper = double(std::numeric_limits<int>::max()) + 1;
  return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) && std::isfinite(rect.height()) &&
      rect.x() >= lower && rect.y() >= lower && rect.x() + rect.width() <= upper && rect.y() + rect.height() <= upper &&
      rect.width() > 0 && rect.height() > 0;
}
double safeExtent(double value, double fallback) {
  return std::isfinite(value) && value >= 1 ? std::min(std::floor(value), maximumEditorWindowExtent) : fallback;
}
}
std::optional<EditorWindowPlacement> recoverEditorWindowPlacement(
    QRectF requested, const QList<QRectF> &displays, int preferred, QMarginsF margins, QSizeF minimum) {
  for (double margin : {margins.left(), margins.top(), margins.right(), margins.bottom()})
    if (!std::isfinite(margin) || margin < 0 || margin > maximumEditorWindowExtent) return {};
  const double horizontal = margins.left() + margins.right(), vertical = margins.top() + margins.bottom();
  const auto usable = [&](int i) {
    return i >= 0 && i < displays.size() && finiteRect(displays.at(i)) &&
        displays.at(i).width() >= horizontal + 1 && displays.at(i).height() >= vertical + 1;
  };
  const QSizeF desired{safeExtent(requested.width(), 1280), safeExtent(requested.height(), 720)};
  const QRectF requestedFrame(requested.x() - margins.left(), requested.y() - margins.top(),
      desired.width() + horizontal, desired.height() + vertical);
  int selected = -1; double overlap = 0;
  if (finiteRect(requestedFrame)) for (int i = 0; i < displays.size(); ++i) if (usable(i)) {
    const auto intersection = requestedFrame.intersected(displays.at(i));
    const double area = intersection.width() * intersection.height();
    if (!intersection.isEmpty() && area > overlap) { selected = i; overlap = area; }
  }
  if (selected < 0) {
    if (usable(preferred)) selected = preferred;
    else for (int i = 0; i < displays.size(); ++i) if (usable(i)) { selected = i; break; }
  }
  if (selected < 0) return {};
  const auto display = displays.at(selected);
  const double width = std::min(std::max(desired.width(), safeExtent(minimum.width(), 400)), display.width() - horizontal);
  const double height = std::min(std::max(desired.height(), safeExtent(minimum.height(), 300)), display.height() - vertical);
  const double frameWidth = width + horizontal, frameHeight = height + vertical;
  const double x = overlap > 0 ? std::clamp(requestedFrame.x(), display.x(), display.x() + display.width() - frameWidth) :
      display.x() + (display.width() - frameWidth) / 2;
  const double y = overlap > 0 ? std::clamp(requestedFrame.y(), display.y(), display.y() + display.height() - frameHeight) :
      display.y() + (display.height() - frameHeight) / 2;
  return EditorWindowPlacement{QRectF(x + margins.left(), y + margins.top(), width, height),
      QRectF(x, y, frameWidth, frameHeight), selected};
}
}  // namespace xerahs::editor
