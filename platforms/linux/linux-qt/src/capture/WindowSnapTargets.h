// Pure Hyprland discovery and session-topology mapping (RC-009/011/022/023/026).
#pragma once
#include "capture/RegionSession.h"

namespace xerahs::capture {
std::vector<SnapTarget> hyprlandSnapTargets(const QByteArray &clientsJson, const QByteArray &monitorsJson,
                                          const Topology &topology, qint64 ownPid);
}  // namespace xerahs::capture
