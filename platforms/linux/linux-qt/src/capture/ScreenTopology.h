// RC-003/004: toolkit screen descriptions become one pre-acquisition snapshot.
#pragma once
#include "capture/RegionGeometry.h"
namespace xerahs::capture {
struct ScreenDescription {
  QString name, manufacturer, model, serial;
  LogicalRect logical;
  double scale = 1;
  int rotation = 0;
  QString colorDescription;
  bool captureAvailable = true;
};
QString stableDisplayIdentity(const QString &name, const QString &manufacturer, const QString &model, const QString &serial);
// Physical origins follow reference XerahS's cumulative logical columns/rows.
// Each extent uses only its display's scale. Invalid/ambiguous identity fails
// closed; bitmap dimensions never influence this topology.
Topology snapshotScreenTopology(const std::vector<ScreenDescription> &screens);
}
