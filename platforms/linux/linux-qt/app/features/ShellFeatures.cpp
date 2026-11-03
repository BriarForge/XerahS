#include "features/ShellFeatures.h"

#include "AppContext.h"
#include "features/ApplicationFeature.h"
#include "features/CaptureFeature.h"
#include "features/FilesFeature.h"
#include "features/OnboardingFeature.h"
#include "features/PreferencesFeature.h"
#include "features/RecordingFeature.h"
#include "features/ScheduleFeature.h"
#include "features/ToolsFeature.h"
#include "features/UploadFeature.h"
#include "features/WorkflowFeature.h"
#include "shell/ActivationDispatcher.h"

namespace xerahs::app {
std::unique_ptr<xerahs::shell::ActivationDispatcher> installShellFeatures(AppContext &ctx, bool showOnboarding) {
  auto dispatcher = createShellDispatcher(ctx);
  installCapture(ctx);
  installSchedule(ctx);
  installRecording(ctx);
  installWorkflows(ctx);
  installTools(ctx);
  installFiles(ctx);
  installUpload(ctx);
  installPreferences(ctx);
  installOnboarding(ctx, showOnboarding);
  installApplication(ctx);
  return dispatcher;
}
}  // namespace xerahs::app
