#include "PipelineRetry.h"
#include "PipelineJournal.h"
#include <QSet>
#include <limits>
namespace xerahs::actions {
namespace {
PipelineRetryPlan unavailable(const char *code) { return {{}, {}, QString::fromLatin1(code)}; }
bool nativeAction(const QString &id) { return id == "save" || id == "image-clipboard"; }
}
bool validPipelineRetryLink(const PipelineRetryLink &link, const QString &taskId) {
  if (!validJournalTaskId(taskId) || !validJournalTaskId(link.originalTaskId) || !validJournalTaskId(link.previousTaskId) ||
      link.originalTaskId == taskId || link.previousTaskId == taskId || link.attempt < 1 ||
      (link.attempt == 1 ? link.originalTaskId != link.previousTaskId : link.originalTaskId == link.previousTaskId)) return false;
  int previous = -1;
  for (const auto &id : link.preservedSuccessfulActions) {
    const auto index = catalogueIndex(id); if (!index || *index <= previous) return false;
    previous = *index;
  }
  return true;
}
PipelineRetryPlan planRetainedCaptureRetry(const PipelineResult &source, bool mediaAvailable, bool savedFileAvailable) {
  if (!validJournalTaskId(source.taskId) || (source.retry && !validPipelineRetryLink(*source.retry, source.taskId)))
    return unavailable("retry-invalid-source");
  if (source.state == PipelineState::Interrupted) return unavailable("retry-outcome-unknown");
  if (!mediaAvailable || !source.workingMediaRetained) return unavailable("retry-media-unavailable");
  if (source.finalWorkingMedia != "capture") return unavailable("retry-media-unavailable");
  PipelineRetryPlan plan;
  PipelineRetryLink link;
  link.originalTaskId = source.retry ? source.retry->originalTaskId : source.taskId; link.previousTaskId = source.taskId;
  if (source.retry) {
    if (source.retry->attempt == std::numeric_limits<int>::max()) return unavailable("retry-limit-reached");
    link.attempt = source.retry->attempt + 1;
  }
  QSet<QString> successes;
  if (source.retry) for (const auto &id : source.retry->preservedSuccessfulActions) successes.insert(id);
  int previous = -1;
  for (const auto &result : source.orderedResults) {
    const auto index = catalogueIndex(result.action);
    if (!index || *index <= previous) return unavailable("retry-invalid-source");
    previous = *index;
    if (result.state == ActionState::Succeeded) successes.insert(result.action);
    if (result.state != ActionState::Failed &&
        !(result.state == ActionState::Skipped && result.diagnostic == diagnostic::dependencyUnavailable)) continue;
    if (!nativeAction(result.action)) return unavailable("retry-action-unavailable");
    // These two actions require only retained working media. A saved file
    // paired with failed save is inconsistent authority, never a reason to
    // silently create a second saved copy.
    if (result.action == "save" && savedFileAvailable) return unavailable("retry-invalid-source");
    if (successes.contains(result.action)) return unavailable("retry-invalid-source");
    plan.selected.append(result.action);
  }
  for (const auto &action : catalogue()) if (successes.contains(action.id)) link.preservedSuccessfulActions.append(action.id);
  if (savedFileAvailable && !successes.contains("save")) return unavailable("retry-invalid-source");
  if (successes.contains("save") && !savedFileAvailable) return unavailable("retry-artifact-unavailable");
  if (plan.selected.isEmpty()) return unavailable("retry-no-failed-actions");
  plan.link = link; return plan;
}
} // namespace xerahs::actions
