#include "actions/PipelineResultJson.h"
#include <QJsonArray>

namespace xerahs::actions {
QJsonObject pipelineResultJson(const PipelineResult &result,
                              const std::vector<SavedFileArtifact> &files) {
  QJsonArray ordered, timings, artifacts;
  for (const auto &action : result.orderedResults) {
    QJsonObject entry{{"action", action.action}, {"state", actionStateName(action.state)}};
    if (action.diagnostic) entry.insert("diagnostic", *action.diagnostic);
    if (action.missingArtifact) entry.insert("missing_artifact", *action.missingArtifact);
    if (action.inputArtifact) entry.insert("input_artifact", *action.inputArtifact);
    if (action.cancellationArrivedDuring) entry.insert("cancellation_arrived_during", true);
    ordered.append(entry);
    // Pending/dependency-skipped actions have no execution timestamps. Do not
    // manufacture start/finish times when no native action ran.
    if (action.startedAt.isValid() || action.finishedAt.isValid()) {
      QJsonObject timing{{"action", action.action}};
      if (action.startedAt.isValid()) timing.insert("started_at", action.startedAt.toUTC().toString(Qt::ISODateWithMs));
      if (action.finishedAt.isValid()) timing.insert("finished_at", action.finishedAt.toUTC().toString(Qt::ISODateWithMs));
      timings.append(timing);
    }
  }
  for (const auto &file : files) {
    artifacts.append(QJsonObject{{"artifact", file.artifact}, {"path", file.path},
        {"media_type", file.mediaType}, {"byte_length", file.byteLength},
        {"filename_context", file.filenameContext}});
  }
  QJsonObject native{{"schema_version", 1}, {"task_id", result.taskId},
                     {"action_timings", timings}, {"file_artifacts", artifacts}};
  if (!result.ignoredSelection.isEmpty()) native.insert("ignored_selection", QJsonArray::fromStringList(result.ignoredSelection));
  return QJsonObject{{"pipeline_state", pipelineStateName(result.state)},
      {"final_working_media", result.finalWorkingMedia}, {"ordered_results", ordered},
      {"working_media_retained", result.workingMediaRetained},
      {"extensions", QJsonObject{{"com.xerahs.app", native}}}};
}
} // namespace xerahs::actions
