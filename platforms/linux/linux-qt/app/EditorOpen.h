// Native worker lifetime, progress/cancellation and explicit source selection.
#pragma once
#include "EditorSource.h"
class QWidget;
namespace xerahs::app {
LoadedSource openSource(const QString &path, QWidget *parent = nullptr);
LoadedSource openCapturedSource(const QImage &image, QWidget *parent = nullptr);
QString imageFileFilter();
// Preserve POSIX symlink/parent-segment resolution when making CLI paths absolute.
QString absoluteImagePath(const QString &path);
}  // namespace xerahs::app
