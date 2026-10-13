#pragma once
#include "PostCapturePipeline.h"
namespace xerahs::actions {
struct PipelineRetryPlan {
  QStringList selected;
  std::optional<PipelineRetryLink> link;
  QString diagnostic;
  bool available() const { return link && !selected.isEmpty() && diagnostic.isEmpty(); }
};
bool validPipelineRetryLink(const PipelineRetryLink &link, const QString &taskId);
// Native save/image/file/path clipboard and reveal retry. Working media is supplied by
// the immutable in-memory receipt, not inferred from a restart record or path.
// Catalogue-wide prerequisite/artifact retry remains separately Required.
PipelineRetryPlan planRetainedCaptureRetry(const PipelineResult &source, bool mediaAvailable, bool savedFileAvailable, bool savedAsFileAvailable = false);
} // namespace xerahs::actions
