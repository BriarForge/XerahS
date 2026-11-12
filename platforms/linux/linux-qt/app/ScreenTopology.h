// Qt is the approved pre-acquisition topology authority (2026-11-04).
#pragma once
#include "capture/RegionGeometry.h"
#include <QPointer>
#include <QScreen>
namespace xerahs::app {
struct CaptureDisplay {
  QPointer<QScreen> screen;
  xerahs::capture::Display mapping;
  QString outputName;
};
using CaptureDisplays = std::vector<CaptureDisplay>;
QString screenIdentity(const QString &name, const QString &manufacturer, const QString &model, const QString &serial);
CaptureDisplays screenTopology();
xerahs::capture::Topology displayMappings(const CaptureDisplays &screens);
}
