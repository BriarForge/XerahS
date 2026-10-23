#include "CounterStore.h"

#include <QFileInfo>
#include <QJsonObject>

#include <limits>

namespace xerahs::naming {
namespace {
const QString kFormat = QStringLiteral("com.xerahs.app/linux-qt/filename-counters");
constexpr int kVersion = 1;
storage::PrivateJsonFile counterFile(const QString &path, int lockWaitMs = 0) {
  return {path, {kFormat, kVersion, 1 << 20, lockWaitMs}};
}
CounterError counterError(storage::StoreStatus status) {
  switch (status) {
    case storage::StoreStatus::Ok:
    case storage::StoreStatus::Missing: return CounterError::None;
    case storage::StoreStatus::Busy: return CounterError::Busy;
    case storage::StoreStatus::Unsafe: return CounterError::Unsafe;
    case storage::StoreStatus::NewerVersion: return CounterError::NewerVersion;
    case storage::StoreStatus::Changed: return CounterError::Changed;
    case storage::StoreStatus::WriteFailed: return CounterError::WriteFailed;
    default: return CounterError::Corrupt;
  }
}
struct Loaded { QByteArray bytes; QJsonObject counters; CounterError error = CounterError::None; };
Loaded load(const QString &path) {
  const auto read = counterFile(path).read();
  Loaded out{read.snapshot, {}, counterError(read.status)};
  if (read.status == storage::StoreStatus::Ok) {
    if (read.version != kVersion || !read.document.value("counters").isObject()) out.error = CounterError::Corrupt;
    else out.counters = read.document.value("counters").toObject();
  }
  return out;
}
// Values are decimal strings: JSON doubles cannot hold every int64 (FN-025).
bool parseValue(const QJsonValue &v, qint64 &result) {
  if (v.isUndefined()) { result = 0; return true; }
  if (!v.isString()) return false;
  bool ok = false; result = v.toString().toLongLong(&ok);
  return ok && result >= 0 && QString::number(result) == v.toString();
}
}

QString counterErrorName(CounterError error) {
  switch (error) {
    case CounterError::None: return {};
    case CounterError::Busy: return QStringLiteral("counter-busy");
    case CounterError::Overflow: return QStringLiteral("counter-overflow");
    case CounterError::Corrupt: return QStringLiteral("counter-corrupt");
    case CounterError::NewerVersion: return QStringLiteral("counter-newer-version");
    case CounterError::Unsafe: return QStringLiteral("counter-unsafe-path");
    case CounterError::WriteFailed: return QStringLiteral("counter-write-failed");
    case CounterError::Changed: return QStringLiteral("counter-changed");
  }
  return {};
}

CounterReservation::CounterReservation() = default;
CounterReservation::CounterReservation(CounterReservation &&) noexcept = default;
CounterReservation &CounterReservation::operator=(CounterReservation &&) noexcept = default;
CounterReservation::~CounterReservation() = default;  // unlocks: rollback without a write

CounterPeek CounterStore::peek(const QString &profileId) const {
  const auto loaded = load(m_path);
  if (loaded.error != CounterError::None) return {0, loaded.error};
  qint64 value = 0;
  if (!parseValue(loaded.counters.value(profileId), value)) return {0, CounterError::Corrupt};
  return {value, CounterError::None};
}

// FN-016: the lock is held from reserve through commit (or rollback by
// destruction), so two processes can never hold the same value, and a failed
// or cancelled save leaves the file untouched and consumes nothing.
CounterReserveResult CounterStore::reserve(const QString &profileId) const {
  CounterReserveResult result;
  if (profileId.isEmpty() || !QFileInfo(m_path).isAbsolute()) { result.error = CounterError::Unsafe; return result; }
  auto lock = std::make_unique<storage::StoreLock>();
  const auto locked = counterFile(m_path, m_lockTimeoutMs).lock(*lock);
  if (locked != storage::StoreStatus::Ok) { result.error = counterError(locked); return result; }
  const auto loaded = load(m_path);
  if (loaded.error != CounterError::None) { result.error = loaded.error; return result; }
  qint64 value = 0;
  if (!parseValue(loaded.counters.value(profileId), value)) { result.error = CounterError::Corrupt; return result; }
  // FN-025: advancing past INT64 max fails; nothing is handed out.
  if (value == std::numeric_limits<qint64>::max()) { result.error = CounterError::Overflow; return result; }
  result.reservation.m_lock = std::move(lock); result.reservation.m_profile = profileId;
  result.reservation.m_value = value; result.reservation.m_snapshot = loaded.bytes;
  return result;
}

CounterError CounterStore::commit(CounterReservation &reservation, qint64 next) const {
  if (!reservation) return CounterError::WriteFailed;
  const auto release = [&] { reservation.m_lock.reset(); };
  const auto loaded = load(m_path);
  if (loaded.error != CounterError::None) { release(); return loaded.error; }
  if (loaded.bytes != reservation.m_snapshot) { release(); return CounterError::Changed; }  // snapshot conflict
  if (next < reservation.m_value) { release(); return CounterError::WriteFailed; }
  auto counters = loaded.counters; counters.insert(reservation.m_profile, QString::number(next));
  const QJsonObject root{{"format", kFormat}, {"version", kVersion}, {"counters", counters}};
  const auto status = counterFile(m_path).write(root, kVersion, reservation.m_snapshot, nullptr, reservation.m_lock.get());
  release();
  return counterError(status);
}

}  // namespace xerahs::naming
