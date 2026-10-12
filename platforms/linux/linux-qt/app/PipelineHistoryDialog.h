#pragma once
#include "NativePipelineJournal.h"
#include <QDialog>
#include <functional>
class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;
namespace xerahs::app {
struct HistoryJob;
class PipelineHistoryDialog final : public QDialog {
public:
  PipelineHistoryDialog(PipelineJournalStorage storage, std::function<bool()> captureBusy, QWidget *parent = nullptr);
private:
  void refresh();
  void poll();
  void updateAvailability();
  void review();
  void forget();
  const JournalEntry *selectedEntry() const;
  PipelineJournalStorage m_storage;
  std::function<bool()> m_captureBusy;
  JournalListing m_listing;
  std::shared_ptr<HistoryJob> m_job;
  QTimer *m_timer;
  QLabel *m_status;
  QTreeWidget *m_table;
  QPushButton *m_refresh, *m_review, *m_forget;
  bool m_confirming = false;
  bool m_reloadWhenIdle = false;
};
} // namespace xerahs::app
