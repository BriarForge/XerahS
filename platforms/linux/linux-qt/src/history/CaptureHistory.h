// CORE-023: the browsable capture history built from durable pipeline result
// records. Each item keeps the record's stable task ID and the files that run
// created; missing files are tolerated and shown, never silently dropped.
#pragma once

#include "actions/PipelineJournal.h"

#include <QDateTime>
#include <QStringList>

#include <vector>

namespace xerahs::history {

struct CaptureHistoryFile {
  QString path;
  qint64 byteLength = 0;
  qint64 width = 0, height = 0;  // from the filename context, 0 when unknown
};

struct CaptureHistoryItem {
  QString taskId;
  QDateTime createdAt;
  QString state;          // pipeline_state, or "interrupted" for unfinished records
  QString workflowName;   // empty for captures without a saved workflow
  QString captureSource;  // region, full-screen, monitor or empty
  std::vector<CaptureHistoryFile> files;  // created by this run, PNG first
};

// Newest first. Records without a file this run created are left out: their
// pixels were never stored (clipboard-only or failed saves).
std::vector<CaptureHistoryItem> captureHistoryItems(const std::vector<xerahs::actions::RecoveredPipelineJournal> &records);

// Case-insensitive match on file name, folder, workflow name, source and the
// local date (yyyy-MM-dd). An empty query matches everything.
bool matchesHistoryQuery(const CaptureHistoryItem &item, const QString &query);

// Bounded paging over a filtered list (CORE-023).
constexpr int kHistoryPageSize = 200;
std::vector<CaptureHistoryItem> historyPage(const std::vector<CaptureHistoryItem> &items, const QString &query, int pages,
                                            int *totalMatches = nullptr);

}  // namespace xerahs::history
