// CORE-023: the user-facing capture history. Thumbnails of saved captures,
// search, bounded paging, and the everyday file actions. The technical
// journal view stays reachable through Details.
// CORE-020, CORE-021: an upload receipt adds Copy URL, Open URL and Copy
// deletion URL beside the file actions.
#pragma once
#include "NativePipelineJournal.h"
#include "history/CaptureHistory.h"
#include <QHash>
#include <QImage>
#include <QSet>
#include <QUrl>
#include <QWidget>
#include <functional>
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;
namespace xerahs::app {
struct CaptureHistoryJob;
class CaptureHistoryWindow final : public QWidget {
public:
  using EditCallback = std::function<void(const QString &path, QWidget *parent)>;
  CaptureHistoryWindow(PipelineJournalStorage storage, std::function<bool()> captureBusy, EditCallback edit, QWidget *parent = nullptr);
  ~CaptureHistoryWindow() override;
  void refresh();
  // The journal view lists unreadable and interrupted records too.
  void setRecordsOpener(std::function<void()> open);
  // Opening an upload URL stays inside the process under test.
  void setUrlOpener(std::function<bool(const QUrl &)> open);
private:
  void poll();
  void rebuild();
  void loadThumbnails();
  void updateAvailability();
  const xerahs::history::CaptureHistoryItem *selectedItem() const;
  QString selectedPath() const;
  QString selectedUploadField(const char *key) const;
  void openFile();
  void editFile();
  void copyImage();
  void copyPath();
  void showInFolder();
  void details();
  void trashFile();
  void forget();
  PipelineJournalStorage m_storage;
  std::function<bool()> m_captureBusy;
  EditCallback m_edit;
  std::function<bool(const QUrl &)> m_openUrl;  // opening an upload URL in the desktop
  std::function<void()> m_technical;
  std::vector<xerahs::history::CaptureHistoryItem> m_items;
  std::vector<xerahs::history::CaptureHistoryItem> m_page;
  QHash<QString, JournalEntry> m_entries;    // task ID to the exact journal record
  QHash<QString, QImage> m_thumbnails;       // path to scaled preview, null when unreadable
  QSet<QString> m_missing;                   // paths found absent on the last read
  std::shared_ptr<CaptureHistoryJob> m_job;
  int m_pages = 1;
  bool m_reloadWhenIdle = false;
  bool m_confirming = false;
  QTimer *m_timer;
  QLineEdit *m_search;
  QListWidget *m_list;
  QLabel *m_status;
  QPushButton *m_open, *m_editButton, *m_copyImage, *m_copyPath, *m_reveal, *m_openUrlButton, *m_copyDeletionUrl, *m_details, *m_trash, *m_forget, *m_more, *m_refresh, *m_records;
};
} // namespace xerahs::app
