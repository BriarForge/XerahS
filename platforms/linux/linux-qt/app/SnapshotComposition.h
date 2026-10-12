// Native pixel I/O boundary; selection/composition semantics remain Qt Core.
#pragma once
#include "capture/RegionGeometry.h"
#include <QImage>
#include <functional>
namespace xerahs::app {
struct ComposedSnapshot { QImage image; QString diagnostic; bool cancelled = false; };
using SnapshotComposer = std::function<ComposedSnapshot(const QImage &, const xerahs::capture::PhysicalRect &,
    const xerahs::capture::Topology &, const xerahs::capture::CompositeControl &)>;
ComposedSnapshot composeSnapshot(const QImage &desktop, const xerahs::capture::PhysicalRect &rectangle,
    const xerahs::capture::Topology &topology, const xerahs::capture::CompositeControl &control = {});
} // namespace xerahs::app
