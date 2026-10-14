#pragma once
#include "actions/PostCapturePipeline.h"
#include "image-editor/AnnotationDocument.h"
#include <QImage>
#include <atomic>
#include <functional>
#include <memory>
class QPrinter;
class QPainter;
class QWidget;
namespace xerahs::app {
struct EditorPrintSnapshot {
  QImage colour, grayscale; // owned sRGB source + visible annotations, alpha on white paper
  QStringList disclosures;
};
using EditorPrintSnapshotPtr = std::shared_ptr<const EditorPrintSnapshot>;
struct EditorPrintChoice {
  xerahs::actions::ActionOutcome action;
  std::shared_ptr<QPrinter> printer; // exclusive use transfers to the submission worker
  double percentOfFit = 100;
};
EditorPrintChoice chooseEditorPrinter(const EditorPrintSnapshot &, QWidget *parent);
bool paintEditorPrintPage(QPainter &, QPrinter &, const EditorPrintSnapshot &, double percentOfFit);
xerahs::actions::ActionOutcome submitEditorPrint(QPrinter &, const EditorPrintSnapshot &, double percentOfFit, const std::atomic<bool> &cancelled);
struct EditorPrintPorts {
  // Worker callback. Captures must outlive the worker after owner destruction.
  std::function<void()> beforeRender;
  // GUI-only, borrowed snapshot/parent. No submission here. Cancel/Failed mean
  // no job was submitted; owner deletion and reentrant calls are allowed.
  // A selected printer must not be retained or used concurrently with submit.
  std::function<EditorPrintChoice(const EditorPrintSnapshot &, QWidget *)> configure = chooseEditorPrinter;
  // Worker-only, borrowed arguments. No GUI references; owned captures may
  // outlive the editor. Cancel/Failed promise no effect. Exceptions or unknown
  // delivery cannot be retried. Cancellation after begin is best effort and
  // cannot prove that a physical job was withdrawn.
  std::function<xerahs::actions::ActionOutcome(QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &)> submit = submitEditorPrint;
};
struct EditorPrintOutcome {
  xerahs::actions::ActionOutcome action{xerahs::actions::ActionState::Failed, QStringLiteral("print-failed"), std::nullopt};
  EditorPrintSnapshotPtr snapshot;
  bool submissionAttempted = false, cancellationArrivedDuring = false, abandoned = false;
};
// Both preparation and submission use owned workers. Dialogs stay GUI-only.
// Before submission, Cancel/destruction has no print effect. During submission,
// owner destruction releases without joining and reports uncertain delivery.
EditorPrintOutcome runEditorPrint(const QImage &, const xerahs::editor::AnnotationDocument &, QWidget *parent,
                                 const EditorPrintPorts &ports = {}, const EditorPrintSnapshotPtr &retry = {});
}
