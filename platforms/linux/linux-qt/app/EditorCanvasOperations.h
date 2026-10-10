// Image codec boundary around the Qt Core canvas engine. Both the native UI
// and thin conformance adapter call this production preparation path.
#pragma once

#include "image-editor/CanvasOperations.h"

#include <QImage>

namespace xerahs::app {

struct PreparedCanvasEdit {
  xerahs::editor::CanvasResult result;
  QImage source;
};

struct CanvasPreview {
  PreparedCanvasEdit edit;
  QImage composite;
};

// A changed result includes its immutable, encoded source PNG. It is ready
// for EditorSession::commitCanvas; failure/cancellation never mutates a session.
PreparedCanvasEdit prepareCanvasEdit(const xerahs::editor::AnnotationDocument &document, const QImage &source,
                                    const xerahs::editor::CanvasOperation &operation,
                                    const xerahs::editor::CanvasControl &control = {});

// The exact prepared edit and authoritative composite displayed by a native
// preview. Neither preparation nor rendering changes the editor session.
CanvasPreview prepareCanvasPreview(const xerahs::editor::AnnotationDocument &document, const QImage &source,
                                   const xerahs::editor::CanvasOperation &operation,
                                   const xerahs::editor::CanvasControl &control = {});

}  // namespace xerahs::app
