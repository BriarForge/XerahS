#pragma once
#include "EditorCanvasOperations.h"
#include <functional>
class QWidget;
namespace xerahs::app {
struct EditorCanvasHooks {
  // Worker-only; capture owned values that outlive cancellation/owner teardown.
  std::function<void()> beforePrepare;
};
void prepareCanvasDisplay(PreparedCanvasEdit &, const xerahs::editor::CanvasControl & = {});
PreparedCanvasEdit runEditorCanvas(const xerahs::editor::AnnotationDocument &, const QImage &,
    const xerahs::editor::CanvasOperation &, QWidget *, const EditorCanvasHooks & = {});
} // namespace xerahs::app
