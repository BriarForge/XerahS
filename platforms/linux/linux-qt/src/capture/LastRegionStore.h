// RC-017/027: durable geometry only, never screenshot pixels or live handles.
#pragma once
#include "capture/RegionGeometry.h"
#include "storage/PrivateJsonFile.h"

namespace xerahs::capture {
struct LastRegionRead {
  storage::StoreRead file;
  std::optional<LastRegion> region;
};
class LastRegionStore {
public:
  explicit LastRegionStore(QString path);
  LastRegionRead read() const;
  // Refuses invalid/newer state and stale snapshots. Compatible unknown fields
  // round-trip; only the completed selection and intersecting displays change.
  storage::StoreStatus save(const LastRegion &region, const LastRegionRead &expected) const;
private:
  storage::PrivateJsonFile m_file;
};
QString lastRegionStorageDiagnostic(storage::StoreStatus status);
}
