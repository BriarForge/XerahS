// CORE-007, EU-001: capture commands, activation dispatch and retained-capture actions.
#pragma once

#include "shell/ActivationDispatcher.h"
#include "workflows/CaptureWorkflows.h"

#include <QString>

#include <functional>
#include <memory>

class QMenu;

namespace xerahs::app {

class ApplicationWindow;
struct AppContext;

xerahs::workflows::WorkflowChoice resolveShellWorkflow(AppContext &ctx, const QUuid &id = {});
QString activationFailureMessage(const QString &diagnostic);
std::unique_ptr<xerahs::shell::ActivationDispatcher> createShellDispatcher(AppContext &ctx);
std::function<void()> prepareShellCapture(AppContext &ctx, const xerahs::shell::ActivationRequest &incoming);
void installCapture(AppContext &ctx);
void installRetainedCapture(AppContext &ctx, ApplicationWindow &window, QMenu &tray);

}  // namespace xerahs::app
