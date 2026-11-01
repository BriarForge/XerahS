#pragma once
#include "actions/PostCapturePipeline.h"
#include "image-editor/AnnotationDocument.h"
#include <QImage>
#include <functional>

class QWidget;

namespace xerahs::app {
struct EditorPinSnapshot {
  QImage image; // Owned sRGB source and visible supported annotations, with alpha.
  QStringList disclosures;
};
xerahs::actions::ActionOutcome publishEditorPin(const EditorPinSnapshot &);
struct EditorPinPorts {
  // Worker-only. Captures must own their data and outlive an abandoned render.
  std::function<void()> beforeRender;
  // GUI-only. Borrowed snapshot; retain image values if needed. Success means
  // the service owns a pin independent of the editor. Failed/Cancelled promise
  // no effect; an exception means uncertain delivery and must not auto-retry.
  // Owner deletion/reentrant actions are allowed; the editor remains busy.
  // An empty service disables Pin to Screen without selecting a fallback.
  std::function<xerahs::actions::ActionOutcome(const EditorPinSnapshot &)> publish = publishEditorPin;
};
struct EditorPinOutcome {
  xerahs::actions::ActionOutcome action;
  bool publicationAttempted = false;
};
// Freezes value-owned source/document data before an owned preparation worker.
// Cancel/owner destruction before publication creates no pin and never joins
// the worker. Successful native pins survive editor destruction.
EditorPinOutcome runEditorPin(const QImage &, const xerahs::editor::AnnotationDocument &,
                             QWidget *, const EditorPinPorts & = {});
} // namespace xerahs::app
