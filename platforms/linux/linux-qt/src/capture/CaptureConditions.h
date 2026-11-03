// CORE-016: conditions that pause or stop automatic capture. Sources are injected so the
// policy is testable without a desktop session.
//   screen locked -> the tick is skipped (the run pauses and resumes after unlock; the bound still applies)
//   free storage below kMinFreeStorageBytes -> the run is refused or stopped
// Rationale: the reference app (ShareX/XerahS) has neither check and Flameshot/Greenshot have no auto capture,
// so the documented Linux policy is: lock pauses, 512 MiB of free space in the captures folder is the floor.
#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace xerahs::capture {

inline constexpr qint64 kMinFreeStorageBytes = 512LL * 1024 * 1024;

enum class AutoCondition { Clear, ScreenLocked, StorageLow };

struct ConditionSources {
  std::function<bool()> screenLocked;   // unset means unlocked
  std::function<qint64()> freeBytes;    // unset or negative means unknown (not blocking)
  qint64 minFreeBytes = kMinFreeStorageBytes;
};

// Storage takes precedence over lock: a full disk ends the run, a lock only pauses it.
AutoCondition evaluate(const ConditionSources &sources);
QString conditionCode(AutoCondition condition);

// Lock state with change notification; fed by the desktop adapter or by a test.
class ScreenLockState final : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  bool locked() const { return m_locked; }
  void setLocked(bool locked);
signals:
  void lockedChanged(bool locked);
private:
  bool m_locked = false;
};

}  // namespace xerahs::capture
