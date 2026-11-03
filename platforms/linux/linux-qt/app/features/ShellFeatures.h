// The one place the shell's feature installers run, in registration order, so
// the application and its tests build the same command registry.
#pragma once
#include <memory>

namespace xerahs::shell { class ActivationDispatcher; }
namespace xerahs::app {
struct AppContext;
// Installs every feature into ctx.commands. The returned dispatcher is also set as ctx.activation.
std::unique_ptr<xerahs::shell::ActivationDispatcher> installShellFeatures(AppContext &ctx, bool showOnboarding);
}  // namespace xerahs::app
