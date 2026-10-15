#pragma once
#include "EditorSource.h"
#include <functional>
class QWidget;
namespace xerahs::app {
struct EditorHistoryHooks {
  // Worker-only fixture/port; captured values must outlive cancellation/owner
  // destruction. No GUI APIs or borrowed widget/session references here.
  std::function<void()> beforePrepare;
};
// Prepares an immutable history target without file I/O or history mutation.
// Cancel/child/owner destruction returns without joining the worker. Only its
// source/private display are for publication; Core retains the exact document.
LoadedSource runEditorHistory(const xerahs::editor::AnnotationDocument &, QWidget *parent,
                              const EditorHistoryHooks & = {});
} // namespace xerahs::app
