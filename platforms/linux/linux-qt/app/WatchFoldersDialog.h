// CORE-024, CORE-025: explicit watch-root and action consent.
#pragma once
#include <QDialog>
namespace xerahs::app {
class WatchFolderService;
class WatchFoldersDialog final : public QDialog {
  Q_OBJECT
public:
  explicit WatchFoldersDialog(WatchFolderService &service, QWidget *parent = nullptr);
};
}
