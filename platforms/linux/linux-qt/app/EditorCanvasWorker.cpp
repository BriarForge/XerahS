#include "EditorCanvasWorker.h"
#include "EditorPreparation.h"
#include <QColorSpace>
#include <memory>
namespace xerahs::app {
void prepareCanvasDisplay(PreparedCanvasEdit &edit, const xerahs::editor::CanvasControl &control) {
  if (edit.result.error || !edit.result.changed) return;
  if (control.cancelled && control.cancelled()) { edit.result.error = "canvas-cancelled"; return; }
  QImage tagged = edit.source;
  if (!tagged.colorSpace().isValid()) tagged.setColorSpace(QColorSpace::SRgb);
  edit.displaySource = tagged.colorSpace() == QColorSpace(QColorSpace::SRgb) ? tagged : tagged.convertedToColorSpace(QColorSpace::SRgb);
  if (edit.displaySource.isNull()) edit.result.error = xerahs::editor::diagnostic::documentTooLarge;
  if (control.cancelled && control.cancelled()) edit.result.error = "canvas-cancelled";
}
PreparedCanvasEdit runEditorCanvas(const xerahs::editor::AnnotationDocument &document, const QImage &source,
    const xerahs::editor::CanvasOperation &operation, QWidget *parent, const EditorCanvasHooks &hooks) {
  PreparedCanvasEdit failure;
  try {
    auto edit = std::make_shared<PreparedCanvasEdit>();
    const auto result = runEditorPreparation(parent, {"canvasImageProgress", "Image operation", "Editing image…", "Image operation progress"},
        [document, source, operation, hooks, edit](const xerahs::editor::RenderControl &control) {
          if (hooks.beforePrepare) hooks.beforePrepare();
          const xerahs::editor::CanvasControl canvasControl{control.cancelled, control.progress};
          *edit = prepareCanvasEdit(document, source, operation, canvasControl); prepareCanvasDisplay(*edit, canvasControl);
        });
    if (result == EditorPreparationResult::Ready) return std::move(*edit);
    failure.result.error = result == EditorPreparationResult::Cancelled ? "canvas-cancelled" : "canvas-preparation-failed";
  } catch (...) { failure.result.error = "canvas-preparation-failed"; }
  return failure;
}
} // namespace xerahs::app
