// CORE-007: settings and editor appearance.
#pragma once
namespace xerahs::app {
struct AppContext;
void installPreferences(AppContext &ctx);
void applyShellVisibility(AppContext &ctx);
}  // namespace xerahs::app
