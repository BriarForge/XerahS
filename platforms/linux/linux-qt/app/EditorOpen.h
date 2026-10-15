// Native worker lifetime, progress/cancellation and explicit source selection.
#pragma once
#include "EditorSource.h"
#include <functional>
class QWidget;
namespace xerahs::app {
// Worker-only hooks must own their captures beyond cancellation/owner teardown.
struct EditorOpenHooks { std::function<void()> beforePrepare; };
LoadedSource openSource(const QString &path, QWidget *parent = nullptr, const EditorOpenHooks &hooks = {});
LoadedSource openCapturedSource(const QImage &image, QWidget *parent = nullptr, const EditorOpenHooks &hooks = {});
LoadedSource openCapturedDocument(const xerahs::editor::AnnotationDocument &document, QWidget *parent = nullptr, const EditorOpenHooks &hooks = {});
QString imageFileFilter();
// Preserve POSIX symlink/parent-segment resolution when making CLI paths absolute.
QString absoluteImagePath(const QString &path);
}  // namespace xerahs::app
