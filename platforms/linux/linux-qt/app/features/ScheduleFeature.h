// CORE-007, CORE-016: delayed and automatic capture commands.
#pragma once
namespace xerahs::app {
struct AppContext;
void installSchedule(AppContext &ctx);
}  // namespace xerahs::app
