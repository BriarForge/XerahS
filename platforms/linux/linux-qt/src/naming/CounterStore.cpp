#include "CounterStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <limits>

namespace xerahs::naming {
namespace {
const QString kFormat = QStringLiteral("com.xerahs.app/linux-qt/filename-counters");
constexpr int kVersion = 1;
constexpr qint64 kMaxBytes = 1 << 20;

struct Loaded { QByteArray bytes; QJsonObject counters; bool exists = false; CounterError error = CounterError::None; };

Loaded load(const QString &path) {
  Loaded out; QFile file(path);
  if (!QFileInfo(path).isSymLink() && !file.exists()) return out;
  if (QFileInfo(path).isSymLink()) { out.error = CounterError::Unsafe; return out; }
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxBytes) { out.error = CounterError::Corrupt; return out; }
  out.exists = true; out.bytes = file.readAll();
  QJsonParseError parse; const auto doc = QJsonDocument::fromJson(out.bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) { out.error = CounterError::Corrupt; return out; }
  const auto root = doc.object();
  if (root.value("format").toString() != kFormat || !root.value("version").isDouble()) { out.error = CounterError::Corrupt; return out; }
  if (root.value("version").toInt() > kVersion) { out.error = CounterError::NewerVersion; return out; }
  if (root.value("version").toInt() != kVersion || !root.value("counters").isObject()) { out.error = CounterError::Corrupt; return out; }
  out.counters = root.value("counters").toObject(); return out;
}
// Values are decimal strings: JSON doubles cannot hold every int64 (FN-025).
bool parseValue(const QJsonValue &v, qint64 &result) {
  if (v.isUndefined()) { result = 0; return true; }
  if (!v.isString()) return false;
  bool ok = false; result = v.toString().toLongLong(&ok);
  return ok && result >= 0 && QString::number(result) == v.toString();
}
bool safeDirectory(const QString &path) {
  const QFileInfo dir(QFileInfo(path).absolutePath());
  return QFileInfo(path).isAbsolute() && !dir.isSymLink();
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
  if (profileId.isEmpty() || !safeDirectory(m_path)) { result.error = CounterError::Unsafe; return result; }
  if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) { result.error = CounterError::WriteFailed; return result; }
  auto lock = std::make_unique<QLockFile>(m_path + QStringLiteral(".lock"));
  lock->setStaleLockTime(30000);
  if (!lock->tryLock(m_lockTimeoutMs)) { result.error = CounterError::Busy; return result; }
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
  QSaveFile file(m_path); file.setDirectWriteFallback(false);
  const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  const bool ok = file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
                  file.write(bytes) == bytes.size() && file.commit();
  release();
  return ok ? CounterError::None : CounterError::WriteFailed;
}

}  // namespace xerahs::naming
