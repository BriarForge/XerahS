#include "history/CaptureHistory.h"

#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

namespace xerahs::history {

std::vector<CaptureHistoryItem> captureHistoryItems(const std::vector<xerahs::actions::RecoveredPipelineJournal> &records) {
  std::vector<CaptureHistoryItem> items;
  for (const auto &record : records) {
    const QJsonObject native = record.report.value("extensions").toObject().value("com.xerahs.app").toObject();
    CaptureHistoryItem item;
    item.taskId = record.taskId;
    item.createdAt = record.createdAt;
    item.state = record.terminal ? record.report.value("pipeline_state").toString() : QStringLiteral("interrupted");
    const QJsonObject workflow = native.value("capture_workflow").toObject();
    item.workflowName = workflow.value("name").toString();
    item.captureSource = workflow.value("capture").toString();
    for (const auto &value : native.value("file_artifacts").toArray()) {
      const QJsonObject file = value.toObject();
      const QString creator = file.value("creator_task_id").toString();
      // Retries list earlier files too; each file belongs to the run that made it.
      if (!creator.isEmpty() && creator != record.taskId) continue;
      if (file.value("path").toString().isEmpty()) continue;
      const QJsonObject context = file.value("filename_context").toObject();
      item.files.push_back({file.value("path").toString(), qint64(file.value("byte_length").toDouble()),
                            qint64(context.value("width").toDouble()), qint64(context.value("height").toDouble())});
    }
    std::stable_partition(item.files.begin(), item.files.end(),
                          [](const CaptureHistoryFile &f) { return f.path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive); });
    if (!item.files.empty()) items.push_back(std::move(item));
  }
  std::stable_sort(items.begin(), items.end(), [](const CaptureHistoryItem &a, const CaptureHistoryItem &b) {
    return a.createdAt > b.createdAt;
  });
  return items;
}

bool matchesHistoryQuery(const CaptureHistoryItem &item, const QString &query) {
  const QString needle = query.trimmed();
  if (needle.isEmpty()) return true;
  const auto has = [&](const QString &text) { return text.contains(needle, Qt::CaseInsensitive); };
  if (has(item.workflowName) || has(item.captureSource) || has(item.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"))))
    return true;
  return std::any_of(item.files.begin(), item.files.end(), [&](const CaptureHistoryFile &file) { return has(file.path); });
}

std::vector<CaptureHistoryItem> historyPage(const std::vector<CaptureHistoryItem> &items, const QString &query, int pages,
                                            int *totalMatches) {
  std::vector<CaptureHistoryItem> page;
  const std::size_t limit = std::size_t(std::max(1, pages)) * kHistoryPageSize;
  int matches = 0;
  for (const auto &item : items) {
    if (!matchesHistoryQuery(item, query)) continue;
    ++matches;
    if (page.size() < limit) page.push_back(item);
  }
  if (totalMatches) *totalMatches = matches;
  return page;
}

}  // namespace xerahs::history
