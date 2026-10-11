// Native worker lifetime, progress/cancellation and explicit source selection.
#pragma once
#include "EditorSource.h"
class QWidget;
namespace xerahs::app {
LoadedSource openSource(const QString &path, QWidget *parent = nullptr);
QString imageFileFilter();
}  // namespace xerahs::app
