#include "NativePipelineJournal.h"
#include "NativePrivateRecords.h"
#include <QDir>
#include <QJsonDocument>
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
JournalWrite NativePipelineJournal::recordFileUpload(const FileUploadHistoryRecord &upload) const {
  using namespace xerahs::actions;
  if ((upload.origin != "file-upload" && upload.origin != "watch-folder") || !QDir::isAbsolutePath(upload.path))
    return {false, QStringLiteral("upload-history-invalid")};
  PipelineJournalSnapshot snapshot;
  snapshot.result.taskId = upload.taskId;
  snapshot.createdAt = upload.createdAt;
  snapshot.selected = {"upload"}; snapshot.terminal = true;
  ActionResult action; action.action = "upload";
  action.state = upload.failureCode.isEmpty() ? ActionState::Succeeded : ActionState::Failed;
  if (!upload.failureCode.isEmpty()) action.diagnostic = upload.failureCode;
  snapshot.result.orderedResults = {action}; snapshot.result.upload = upload.result;
  auto value = QJsonDocument::fromJson(serializePipelineJournal(snapshot)).object();
  auto result = value.value("result").toObject();
  auto extensions = result.value("extensions").toObject();
  auto native = extensions.value("com.xerahs.app").toObject();
  native.insert("file_upload", QJsonObject{{"origin", upload.origin}, {"path", upload.path},
      {"destination_name", upload.destinationName}, {"failure_code", upload.failureCode}});
  extensions.insert("com.xerahs.app", native); result.insert("extensions", extensions); value.insert("result", result);
  return replace(upload.taskId, {}, QJsonDocument(value).toJson(QJsonDocument::Compact));
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
