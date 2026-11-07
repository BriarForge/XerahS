#include "capture/CaptureConditions.h"

namespace xerahs::capture {

AutoCondition evaluate(const ConditionSources &sources) {
  if (sources.freeBytes) {
    const qint64 free = sources.freeBytes();
    if (free >= 0 && free < sources.minFreeBytes) return AutoCondition::StorageLow;
  }
  if (sources.screenLocked && sources.screenLocked()) return AutoCondition::ScreenLocked;
  return AutoCondition::Clear;
}

QString conditionCode(AutoCondition condition) {
  switch (condition) {
  case AutoCondition::ScreenLocked: return QStringLiteral("screen-locked");
  case AutoCondition::StorageLow: return QStringLiteral("storage-low");
  case AutoCondition::WorkflowUnavailable: return QStringLiteral("workflow-unavailable");
  case AutoCondition::Clear: break;
  }
  return {};
}

void ScreenLockState::setLocked(bool locked) {
  if (m_locked == locked) return;
  m_locked = locked;
  emit lockedChanged(locked);
}

}  // namespace xerahs::capture
