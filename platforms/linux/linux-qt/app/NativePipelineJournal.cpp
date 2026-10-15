#include "NativePipelineJournal.h"
#include "NativePrivateRecords.h"
#include <QDir>
#include <algorithm>
namespace xerahs::app {
namespace {
NativePrivateRecords records(const QString &directory) {
  return {directory, [](const QByteArray &bytes, const QString &id) { return bool(xerahs::actions::parsePipelineJournal(bytes, id).record); }};
}
}
QString NativePipelineJournal::defaultDirectory() {
  const auto configured = qEnvironmentVariable("XDG_STATE_HOME");
  const auto state = QDir::isAbsolutePath(configured) ? configured : QDir::homePath() + QStringLiteral("/.local/state");
  return QDir::cleanPath(state + QStringLiteral("/com.xerahs.app/pipelines"));
}
JournalWrite NativePipelineJournal::replace(const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) const {
  return records(m_directory).replace(id, expected, bytes);
}
JournalWrite NativePipelineJournal::erase(const QString &id, const QByteArray &expected) const {
  return records(m_directory).erase(id, expected);
}
JournalListing NativePipelineJournal::list() const {
  auto stored = records(m_directory).list(); JournalListing result; result.diagnostic = stored.diagnostic;
  for (const auto &entry : stored.entries) result.entries.push_back({entry.id, entry.bytes, entry.diagnostic.isEmpty() ?
      xerahs::actions::parsePipelineJournal(entry.bytes, entry.id) : xerahs::actions::PipelineJournalParse{{}, entry.diagnostic}});
  std::sort(result.entries.begin(), result.entries.end(), [](const JournalEntry &left, const JournalEntry &right) {
    const auto a = left.parsed.record ? left.parsed.record->createdAt : QDateTime{};
    const auto b = right.parsed.record ? right.parsed.record->createdAt : QDateTime{};
    return a == b ? left.taskId < right.taskId : a > b;
  });
  return result;
}
} // namespace xerahs::app
