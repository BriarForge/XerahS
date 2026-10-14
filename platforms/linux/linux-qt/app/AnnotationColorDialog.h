#pragma once
#include "image-editor/EditorSession.h"
class QWidget;
namespace xerahs::app {
struct AnnotationColorChoice { std::optional<xerahs::editor::Argb> color; QString diagnostic; };
// Native Qt picker with alpha, visible scope and an owned accepted result.
// Parent/dialog destruction and Cancel never produce an edit value.
AnnotationColorChoice chooseAnnotationColor(xerahs::editor::EditorSession::ColorTarget,
    xerahs::editor::Argb initial, const QString &scope, QWidget *parent);
} // namespace xerahs::app
