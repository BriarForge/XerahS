#pragma once
#include "actions/PostCapturePipeline.h"
#include "image-editor/AnnotationDocument.h"
#include <QImage>
#include <functional>
#include <memory>
class QIODevice;
class QWidget;
namespace xerahs::app {
struct EditorCopyPayload {
  QImage image; // owned sRGB composite, including visible supported annotations
  QByteArray png; // interoperable alpha-preserving representation of that composite
  QStringList disclosures;
};
using EditorCopyPayloadPtr = std::shared_ptr<const EditorCopyPayload>;
xerahs::actions::ActionOutcome publishEditorClipboard(const EditorCopyPayload &payload);
struct EditorCopyPorts {
  // Worker-only fixture boundaries. Borrowed arguments expire at call return;
  // captures must outlive the worker even if its owner is destroyed. No GUI APIs.
  std::function<void()> beforeRender;
  std::function<bool(const QImage &, QIODevice *)> encode;
  // GUI-thread publication, borrowed payload for this call. Retain owned copies
  // if needed. Failed/Cancelled mean no effect; outcomeUnknown means delivery is uncertain.
  // Exceptions may follow an effect and become unknown, never retryable failure.
  // Owner deletion is allowed. Reentrant document/output actions remain busy.
  // An empty provider disables Copy; it does not select a silent fallback.
  std::function<xerahs::actions::ActionOutcome(const EditorCopyPayload &)> publish = publishEditorClipboard;
};
struct EditorCopyOutcome {
  xerahs::actions::ActionOutcome action{xerahs::actions::ActionState::Failed, QStringLiteral("clipboard-failed"), std::nullopt};
  EditorCopyPayloadPtr payload;
  bool publicationAttempted = false;
  bool cancellationArrivedDuring = false;
  bool abandoned = false;
};
// Source must own its pixels. Value-owned document and Qt image copies freeze
// one snapshot. Rendering/PNG encoding occur on a worker; publication is GUI-only.
// Cancel or owner destruction before publication cannot change the clipboard.
// Owner destruction releases without joining; the worker retains no GUI refs.
EditorCopyOutcome runEditorCopy(const QImage &source, const xerahs::editor::AnnotationDocument &document,
                               QWidget *parent, const EditorCopyPorts &ports = {}, const EditorCopyPayloadPtr &retry = {});
} // namespace xerahs::app
