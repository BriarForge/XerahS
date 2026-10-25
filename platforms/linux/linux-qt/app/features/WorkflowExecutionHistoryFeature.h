#pragma once
#include "WorkflowExecutionHistoryDialog.h"
#include "CommandRegistry.h"
namespace xerahs::app {
QAction *installWorkflowExecutionHistory(CommandRegistry &commands, WorkflowHistoryPorts ports,
    std::function<bool()> captureBusy, std::function<QWidget *()> parent, QObject *owner);
}
