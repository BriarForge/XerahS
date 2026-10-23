// CORE-007: capture workflows and session shortcut commands.
#pragma once
#include "workflows/WorkflowShortcuts.h"
namespace xerahs::app {
struct AppContext;
xerahs::workflows::WorkflowShortcutPlan shortcutPlan(AppContext &ctx);
void installWorkflows(AppContext &ctx);
}  // namespace xerahs::app
