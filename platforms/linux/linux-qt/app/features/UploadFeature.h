// CORE-007, CORE-020, CORE-024: upload file, destinations and watch folders.
#pragma once
namespace xerahs::app {
class ApplicationWindow;
struct AppContext;
void drainWatchFolders();
// Stops and retires the process-wide watch service. Intended for teardown
// paths (tests) where the AppContext, window and upload controller that the
// service references are about to go away: the service is parented to the
// application, so it outlives them and must be stopped while they are alive.
void stopWatchFolders();
void installUpload(AppContext &ctx);
void attachUploadWindow(AppContext &ctx, ApplicationWindow &window);
}  // namespace xerahs::app
