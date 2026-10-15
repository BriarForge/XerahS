#include "WorkflowDiscovery.h"
#include "WorkflowStoragePath.h"
#include "workflows/CaptureWorkflows.h"
namespace xerahs::app {
QJsonObject discoverNativeWorkflows() {
  const auto path = nativeCaptureWorkflowPath();
  if (path.isEmpty()) return xerahs::workflows::workflowDiscoveryError("workflow-storage-unavailable");
  xerahs::editor::FilePreferenceStorage storage(path);
  xerahs::workflows::CaptureWorkflowStore store(storage);
  return store.discovery(xerahs::workflows::implementedCaptureActions());
}
} // namespace xerahs::app
