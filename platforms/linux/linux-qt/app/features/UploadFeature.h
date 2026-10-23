// CORE-007, CORE-020, CORE-024: upload file, destinations and watch folders.
#pragma once
namespace xerahs::app {
class ApplicationWindow;
struct AppContext;
void installUpload(AppContext &ctx);
void attachUploadWindow(AppContext &ctx, ApplicationWindow &window);
}  // namespace xerahs::app
