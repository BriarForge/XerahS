#pragma once
#include "CaptureSave.h"
#include "actions/PipelineJournal.h"
#include <functional>
namespace xerahs::app {
struct CaptureDeleteHooks {
  // Must durably retain private recovery metadata before moving any entry.
  std::function<bool(const xerahs::actions::DeletionRecovery &)> observe;
  std::function<void(const QString &)> beforeMove;
  std::function<void(const QString &)> afterMove;
  std::function<void(const QString &)> beforeRestore;
};
// Refresh only a verified original restored after failed unlink; invalidate
// its live file binding when deletion is acknowledged.
xerahs::actions::ActionOutcome deleteCaptureFile(CaptureFile &file,
    const xerahs::actions::CancellationToken &cancellation, const CaptureDeleteHooks &hooks);
}
