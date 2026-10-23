#pragma once
#include "capture/RegionSession.h"
#include <memory>
#include <optional>

namespace xerahs::app {
// Optional compositor port: failures leave ordinary region selection available.
class WindowDiscovery {
public:
  virtual ~WindowDiscovery() = default;
  virtual std::vector<capture::SnapTarget> targets(const capture::Topology &topology, qint64 ownPid) = 0;
  // CORE-014: the focused window in desktop physical space. Nullopt when the
  // compositor, the window, or a window on a display is absent.
  virtual std::optional<capture::SnapTarget> activeWindow(const capture::Topology &topology, qint64 ownPid) = 0;
};
std::unique_ptr<WindowDiscovery> nativeWindowDiscovery();
}  // namespace xerahs::app
