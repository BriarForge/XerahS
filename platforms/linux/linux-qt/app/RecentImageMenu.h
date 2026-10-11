// Native controls over the opt-in recent-path model. Persistence follows in
// EDITOR-SETTINGS-001; this host retains private paths only for this process.
#pragma once
#include "image-editor/RecentImages.h"
#include <functional>
class QMenu;
class QWidget;
namespace xerahs::app {
xerahs::editor::RecentImages &recentImageHistory();
bool manageRecentImages(QWidget *parent = nullptr);
QMenu *addRecentImagesMenu(QMenu *parent, std::function<void(const QString &)> open,
                          std::function<void()> manage);
}  // namespace xerahs::app
