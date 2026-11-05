#include "features/DiagnosticsFeature.h"
#include "AppContext.h"
#include "DiagnosticsDialog.h"
#include "CaptureController.h"
#include <QApplication>
#include <QPointer>

namespace xerahs::app {
void installDiagnostics(AppContext &ctx) {
  ctx.diagnosticLog = std::make_shared<diagnostics::DiagnosticLog>();
  QObject::connect(&ctx.capture, &CaptureController::retainedResultChanged, &ctx.app, [&ctx] {
    const auto receipt = ctx.capture.lastPipelineResult();
    if (receipt && ctx.diagnosticLog) diagnostics::recordPipelineErrors(*ctx.diagnosticLog, receipt->pipeline);
  });
  auto *action = ctx.commands.add(CommandSpec{"export-diagnostics", QCoreApplication::translate("DiagnosticsFeature", "Export diagnostics…"),
      CommandGroup::Application, CommandSurface::Window | CommandSurface::Tray | CommandSurface::FileMenu});
  auto review = std::make_shared<QPointer<DiagnosticsDialog>>();
  QObject::connect(action, &QAction::triggered, &ctx.app, [&ctx, review] {
    if (QApplication::activeModalWidget()) return;
    if (*review) { (*review)->show(); (*review)->raise(); (*review)->activateWindow(); return; }
    *review = new DiagnosticsDialog(diagnostics::diagnosticBundle(ctx.settings.current(), *ctx.diagnosticLog),
        ctx.dialogParent ? ctx.dialogParent() : nullptr);
    (*review)->show();
  });
}
}
