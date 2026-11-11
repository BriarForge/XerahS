#pragma once
#include "OwnedWork.h"
#include "history/LegacyHistoryArchive.h"
#include <QDialog>
#include <functional>
class QLabel;
class QPushButton;
class QTableWidget;
class QCloseEvent;
namespace xerahs::app {
class LegacyHistoryWindow final : public QDialog {
public:
  using Picker = std::function<QString(QWidget *)>;
  explicit LegacyHistoryWindow(QWidget *parent = nullptr, Picker picker = {}, QString archivePath = {});
  ~LegacyHistoryWindow() override;
protected:
  void closeEvent(QCloseEvent *event) override;
private:
  void choose();
  void loadArchive();
  void importSelected();
  void undoImport();
  void reviewRecovery();
  void removeRecovery();
  void writeArchive(std::function<QString(const history::LegacyHistoryArchive &, const OwnedWork::Context &)> change);
  void setBusy(bool busy);
  void rebuild();
  OwnedWork m_work;
  Picker m_picker;
  QString m_archivePath;
  history::LegacyHistoryArchiveRead m_archive;
  std::optional<history::LegacyHistorySnapshot> m_source;
  history::LegacyHistoryPreview m_preview;
  bool m_savedView = true;
  qsizetype m_page = 0;
  QLabel *m_status;
  QTableWidget *m_table;
  QPushButton *m_choose, *m_saved, *m_import, *m_undo, *m_recover, *m_removeRecovery, *m_previous, *m_next;
};
}
