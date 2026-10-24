#include "PipelineRetry.h"
#include "PipelineJournal.h"
#include <QSet>
#include <limits>
namespace xerahs::actions {
namespace {
PipelineRetryPlan unavailable(const char *code) { return {{}, {}, QString::fromLatin1(code)}; }
bool fileConsumer(const QString &id) { return id == "file-clipboard" || id == "path-clipboard" || id == "reveal"; }
bool nativeAction(const QString &id) { return id == "save" || id == "save-as" || id == "image-clipboard" || fileConsumer(id); }
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
  previous = -1;
  for (const auto &id : link.preservedRasterActions) {
    const auto index = catalogueIndex(id);
    if ((id != "save" && id != "save-as") || !index || *index <= previous || link.preservedSuccessfulActions.contains(id)) return false;
    previous = *index;
  }
  return true;
}
PipelineRetryPlan planRetainedCaptureRetry(const PipelineResult &source, bool mediaAvailable, bool savedFileAvailable, bool savedAsFileAvailable) {
  if (!validJournalTaskId(source.taskId) || (source.retry && !validPipelineRetryLink(*source.retry, source.taskId)))
    return unavailable("retry-invalid-source");
  if (source.state == PipelineState::Interrupted) return unavailable("retry-outcome-unknown");
  if (!mediaAvailable || !source.workingMediaRetained) return unavailable("retry-media-unavailable");
  if (source.finalWorkingMedia != "capture" && source.finalWorkingMedia != "annotation") return unavailable("retry-media-unavailable");
  PipelineRetryPlan plan;
  PipelineRetryLink link;
  link.originalTaskId = source.retry ? source.retry->originalTaskId : source.taskId; link.previousTaskId = source.taskId;
  if (source.retry) {
    if (source.retry->attempt == std::numeric_limits<int>::max()) return unavailable("retry-limit-reached");
    link.attempt = source.retry->attempt + 1;
  }
  QSet<QString> successes;
  QSet<QString> partialRasters;
  if (source.retry) for (const auto &id : source.retry->preservedRasterActions) partialRasters.insert(id);
  if (source.retry) for (const auto &id : source.retry->preservedSuccessfulActions) successes.insert(id);
  int previous = -1;
  for (const auto &result : source.orderedResults) {
    const auto index = catalogueIndex(result.action);
    if (!index || *index <= previous) return unavailable("retry-invalid-source");
    previous = *index;
    if (!validSavedOutputOutcomes(result.action, result.state, result.artifacts)) return unavailable("retry-invalid-source");
    const bool partial = hasAcknowledgedPartialRaster(result.action, result.state, result.artifacts);
    if (partial) partialRasters.insert(result.action);
    if (result.state == ActionState::Succeeded) { successes.insert(result.action); partialRasters.remove(result.action); }
    if (result.state != ActionState::Failed &&
        !(result.state == ActionState::Skipped && result.diagnostic == diagnostic::dependencyUnavailable)) continue;
    if (!nativeAction(result.action)) return unavailable("retry-action-unavailable");
    if (result.action == "reveal" && result.diagnostic == QStringLiteral("reveal-outcome-unknown")) return unavailable("retry-outcome-unknown");
    // Only typed partial-save acknowledgement can pair a failed save with a
    // known PNG. The native executor repairs its companion without a new PNG.
    if (result.action == "save" && savedFileAvailable && result.state == ActionState::Failed && result.diagnostic && result.diagnostic->startsWith("counter-"))
      return unavailable("retry-counter-persistence-failed");
    if (result.action == "save" && savedFileAvailable && !partial) return unavailable("retry-invalid-source");
    if (result.action == "save-as" && savedAsFileAvailable && !partial) return unavailable("retry-invalid-source");
    if (successes.contains(result.action)) return unavailable("retry-invalid-source");
    plan.selected.append(result.action);
  }
  for (const auto &action : catalogue()) if (successes.contains(action.id)) link.preservedSuccessfulActions.append(action.id);
  for (const auto &action : catalogue()) if (partialRasters.contains(action.id)) link.preservedRasterActions.append(action.id);
  if (source.finalWorkingMedia == "annotation" && !successes.contains("annotation")) return unavailable("retry-invalid-source");
  if (savedFileAvailable && !successes.contains("save") && !partialRasters.contains("save")) return unavailable("retry-invalid-source");
  if ((successes.contains("save") || partialRasters.contains("save")) && !savedFileAvailable) return unavailable("retry-artifact-unavailable");
  if (savedAsFileAvailable && !successes.contains("save-as") && !partialRasters.contains("save-as")) return unavailable("retry-invalid-source");
  if ((successes.contains("save-as") || partialRasters.contains("save-as")) && !savedAsFileAvailable) return unavailable("retry-artifact-unavailable");
  for (const auto &action : plan.selected)
    if (fileConsumer(action) && !savedFileAvailable && !savedAsFileAvailable && !plan.selected.contains("save") && !plan.selected.contains("save-as"))
      return unavailable("retry-prerequisite-unavailable");
  if (plan.selected.isEmpty()) return unavailable("retry-no-failed-actions");
  plan.link = link; return plan;
}
} // namespace xerahs::actions
