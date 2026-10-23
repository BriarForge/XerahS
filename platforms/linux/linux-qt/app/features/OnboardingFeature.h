// CORE-008: first-run onboarding and the "Setup guide…" command.
#pragma once
namespace xerahs::app {
struct AppContext;
// interactiveLaunch is true only for a plain interactive start (no capture,
// edit, workflow or recording request); it alone may auto-open the guide.
void installOnboarding(AppContext &ctx, bool interactiveLaunch);
}  // namespace xerahs::app
