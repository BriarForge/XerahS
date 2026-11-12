#include "ScreenTopology.h"
#include "capture/ScreenTopology.h"
#include <QGuiApplication>
namespace xerahs::app {
QString screenIdentity(const QString &name, const QString &manufacturer, const QString &model, const QString &serial) {
  return xerahs::capture::stableDisplayIdentity(name, manufacturer, model, serial);
}
CaptureDisplays screenTopology() {
  const auto screens = QGuiApplication::screens();
  std::vector<xerahs::capture::ScreenDescription> descriptions;
  for (auto *screen : screens) {
    const auto g = screen->geometry();
    descriptions.push_back({screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber(),
        {double(g.x()), double(g.y()), double(g.x() + g.width()), double(g.y() + g.height())}, screen->devicePixelRatio(),
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
}
