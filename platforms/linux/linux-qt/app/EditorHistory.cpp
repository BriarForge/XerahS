#include "EditorHistory.h"
#include "EditorPreparation.h"
#include <memory>
namespace xerahs::app {
LoadedSource runEditorHistory(const xerahs::editor::AnnotationDocument &document, QWidget *parent, const EditorHistoryHooks &hooks) {
  try {
    auto source = std::make_shared<LoadedSource>();
    const auto result = runEditorPreparation(parent, {"editorHistoryProgress", "Restore editor history", "Restoring canvas history…", "Restore editor history progress"},
        [document, hooks, source](const xerahs::editor::RenderControl &control) {
          if (hooks.beforePrepare) hooks.beforePrepare();
          *source = prepareCapturedDocument(document, control);
        });
    if (result == EditorPreparationResult::Ready) return std::move(*source);
    LoadedSource failure; failure.diagnostic = result == EditorPreparationResult::Cancelled ? "canvas-cancelled" : "canvas-history-failed"; return failure;
  } catch (...) { LoadedSource failure; failure.diagnostic = "canvas-history-failed"; return failure; }
}
} // namespace xerahs::app
