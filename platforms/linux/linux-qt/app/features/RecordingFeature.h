// CORE-007: recording commands adopted from RecordingActions.
#pragma once
namespace xerahs::app {
struct AppContext;
void installRecording(AppContext &ctx);
void startScreenRecording(AppContext &ctx);
void startRegionRecording(AppContext &ctx);
}  // namespace xerahs::app
