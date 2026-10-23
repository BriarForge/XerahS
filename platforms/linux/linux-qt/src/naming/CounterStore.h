// FILENAME-GENERATION-001 (FN-007, FN-015, FN-016, FN-025): persisted
// per-profile zero-based filename counter in one small versioned JSON file.
#pragma once

#include <QString>

#include <memory>
#include <optional>

class QLockFile;

namespace xerahs::naming {

enum class CounterError {
  None,
  Busy,         // another reservation holds the lock
  Overflow,     // FN-025: counter-overflow
  Corrupt,      // unreadable/invalid file; never reset silently
  NewerVersion, // written by a newer app; never overwritten
  Unsafe,       // path or directory is a symlink / not ours
  WriteFailed,
  Changed,      // file changed between reserve and commit
};
QString counterErrorName(CounterError error);

// Holds the cross-process lock until committed or destroyed (rollback).
class CounterReservation {
public:
  CounterReservation();
  CounterReservation(CounterReservation &&) noexcept;
  CounterReservation &operator=(CounterReservation &&) noexcept;
  ~CounterReservation();
  explicit operator bool() const { return m_lock != nullptr; }
  // ExpansionContext::counter for this name (zero-based, FN-025).
  qint64 value() const { return m_value; }
private:
  friend class CounterStore;
  std::unique_ptr<QLockFile> m_lock;
  QString m_profile;
  qint64 m_value = 0;
  QByteArray m_snapshot;
};

struct CounterReserveResult {
  CounterReservation reservation;
  CounterError error = CounterError::None;
};
struct CounterPeek {
  qint64 value = 0;
  CounterError error = CounterError::None;
};

class CounterStore {
public:
  explicit CounterStore(QString path, int lockTimeoutMs = 3000) : m_path(std::move(path)), m_lockTimeoutMs(lockTimeoutMs) {}
  // FN-015: non-committing read of the value the next name would use.
  CounterPeek peek(const QString &profileId) const;
  CounterReserveResult reserve(const QString &profileId) const;
  // Persists `next` (what the expander returned as nextCounter) and releases
  // the lock. Call only after the name was successfully published (FN-016).
  CounterError commit(CounterReservation &reservation, qint64 next) const;
private:
  QString m_path;
  int m_lockTimeoutMs;
};

}  // namespace xerahs::naming
