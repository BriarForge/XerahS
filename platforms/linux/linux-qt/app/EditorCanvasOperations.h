// Image codec boundary around the Qt Core canvas engine. Both the native UI
// and thin conformance adapter call this production preparation path.
#pragma once

#include "image-editor/CanvasOperations.h"

#include <QImage>

namespace xerahs::app {

struct PreparedCanvasEdit {
  xerahs::editor::CanvasResult result;
};

// A changed result includes its immutable, encoded source PNG. It is ready
// for EditorSession::commitCanvas; failure/cancellation never mutates a session.
PreparedCanvasEdit prepareCanvasEdit(const xerahs::editor::AnnotationDocument &document, const QImage &source,
                                    const xerahs::editor::CanvasOperation &operation,
                                    const xerahs::editor::CanvasControl &control = {});

}  // namespace xerahs::app
