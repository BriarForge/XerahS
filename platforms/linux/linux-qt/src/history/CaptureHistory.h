// CORE-023: the browsable capture history built from durable pipeline result
// records. Each item keeps its stable task ID and created files or uploaded
// source file; missing files are shown, never silently dropped.
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
  QString outcome;        // validated terminal observation; completed does not imply success
  QString workflowName;   // empty for captures without a saved workflow
  QString captureSource;  // region, full-screen, monitor or empty
  QString origin;  // empty for captures; file-upload or watch-folder for existing files
  QString destinationName;
  QString failureMessage;  // published upload_result error_message; empty when the record has none
  QString resultUrl;       // public upload_result url only; deletion and thumbnail URLs are not copied
  QString sourceLabel;     // visible source name supplied by the window; empty in stored records
  bool uploadOnly() const { return !origin.isEmpty(); }
  std::vector<CaptureHistoryFile> files;  // capture-created files (PNG first), or one uploaded source file
};

// Newest first, including results with no acknowledged file. Upload-only items
// retain a reference to the existing source file. No output is reconstructed.
std::vector<CaptureHistoryItem> captureHistoryItems(const std::vector<xerahs::actions::RecoveredPipelineJournal> &records);

// Case-insensitive match on stable task ID, state/outcome, file name, folder, workflow name,
// capture source (a hyphen matches the space in the visible label), the window-supplied source
// label, upload origin/destination, the public result URL, the published failure message, and the
// local date and time (yyyy-MM-dd HH:mm). An empty query matches everything.
bool matchesHistoryQuery(const CaptureHistoryItem &item, const QString &query);

// Bounded paging over a filtered list (CORE-023).
constexpr int kHistoryPageSize = 200;
std::vector<CaptureHistoryItem> historyPage(const std::vector<CaptureHistoryItem> &items, const QString &query, int pages,
                                            int *totalMatches = nullptr);

// Search text beyond this many characters is ignored so a pasted blob cannot make every match expensive.
constexpr int kHistoryMaxQueryLength = 256;

// Keyset pagination: the position after the last item a viewer has seen. It is independent of how many
// records were added since, so a new capture never shifts, repeats or skips an item on later pages.
struct HistoryCursor {
  QDateTime createdAt;
  QString taskId;
  bool valid() const { return createdAt.isValid(); }
};
struct HistoryPageResult {
  std::vector<CaptureHistoryItem> items;
  HistoryCursor next;   // valid only when more matches follow
  int totalMatches = 0;
};
// Items strictly after `after` (all items when `after` is invalid) in the deterministic order newest first,
// ties by task ID; at most min(limit, kHistoryPageSize) items.
HistoryPageResult historyPageAfter(const std::vector<CaptureHistoryItem> &items, const QString &query,
                                   const HistoryCursor &after = {}, int limit = kHistoryPageSize);

}  // namespace xerahs::history
