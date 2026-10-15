#pragma once
#include "image-editor/AnnotationRenderer.h"
#include <QString>
#include <functional>
class QWidget;
namespace xerahs::app {
struct EditorPreparationUi { QString name, title, label, accessibleName; };
enum class EditorPreparationResult { Ready, Cancelled, Failed };
// Native-only wait. The callable and all its captures must own worker data;
// cancellation/child/owner destruction returns without joining that work.
EditorPreparationResult runEditorPreparation(QWidget *, const EditorPreparationUi &,
    std::function<void(const xerahs::editor::RenderControl &)>);
} // namespace xerahs::app
