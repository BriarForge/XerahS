#include "features/WorkflowExecutionHistoryFeature.h"
#include "ApplicationControl.h"
#include <QPointer>
namespace xerahs::app {
QAction *installWorkflowExecutionHistory(CommandRegistry &commands, WorkflowHistoryPorts ports,
    std::function<bool()> captureBusy, std::function<QWidget *()> parent, QObject *owner) {
  auto *action = commands.add({"workflow-execution-history", "Workflow execution history…", CommandGroup::Workflows,
      CommandSurface::Tray | CommandSurface::Window | CommandSurface::CaptureMenu,
      "Review durable execution metadata and explicitly forget one record while keeping output files."});
  if (!action || !owner) return action;
  auto window = std::make_shared<QPointer<WorkflowExecutionHistoryDialog>>();
  QObject::connect(action, &QAction::triggered, owner, [window, ports, captureBusy, parent, guard = QPointer<QObject>(owner)] {
    if (!applicationControlAvailability().isEmpty()) return;
    if (*window) { (*window)->show(); if (*window) (*window)->raise(); if (*window) (*window)->activateWindow(); return; }
    QWidget *widget = parent ? parent() : nullptr; if (!guard) return;
    *window = new WorkflowExecutionHistoryDialog(ports, captureBusy, widget); (*window)->show();
  });
  return action;
}
}
