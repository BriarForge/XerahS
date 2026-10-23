#include "capture/WindowSnapTargets.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace xerahs::capture {
namespace {
// Reject absent/wrongly typed fields instead of inventing visible geometry.
bool number(const QJsonValue &v) { return v.isDouble() && std::isfinite(v.toDouble()); }
struct Candidate { SnapTarget target; int layer; int focus; };
bool safelyMaps(const Display &d, double x, double y) {
  const double px = double(d.physical.left) + (x - d.logical.left) * d.scale;
  const double py = double(d.physical.top) + (y - d.logical.top) * d.scale;
  // mapToPhysical rounds before clamping; reject corrupt values outside its integer range.
  constexpr double limit = 9223372036854775808.0;
  return std::isfinite(px) && std::isfinite(py) && px >= -limit && px < limit && py >= -limit && py < limit;
}
}

std::vector<SnapTarget> hyprlandSnapTargets(const QByteArray &clientsJson, const QByteArray &monitorsJson,
                                          const Topology &topology, qint64 ownPid) {
  const auto clients = QJsonDocument::fromJson(clientsJson);
  const auto monitors = QJsonDocument::fromJson(monitorsJson);
  if (!clients.isArray() || !monitors.isArray()) return {};
  std::vector<Candidate> candidates;
  for (const auto &value : clients.array()) {
    const auto w = value.toObject();
    if (!w.value("mapped").toBool() || !w.value("hidden").isBool() || w.value("hidden").toBool() ||
        !number(w.value("pid")) || w.value("pid").toDouble() == double(ownPid)) continue;
    const auto at = w.value("at").toArray(), size = w.value("size").toArray();
    if (at.size() != 2 || size.size() != 2 || !number(at[0]) || !number(at[1]) ||
        !number(size[0]) || !number(size[1]) || size[0].toDouble() <= 0 || size[1].toDouble() <= 0) continue;
    const auto workspace = w.value("workspace").toObject().value("id");
    if (!number(workspace) || !number(w.value("monitor"))) continue;
    const Display *display = nullptr;
    for (const auto &monitorValue : monitors.array()) {
      const auto monitor = monitorValue.toObject();
      if (!number(monitor.value("id")) || monitor.value("id") != w.value("monitor") ||
          monitor.value("disabled").toBool()) continue;
      const auto active = monitor.value("activeWorkspace").toObject().value("id");
      const auto special = monitor.value("specialWorkspace").toObject().value("id");
      if ((number(active) && workspace == active) ||
          (number(special) && special.toInt() != 0 && workspace == special))
        display = findDisplay(topology, monitor.value("name").toString());
      break;
    }
    if (!display) continue;
    // Both exclusive edges use the owning monitor, including off-screen edges.
    const double x = at[0].toDouble(), y = at[1].toDouble();
    const double right = x + size[0].toDouble(), bottom = y + size[1].toDouble();
    if (!std::isfinite(right) || !std::isfinite(bottom)) continue;
    if (!safelyMaps(*display, x, y) || !safelyMaps(*display, right, bottom)) continue;
    const auto a = mapToPhysical(*display, x, y);
    const auto b = mapToPhysical(*display, right, bottom);
    const auto rect = normalized(a, b);
    if (!rect.positive()) continue;
    const auto fullscreen = w.value("fullscreen");
    const int layer = (fullscreen.isBool() ? fullscreen.toBool() : fullscreen.toInt() != 0) ? 0 :
        w.value("floating").toBool() ? 1 : 2;
    const int focus = w.value("focusHistoryID").toInt(-1);
    candidates.push_back({{rect}, layer, focus < 0 ? std::numeric_limits<int>::max() : focus});
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
    return a.layer != b.layer ? a.layer < b.layer : a.focus < b.focus;
  });
  std::vector<SnapTarget> result;
  for (const auto &candidate : candidates) result.push_back(candidate.target);
  return result;
}
}  // namespace xerahs::capture
