// CORE-007: ties the command window to features that need it after registration.
#pragma once
class QMenu;
namespace xerahs::app {
class ApplicationWindow;
struct AppContext;
void attachCommandWindow(AppContext &ctx, ApplicationWindow &window, QMenu &tray);
}  // namespace xerahs::app
