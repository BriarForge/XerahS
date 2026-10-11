// Native controls over the opt-in recent-path model and atomic private store.
#pragma once
#include "NativeEditorPreferences.h"
#include <functional>
class QMenu;
class QWidget;
namespace xerahs::app {
bool manageRecentImages(QWidget *parent = nullptr);
QMenu *addRecentImagesMenu(QMenu *parent, std::function<void(const QString &)> open,
                          std::function<void()> manage);
}  // namespace xerahs::app
