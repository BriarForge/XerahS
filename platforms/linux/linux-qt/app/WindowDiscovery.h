#pragma once
#include "capture/RegionSession.h"
#include <memory>

namespace xerahs::app {
// Optional compositor port: failures leave ordinary region selection available.
class WindowDiscovery {
public:
  virtual ~WindowDiscovery() = default;
  virtual std::vector<capture::SnapTarget> targets(const capture::Topology &topology, qint64 ownPid) = 0;
};
std::unique_ptr<WindowDiscovery> nativeWindowDiscovery();
}  // namespace xerahs::app
