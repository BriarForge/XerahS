#include "WorkflowExecution.h"
#include "shell/ErrorPresentation.h"
#include "actions/PipelineResultJson.h"
#include <cmath>
#include "storage/JsonBoundary.h"
#include <QJsonDocument>
#include <algorithm>
namespace xerahs::workflows {
namespace {
QStringList captureStepActions() {
  QStringList steps; for (const auto &source : implementedCaptureSources()) steps.append("capture-" + source); return steps;
}
bool terminal(const QString &state) {
  return QStringList{"succeeded", "partial", "cancelled", "permission-denied", "failed", "not-started", "interrupted", "skipped", "validation-failure", "unavailable-dependency", "transient-failure", "permanent-failure"}.contains(state);
}
bool safeDiagnostic(const QString &code) {
  return QStringList{"", "capture-source-failed", "capture-permission-denied", "capture-cancelled", "display-topology-changed",
      "workflow-not-started", "workflow-interrupted", "workflow-storage-unavailable", "action-failed",
      "encoder-unavailable", "encoder-start-failed", "encoder-failed", "request-invalid", "portal-unavailable", "portal-timeout", "portal-invalid-response", "target-lost", "finalize-timeout", "output-create-failed", "output-publish-failed", "region-outside-stream", "region-mapping-unavailable"}.contains(code);
}
bool recordingSnapshot(const QJsonObject &snapshot) { return snapshot.value("capture") == "recording"; }
bool validSnapshot(const QJsonObject &snapshot) {
  if (recordingSnapshot(snapshot)) {
    // Full request is frozen privately; projection never exposes paths, device
    // IDs, names or region coordinates. Version 1 was the earlier small record.
    if (snapshot.value("definitionVersion") == 1) {
      const QStringList keys{"definitionVersion", "id", "capture", "format", "directory", "audio", "region"};
      if (snapshot.size() != keys.size() || !snapshot.value("audio").isBool() || !snapshot.value("region").isBool()) return false;
    } else if (snapshot.value("definitionVersion") == 2) {
      const QStringList keys{"definitionVersion", "id", "capture", "format", "directory", "filename", "target", "frameRate", "quality", "cursor", "systemAudioDevice", "microphoneDevice", "region", "crop", "streamSize"};
      if (snapshot.size() != keys.size() || !snapshot.value("cursor").isBool() ||
          !QStringList{"monitor", "window"}.contains(snapshot.value("target").toString())) return false;
      for (const auto *key : {"filename", "systemAudioDevice", "microphoneDevice"})
        if (!snapshot.value(key).isString() || snapshot.value(key).toString().size() > 32768 || snapshot.value(key).toString().contains(QChar::Null)) return false;
      for (const auto *key : {"frameRate", "quality"}) {
        const auto number = snapshot.value(key);
        if (!number.isDouble() || !std::isfinite(number.toDouble()) || number.toDouble() != number.toInt()) return false;
      }
      for (const auto *key : {"region", "crop", "streamSize"}) {
        const auto value = snapshot.value(key);
        if (value.isNull() && QLatin1String(key) != "streamSize") continue;
        if (!value.isArray() || value.toArray().size() != (QLatin1String(key) == "streamSize" ? 2 : 4)) return false;
        for (const auto &number : value.toArray()) if (!number.isDouble() || !std::isfinite(number.toDouble())) return false;
      }
    } else return false;
    if (!xerahs::actions::validJournalTaskId(snapshot.value("id").toString()) ||
        !QStringList{"mp4", "webm", "gif"}.contains(snapshot.value("format").toString()) ||
        !snapshot.value("directory").isString() || snapshot.value("directory").toString().size() > 32768 || snapshot.value("directory").toString().contains(QChar::Null)) return false;
    return true;
  }
  auto definition = snapshot; definition.remove("definitionVersion");
  QJsonObject root{{"format", "com.xerahs.app/linux-qt/capture-workflows"}, {"version", 2},
      {"defaultId", snapshot.value("id")}, {"workflows", QJsonArray{definition}}};
  const auto parsed = parseCaptureWorkflows(QJsonDocument(root).toJson(QJsonDocument::Compact));
  // Records describe past settings, not permission to execute them now. Keep
  // structural validation independent of the current native action catalogue;
  // capture admission still checks workflowAvailability before creating work.
  return parsed.catalog && parsed.catalog->entries.size() == 1 && workflowSnapshot(parsed.catalog->entries[0]) == snapshot;
}
QString date(const QDateTime &value) { return value.toUTC().toString(Qt::ISODateWithMs); }
QJsonObject object(const WorkflowExecutionRecord &r, int version = 2) {
  QJsonObject result{{"format", "com.xerahs.app/linux-qt/workflow-execution"}, {"version", version}, {"id", r.id}, {"origin", r.origin},
      {"state", r.state}, {"startedAt", date(r.startedAt)}, {"finishedAt", r.finishedAt.isValid() ? date(r.finishedAt) : QString{}},
      {"settings", r.settings}, {"pipelineId", r.pipelineId}, {"diagnostic", r.diagnostic}, {"steps", r.steps}};
  if (!r.outputs.isEmpty()) result.insert("outputs", QJsonArray::fromStringList(r.outputs));
  return result;
}
}
QByteArray serializeWorkflowExecution(const WorkflowExecutionRecord &r) { return QJsonDocument(object(r)).toJson(QJsonDocument::Compact); }
std::optional<WorkflowExecutionRecord> parseWorkflowExecution(const QByteArray &bytes, const QString &id) {
  if (QUuid(id).isNull() || !xerahs::actions::validJournalTaskId(id) || bytes.size() > xerahs::actions::maximumJournalBytes) return {};
  const auto parsed = xerahs::compatibility::readJson(bytes, {xerahs::actions::maximumJournalBytes, 64, 100000});
  if (!parsed.ok() || !parsed.document.isObject()) return {};
  const auto o = parsed.document.object();
  if (o.value("format") != "com.xerahs.app/linux-qt/workflow-execution" || (o.value("version") != 1 && o.value("version") != 2) || o.value("id") != id) return {};
  WorkflowExecutionRecord r; r.id = id;
  for (const auto *key : {"origin", "state", "startedAt", "finishedAt", "pipelineId", "diagnostic"}) if (!o.value(QLatin1String(key)).isString()) return {};
  r.origin = o.value("origin").toString(); r.state = o.value("state").toString(); r.pipelineId = o.value("pipelineId").toString(); r.diagnostic = o.value("diagnostic").toString();
  r.startedAt = QDateTime::fromString(o.value("startedAt").toString(), Qt::ISODateWithMs);
  r.finishedAt = QDateTime::fromString(o.value("finishedAt").toString(), Qt::ISODateWithMs);
  r.settings = o.value("settings").toObject(); r.steps = o.value("steps").toArray();
  if (!QStringList{"native-shell", "global-shortcut", "external-activation", "command-line"}.contains(r.origin) ||
      (!terminal(r.state) && !QStringList{"admitted", "capturing", "processing"}.contains(r.state)) || !r.startedAt.isValid() ||
      terminal(r.state) != r.finishedAt.isValid() || (r.finishedAt.isValid() && r.finishedAt < r.startedAt) ||
      !safeDiagnostic(r.diagnostic) || !validSnapshot(r.settings) || !o.value("steps").isArray() || r.steps.size() > 64 ||
      (!r.pipelineId.isEmpty() && !xerahs::actions::validJournalTaskId(r.pipelineId)) ||
      ((r.state == "processing" || r.state == "succeeded" || r.state == "partial") && r.pipelineId.isEmpty() && !recordingSnapshot(r.settings))) return {};
  for (const auto &step : r.steps) {
    const auto entry = step.toObject();
    const auto failure = entry.value("failure_class");
    if ((entry.size() != 2 && entry.size() != 3) ||
        (entry.size() == 3 && (o.value("version") != 2 || !xerahs::actions::parseFailureClass(failure.toString()) || entry.value("state") != "failed")) || !entry.value("action").isString() || !entry.value("state").isString() ||
        (!captureStepActions().contains(entry.value("action").toString()) && !xerahs::actions::catalogueIndex(entry.value("action").toString()) &&
         !(recordingSnapshot(r.settings) && QStringList{"capture-recording", "start-recording", "pause-recording", "resume-recording", "stop-recording", "abort-recording"}.contains(entry.value("action").toString()))) ||
        !QStringList{"succeeded", "failed", "cancelled", "skipped", "permission-denied"}.contains(entry.value("state").toString())) return {};
  }
  if (o.contains("outputs")) {
    if (o.value("version") != 2 || !recordingSnapshot(r.settings) || !terminal(r.state) || !o.value("outputs").isArray() || o.value("outputs").toArray().size() != 1) return {};
    const auto output = o.value("outputs").toArray().first();
    if (!output.isString() || !((r.state == "succeeded" && output == r.id + "/recording.file") ||
        (r.state != "succeeded" && r.state != "cancelled" && output == r.id + "/recording.partial"))) return {};
    r.outputs = {output.toString()};
  }
  if (object(r, o.value("version").toInt()) != o) return {}; // Unknown fields cannot enter a query/report.
  return r;
}
QJsonObject workflowExecutionError(const QString &code) {
  const auto safe = xerahs::shell::isStableErrorCode(code, xerahs::shell::ExecutionSurface) ? code : QStringLiteral("workflow-storage-unavailable");
  return {{"schemaVersion", 1}, {"ok", false}, {"error", QJsonObject{{"code", safe}}}};
}
QJsonObject workflowExecutionResult(const WorkflowExecutionRecord &r) {
  if (!parseWorkflowExecution(serializeWorkflowExecution(r), r.id)) return workflowExecutionError("workflow-execution-invalid");
  const bool ended = terminal(r.state);
  QJsonObject result{{"schemaVersion", 1}, {"ok", true}, {"requestId", r.id}, {"workflowId", r.settings.value("id")},
      {"origin", r.origin}, {"startedAt", date(r.startedAt)}, {"terminal", ended}, {"runtimeStatus", "not-queried"},
      {"state", ended ? r.state : QStringLiteral("outcome-unconfirmed")}, {"lastRecordedState", r.state}, {"steps", r.steps}};
  if (ended) result.insert("finishedAt", date(r.finishedAt));
  if (recordingSnapshot(r.settings)) result.insert("outputs", QJsonArray::fromStringList(r.outputs));
  if (!r.pipelineId.isEmpty()) result.insert("pipelineTaskId", r.pipelineId);
  if (!r.diagnostic.isEmpty()) result.insert("diagnostic", r.diagnostic);
  return result;
}
WorkflowExecution::WorkflowExecution(QString id, QString origin, const CaptureWorkflow &effective, Writer writer) : m_writer(std::move(writer)) {
  m_record.id = std::move(id); m_record.origin = std::move(origin); m_record.startedAt = QDateTime::currentDateTimeUtc();
  m_record.settings = workflowSnapshot(effective);
}
WorkflowExecution::WorkflowExecution(QString id, QString origin, QJsonObject effectiveSettings, Writer writer) : m_writer(std::move(writer)) {
  m_record.id = std::move(id); m_record.origin = std::move(origin); m_record.startedAt = QDateTime::currentDateTimeUtc();
  m_record.settings = std::move(effectiveSettings);
}
bool WorkflowExecution::step(const QString &action, const QString &state, const QString &diagnostic) {
  if (!m_expected || terminal(m_record.state) || !recordingSnapshot(m_record.settings)) return false;
  QJsonObject fact{{"action", action}, {"state", state}};
  if (state == "failed") fact.insert("failure_class", xerahs::actions::failureClassName(xerahs::actions::classifyFailure(diagnostic)));
  m_record.steps.append(fact); return persist();
}
WorkflowExecution::~WorkflowExecution() {
  // Admission which never reaches dispatch must not masquerade as a capture.
  // In-flight destruction/restart leaves the last durable observation unknown.
  if (m_expected && m_record.state == "admitted") { try { finish("not-started", "workflow-not-started"); } catch (...) {} }
}
bool WorkflowExecution::persist() {
  if (m_failed || !m_writer) return false;
  const auto bytes = serializeWorkflowExecution(m_record);
  try { if (!parseWorkflowExecution(bytes, m_record.id) || !m_writer(m_record.id, m_expected, bytes).stored) { m_failed = true; return false; } }
  catch (...) { m_failed = true; return false; }
  m_expected = bytes; return true;
}
bool WorkflowExecution::admit() { return !m_expected && m_record.state == "admitted" && persist(); }
bool WorkflowExecution::start() {
  if (!m_expected || m_record.state != "admitted") return false;
  m_record.state = "capturing"; return persist();
}
bool WorkflowExecution::processing(const QString &pipelineId) {
  if (m_record.state != "capturing") return false;
  m_record.pipelineId = pipelineId; m_record.state = "processing";
  m_record.steps.append(QJsonObject{{"action", captureStep()}, {"state", "succeeded"}}); return persist();
}
bool WorkflowExecution::finish(const QString &state, const QString &diagnostic, const QStringList &outputs) {
  if (!m_expected || terminal(m_record.state) || !terminal(state)) return false;
  if (m_record.steps.isEmpty() && state != "not-started") m_record.steps.append(QJsonObject{{"action", captureStep()},
      {"state", state == "permission-denied" || state == "cancelled" ? state : QStringLiteral("failed")}});
  m_record.outputs = outputs;
  m_record.state = state; m_record.diagnostic = safeDiagnostic(diagnostic) ? diagnostic : QStringLiteral("action-failed");
  m_record.finishedAt = std::max(m_record.startedAt, QDateTime::currentDateTimeUtc()); return persist();
}
bool WorkflowExecution::complete(const xerahs::actions::PipelineResult &pipeline) {
  using namespace xerahs::actions;
  if (m_record.state != "processing" || pipeline.taskId != m_record.pipelineId) return false;
  for (const auto &step : pipeline.orderedResults) {
    if (!catalogueIndex(step.action)) return false;
    QJsonObject fact{{"action", step.action}, {"state", actionStateName(step.state)}};
    if (step.failureClass) fact.insert("failure_class", failureClassName(*step.failureClass));
    m_record.steps.append(fact);
  }
  return finish(pipelineTerminalState(pipeline),
      pipeline.state == PipelineState::Interrupted ? QStringLiteral("action-failed") : QString{});
}
} // namespace xerahs::workflows
