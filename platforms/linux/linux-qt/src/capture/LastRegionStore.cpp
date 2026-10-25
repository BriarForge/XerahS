#include "LastRegionStore.h"
#include <QJsonArray>
#include <QSet>
#include <QMap>
#include <cmath>

namespace xerahs::capture {
namespace {
// Geometry stays exact in interoperable JSON number readers. Native screen
// coordinates are far inside this range; no rounded geometry is accepted.
constexpr qint64 exactInteger = 9007199254740991LL;
bool coordinate(qint64 value) { return value >= -exactInteger && value <= exactInteger; }
bool validRect(const PhysicalRect &r) {
  return coordinate(r.left) && coordinate(r.top) && coordinate(r.right) && coordinate(r.bottom) && r.positive();
}
bool readCoordinate(const QJsonValue &value, qint64 &out) {
  const double d = value.toDouble();
  if (!value.isDouble() || !std::isfinite(d) || std::floor(d) != d || d < -double(exactInteger) || d > double(exactInteger)) return false;
  out = qint64(d); return true;
}
bool readRect(const QJsonValue &value, PhysicalRect &out) {
  const auto object = value.toObject();
  return value.isObject() && readCoordinate(object.value("left"), out.left) && readCoordinate(object.value("top"), out.top) &&
      readCoordinate(object.value("right"), out.right) && readCoordinate(object.value("bottom"), out.bottom) && validRect(out);
}
QJsonObject rectangle(const PhysicalRect &r, QJsonObject existing = {}) {
  existing.insert("left", r.left); existing.insert("top", r.top); existing.insert("right", r.right); existing.insert("bottom", r.bottom); return existing;
}
bool validRegion(const LastRegion &region) {
  if (region.version != 1 || !validRect(region.rectangle) || region.displays.empty()) return false;
  QSet<QString> ids; Topology topology;
  for (const auto &display : region.displays) {
    if (display.id.isEmpty() || display.id.contains(QChar(0)) || ids.contains(display.id) || !validRect(display.physical) || !intersection(region.rectangle, display.physical)) return false;
    ids.insert(display.id); topology.push_back({display.id, display.physical, {}, 1});
  }
  const auto bounds = boundingBox(topology);
  return region.rectangle.left >= bounds.left && region.rectangle.top >= bounds.top &&
      region.rectangle.right <= bounds.right && region.rectangle.bottom <= bounds.bottom;
}
std::optional<LastRegion> parse(const QJsonObject &document) {
  LastRegion result;
  if (document.value("version") != QJsonValue(1) || !readRect(document.value("rectangle"), result.rectangle) || !document.value("displays").isArray()) return std::nullopt;
  for (const auto &value : document.value("displays").toArray()) {
    const auto object = value.toObject(); LastRegionDisplay display;
    if (!value.isObject() || !object.value("id").isString() || !readRect(object.value("physical"), display.physical)) return std::nullopt;
    display.id = object.value("id").toString(); result.displays.push_back(std::move(display));
  }
  return validRegion(result) ? std::optional<LastRegion>(result) : std::nullopt;
}
}
LastRegionStore::LastRegionStore(QString path)
    : m_file(std::move(path), {QStringLiteral("com.xerahs.app/linux-qt/last-region"), 1, 1048576, 0, 30000, true}) {}
LastRegionRead LastRegionStore::read() const {
  LastRegionRead result; result.file = m_file.read();
  if (result.file.status == storage::StoreStatus::Ok) {
    result.region = parse(result.file.document);
    if (!result.region) result.file.status = storage::StoreStatus::Invalid;
  }
  return result;
}
storage::StoreStatus LastRegionStore::save(const LastRegion &region, const LastRegionRead &expected) const {
  using storage::StoreStatus;
  if (expected.file.status != StoreStatus::Ok && expected.file.status != StoreStatus::Missing) return expected.file.status;
  if (!validRegion(region)) return StoreStatus::Invalid;
  auto document = expected.file.document;
  document.insert("rectangle", rectangle(region.rectangle, document.value("rectangle").toObject()));
  QMap<QString, QJsonObject> previous;
  for (const auto &value : document.value("displays").toArray()) previous.insert(value.toObject().value("id").toString(), value.toObject());
  QJsonArray displays;
  for (const auto &display : region.displays) {
    auto object = previous.value(display.id); object.insert("id", display.id);
    object.insert("physical", rectangle(display.physical, object.value("physical").toObject())); displays.append(object);
  }
  document.insert("displays", displays);
  return m_file.write(document, 1, expected.file.snapshot, nullptr);
}
QString lastRegionStorageDiagnostic(storage::StoreStatus status) {
  using storage::StoreStatus;
  if (status == StoreStatus::Ok) return {};
  return QStringLiteral("last-region-storage-%1: Capture pixels were acquired, but last-region geometry was not saved. Existing metadata was kept; review last-region storage before trying again.").arg(storage::storeStatusName(status));
}
}
