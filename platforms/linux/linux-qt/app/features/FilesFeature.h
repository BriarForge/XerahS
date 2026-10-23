// CORE-007: new/open image, captures folder, history, and the recent-images submenu.
#pragma once
class QAction;
class QMenu;
namespace xerahs::app {
struct AppContext;
void installFiles(AppContext &ctx);
// Moves the recent-images submenu to sit immediately before `before` (Open captures folder).
void insertRecentImagesMenu(QMenu &menu, QAction *before);
}  // namespace xerahs::app
