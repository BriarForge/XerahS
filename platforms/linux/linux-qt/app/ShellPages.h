#pragma once

namespace xerahs::app {

struct AppContext;
class ApplicationWindow;

// Installs all 10 embedded reference-UI pages into ApplicationWindow:
// Editor, Recording, History, Statistics, Workflows, Tools,
// Application Settings, Destination Settings, Debug, and About.
// Every page is backed by the native AppContext stores and commands.
void installShellPages(AppContext &ctx, ApplicationWindow &window);

}  // namespace xerahs::app
