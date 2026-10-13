#include "PipelineJournal.h"
#include "PipelineRetry.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace xerahs::actions {
namespace {
const QString format = QStringLiteral("com.xerahs.app/pipeline-journal");
bool pathValue(const QJsonValue &value) {
  const auto path = value.toString();
  return value.isString() && path.size() <= 32768 && !path.contains(QChar::Null) && QDir::isAbsolutePath(path);
}
bool codeValue(const QJsonValue &value) {
  static const QRegularExpression code(QStringLiteral("^[a-z0-9][a-z0-9.-]{0,127}$"));
  const auto match = code.match(value.toString());
  return value.isString() && match.hasMatch() && match.capturedLength() == value.toString().size();
}
bool timeValue(const QJsonValue &value) {
  return value.isString() && value.toString().size() <= 40 && value.toString().endsWith(QLatin1Char('Z')) &&
      QDateTime::fromString(value.toString(), Qt::ISODateWithMs).isValid();
}
PipelineJournalParse invalid() { return {{}, QStringLiteral("pipeline-journal-invalid")}; }
}
bool validJournalTaskId(const QString &id) {
  static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
  return id.size() == 36 && uuid.match(id).hasMatch();
}
QByteArray serializePipelineJournal(const PipelineJournalSnapshot &snapshot) {
  auto result = snapshot.result;
  if (!snapshot.terminal) result.state = PipelineState::Interrupted;
  result.workingMediaRetained = false;
  QJsonObject value{{"format", format}, {"version", 1}, {"task_id", result.taskId},
      {"created_at", snapshot.createdAt.toUTC().toString(Qt::ISODateWithMs)},
      {"selected_actions", QJsonArray::fromStringList(snapshot.selected)}, {"terminal", snapshot.terminal},
      {"result", pipelineResultJson(result, snapshot.files)}};
  if (snapshot.pendingAction) value.insert("pending_action", *snapshot.pendingAction);
  if (snapshot.pendingSavePath) value.insert("pending_save_path", *snapshot.pendingSavePath);
  return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
PipelineJournalParse parsePipelineJournal(const QByteArray &bytes, const QString &expectedTaskId) {
  if (bytes.size() > maximumJournalBytes || !validJournalTaskId(expectedTaskId)) return invalid();
  QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
  const auto value = document.object();
  if (value.value("format") != format) return invalid();
  if (value.value("version") != QJsonValue(1)) return {{}, QStringLiteral("pipeline-journal-version-unsupported")};
  if (value.value("task_id") != expectedTaskId || !timeValue(value.value("created_at")) ||
      !value.value("terminal").isBool() || !value.value("selected_actions").isArray() || !value.value("result").isObject()) return invalid();
  RecoveredPipelineJournal record;
  record.taskId = expectedTaskId; record.createdAt = QDateTime::fromString(value.value("created_at").toString(), Qt::ISODateWithMs);
  record.terminal = value.value("terminal").toBool(); record.report = value.value("result").toObject();
  int previous = -1;
  for (const auto &entry : value.value("selected_actions").toArray()) {
    const auto index = catalogueIndex(entry.toString());
    if (!entry.isString() || !index || *index <= previous) return invalid();
    previous = *index; record.selected.append(entry.toString());
  }
  const auto state = record.report.value("pipeline_state").toString();
  if (record.terminal ? (state != "completed" && state != "cancelled") : state != "interrupted") return invalid();
  if (record.report.value("working_media_retained") != QJsonValue(false) ||
      !record.report.value("ordered_results").isArray()) return invalid();
  const auto media = record.report.value("final_working_media").toString();
  const auto mediaIndex = catalogueIndex(media);
  if (media != "capture" && (!mediaIndex || catalogue()[size_t(*mediaIndex)].kind != ActionKind::Transformation)) return invalid();
  QSet<QString> completed; previous = -1;
  for (const auto &entry : record.report.value("ordered_results").toArray()) {
    if (!entry.isObject()) return invalid();
    const auto action = entry.toObject(); const auto id = action.value("action").toString(); const auto index = catalogueIndex(id);
    const auto actionState = action.value("state").toString();
    if (!index || *index <= previous || !record.selected.contains(id) ||
        (actionState != "succeeded" && actionState != "failed" && actionState != "cancelled" && actionState != "skipped")) return invalid();
    previous = *index; completed.insert(id);
    if (record.selected.value(completed.size() - 1) != id) return invalid();
    for (const auto *key : {"diagnostic", "missing_artifact", "input_artifact"})
      if (action.contains(key) && !codeValue(action.value(key))) return invalid();
    if (action.contains("cancellation_arrived_during") && !action.value("cancellation_arrived_during").isBool()) return invalid();
  }
  const auto metadata = record.report.value("extensions").toObject().value("com.xerahs.app").toObject();
  if (metadata.value("schema_version") != QJsonValue(1) || metadata.value("task_id") != expectedTaskId ||
      !metadata.value("action_timings").isArray() || !metadata.value("file_artifacts").isArray()) return invalid();
  for (const auto &entry : metadata.value("action_timings").toArray()) {
    const auto timing = entry.toObject();
    if (!entry.isObject() || !completed.contains(timing.value("action").toString())) return invalid();
    for (const auto *key : {"started_at", "finished_at"}) if (timing.contains(key) && !timeValue(timing.value(key))) return invalid();
  }
  for (const auto &entry : metadata.value("file_artifacts").toArray()) {
    const auto file = entry.toObject();
    if (!entry.isObject() || !pathValue(file.value("path")) || !codeValue(file.value("artifact")) ||
        !file.value("media_type").isString() || file.value("media_type").toString().size() > 128 ||
        !file.value("byte_length").isDouble() || file.value("byte_length").toInteger(-1) < 0 ||
        !file.value("filename_context").isObject()) return invalid();
    if (file.contains("creator_task_id") && !validJournalTaskId(file.value("creator_task_id").toString())) return invalid();
  }
  if (metadata.contains("retry")) {
    if (!metadata.value("retry").isObject()) return invalid();
    const auto retry = metadata.value("retry").toObject();
    if (retry.value("schema_version") != QJsonValue(1) || !retry.value("preserved_successful_actions").isArray()) return invalid();
    PipelineRetryLink link{retry.value("original_task_id").toString(), retry.value("previous_task_id").toString(), retry.value("attempt").toInt(), {}};
    if (retry.value("attempt") != QJsonValue(link.attempt)) return invalid();
    for (const auto &id : retry.value("preserved_successful_actions").toArray()) {
      if (!id.isString() || record.selected.contains(id.toString())) return invalid();
      link.preservedSuccessfulActions.append(id.toString());
    }
    if (!validPipelineRetryLink(link, expectedTaskId)) return invalid();
  }
  if (value.contains("pending_action")) {
    const auto id = value.value("pending_action").toString();
    if (!catalogueIndex(id) || record.selected.value(completed.size()) != id || record.terminal) return invalid();
    record.pendingAction = id;
  }
  if (value.contains("pending_save_path")) {
    if ((record.pendingAction != QStringLiteral("save") && record.pendingAction != QStringLiteral("save-as")) || !pathValue(value.value("pending_save_path"))) return invalid();
    record.pendingSavePath = value.value("pending_save_path").toString();
  }
  if (record.terminal && completed.size() != record.selected.size()) return invalid();
  // Recovery never invents an action failure or repeats external work. An
  // unacknowledged network action, like any pending action, has unknown outcome.
  auto extensions = record.report.value("extensions").toObject(); auto native = metadata;
  QJsonObject recovery{{"schema_version", 1}, {"terminal", record.terminal}};
  if (record.pendingAction) recovery.insert("unknown_action", *record.pendingAction);
  if (record.pendingSavePath) recovery.insert("unconfirmed_save_path", *record.pendingSavePath);
  native.insert("recovery", recovery); extensions.insert("com.xerahs.app", native); record.report.insert("extensions", extensions);
  return {std::move(record), {}};
}
PipelineJournalRecorder::PipelineJournalRecorder(std::shared_ptr<const PipelineJournalStoragePort> storage, const QString &id,
                                                 const QStringList &selected, const QDateTime &createdAt,
                                                 const std::optional<PipelineRetryLink> &retry, const std::vector<SavedFileArtifact> &initialFiles, const QString &initialWorkingMedia)
    : m_storage(std::move(storage)) {
  m_snapshot.result.taskId = id; m_snapshot.result.retry = retry; m_snapshot.files = initialFiles; m_snapshot.createdAt = createdAt;
  const auto media = catalogueIndex(initialWorkingMedia);
  if (media && catalogue()[size_t(*media)].kind == ActionKind::Transformation) m_snapshot.result.finalWorkingMedia = initialWorkingMedia;
  for (const auto &action : catalogue()) if (selected.contains(action.id)) m_snapshot.selected.append(action.id);
}
bool PipelineJournalRecorder::begin() {
  return !m_storage || !m_snapshot.result.retry || persist();
}
bool PipelineJournalRecorder::before(const ActionSpec &action) {
  if (!m_storage) return true;
  const bool required = action.id == "save" || action.kind == ActionKind::Network || !m_snapshot.files.empty();
  // Save As may still be cancelled without a destination or durable effect.
  // Once chosen, beforePublish() persists both intent and exact candidate path.
  if (action.id == "save-as" && !required && !m_expected) { m_snapshot.pendingAction = action.id; return true; }
  if (!required && !m_expected) return true;
  m_snapshot.pendingAction = action.id; m_snapshot.pendingSavePath.reset();
  const bool stored = persist();
  // PCA-004/007: after a failed save, independent non-durable clipboard work
  // remains possible. Once a durable file exists, another external effect
  // requires its write-ahead observation to succeed first.
  return stored || !required;
}
bool PipelineJournalRecorder::beforePublish(const QString &path) {
  if (!m_storage) return true;
  m_snapshot.pendingSavePath = path; return persist();
}
void PipelineJournalRecorder::completed(const ActionResult &result, const std::vector<SavedFileArtifact> &files) {
  m_snapshot.result.orderedResults.push_back(result);
  const auto index = catalogueIndex(result.action);
  if (result.state == ActionState::Succeeded && index && catalogue()[size_t(*index)].kind == ActionKind::Transformation)
    m_snapshot.result.finalWorkingMedia = result.action;
  m_snapshot.pendingAction.reset(); m_snapshot.pendingSavePath.reset(); m_snapshot.files = files;
  if (m_expected) persist();
}
void PipelineJournalRecorder::selectionChanged(const QStringList &selected) {
  // The engine supplies its complete effective plan. completed() records the
  // selector and persists both together if a journal already exists.
  m_snapshot.selected = selected;
}
void PipelineJournalRecorder::finish(const PipelineResult &result, const std::vector<SavedFileArtifact> &files) {
  if (!m_expected) return;
  m_snapshot.result = result; m_snapshot.files = files;
  if (result.state != PipelineState::Interrupted) { m_snapshot.pendingAction.reset(); m_snapshot.pendingSavePath.reset(); }
  m_snapshot.terminal = result.state != PipelineState::Interrupted; persist();
}
bool PipelineJournalRecorder::persist() {
  const auto bytes = serializePipelineJournal(m_snapshot);
  const auto written = m_storage->replace(m_snapshot.result.taskId, m_expected, bytes);
  if (written.stored) { m_expected = bytes; return true; }
  m_diagnostic = written.diagnostic; return false;
}
} // namespace xerahs::actions
