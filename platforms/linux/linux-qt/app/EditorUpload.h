#pragma once
#include "actions/PostCapturePipeline.h"
#include "image-editor/AnnotationDocument.h"
#include "image-editor/AnnotationRenderer.h"
#include "uploads/UploadDestination.h"
#include "uploads/CredentialStore.h"
#include <QImage>
#include <functional>
#include <memory>
class QWidget;
namespace xerahs::app {
struct EditorUploadSnapshot { QImage image; QByteArray png; };
struct EditorUploadChoice {
  xerahs::actions::ActionOutcome action{xerahs::actions::ActionState::Cancelled, {}, {}};
  std::optional<xerahs::uploads::UploadDestination> destination;
};
EditorUploadChoice chooseEditorUpload(const EditorUploadSnapshot &, QWidget *, const QString &catalogPath = {});
xerahs::actions::ActionOutcome submitEditorUpload(const EditorUploadSnapshot &, const xerahs::uploads::UploadDestination &,
    const xerahs::editor::RenderControl &, const std::shared_ptr<xerahs::uploads::CredentialStore> &credentials = {});
struct EditorUploadPorts {
  // Worker-only preparation; captures must own data across owner destruction.
  std::function<void()> beforeRender;
  // GUI-only, no external publication. Borrows the snapshot/parent for this call.
  // Success transfers an owned destination frozen by explicit user consent.
  // Missing callbacks disable the action. Reentrant output remains busy.
  std::function<EditorUploadChoice(const EditorUploadSnapshot &, QWidget *)> confirm =
      [](const auto &snapshot, QWidget *parent) { return chooseEditorUpload(snapshot, parent); };
  // Worker-only; no GUI captures. Borrowed arguments last until return. Poll
  // cancellation; progress is 0..100. Exceptions/abandonment may mean a remote
  // effect and cannot authorize automatic retry. Captures outlive the editor.
  std::function<xerahs::actions::ActionOutcome(const EditorUploadSnapshot &, const xerahs::uploads::UploadDestination &,
      const xerahs::editor::RenderControl &)> submit =
      [](const auto &snapshot, const auto &destination, const auto &control) { return submitEditorUpload(snapshot, destination, control); };
};
struct EditorUploadOutcome { xerahs::actions::ActionOutcome action; };
EditorUploadOutcome runEditorUpload(const QImage &, const xerahs::editor::AnnotationDocument &, QWidget *, const EditorUploadPorts & = {});
}
