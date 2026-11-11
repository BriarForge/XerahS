#pragma once
#include "OwnedWork.h"
#include "history/LegacyHistory.h"
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
  explicit LegacyHistoryWindow(QWidget *parent = nullptr, Picker picker = {});
  ~LegacyHistoryWindow() override;
protected:
  void closeEvent(QCloseEvent *event) override;
private:
  void choose();
  void rebuild();
  OwnedWork m_work;
  Picker m_picker;
  history::LegacyHistoryPreview m_preview;
  qsizetype m_page = 0;
  QLabel *m_status;
  QTableWidget *m_table;
  QPushButton *m_choose, *m_previous, *m_next;
};
}
