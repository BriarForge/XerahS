#include "ActivationProtocol.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

namespace xerahs::shell {
namespace {
bool validId(const QString &id) {
  const QUuid uuid(id); return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}
bool validUtf8(const QByteArray &bytes) { return QString::fromUtf8(bytes).toUtf8() == bytes; }
// QJsonDocument accepts duplicate keys using last-value semantics. Count
// top-level members after JSON syntax validation so ambiguous requests/replies
// (including escaped spellings of the same key) fail closed.
bool uniqueObjectKeys(const QByteArray &bytes, const QJsonObject &object) {
  bool quoted = false, escaped = false; int depth = 0, members = 0;
  for (const char byte : bytes) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (byte == '\\') escaped = true;
      else if (byte == '"') quoted = false;
    } else if (byte == '"') quoted = true;
    else if (byte == '{' || byte == '[') ++depth;
    else if (byte == '}' || byte == ']') --depth;
    else if (byte == ':' && depth == 1) ++members;
  }
  return members == object.size();
}
QString actionName(ActivationAction action) {
  switch (action) {
    case ActivationAction::Activate: return QStringLiteral("activate");
    case ActivationAction::CaptureRegion: return QStringLiteral("capture-region");
    case ActivationAction::EditImage: return QStringLiteral("edit-image");
    case ActivationAction::RunWorkflow: return QStringLiteral("run-workflow");
    case ActivationAction::CaptureRegionChooseActions: return QStringLiteral("capture-region-choose-actions");
    case ActivationAction::CaptureFullScreen: return QStringLiteral("capture-full-screen");
    case ActivationAction::CaptureMonitor: return QStringLiteral("capture-monitor");
  }
  return {};
}
bool knownDiagnostic(const QString &value) {
  static const QSet<QString> values{"activation-invalid", "activation-version-unsupported", "activation-busy",
                                   "activation-unavailable", "activation-handler-failed", "activation-peer-denied"};
  return values.contains(value);
}
}
bool startsCapture(ActivationAction action) {
  return action == ActivationAction::CaptureRegion || action == ActivationAction::CaptureRegionChooseActions ||
      action == ActivationAction::RunWorkflow || action == ActivationAction::CaptureFullScreen || action == ActivationAction::CaptureMonitor;
}
ActivationRequest makeActivationRequest(ActivationAction action, const QString &path, const QString &workflowId) {
  return {QUuid::createUuid().toString(QUuid::WithoutBraces), action, path, workflowId};
}
QByteArray serializeActivation(const ActivationRequest &request) {
  QJsonObject value{{"version", 1}, {"id", request.id}, {"action", actionName(request.action)}};
  if (request.action == ActivationAction::EditImage) value.insert(QStringLiteral("path"), request.path);
  if (request.action == ActivationAction::RunWorkflow) value.insert(QStringLiteral("workflow_id"), request.workflowId);
  return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
ParsedActivation parseActivation(const QByteArray &bytes) {
  const auto invalid = [] { return ParsedActivation{{}, QStringLiteral("activation-invalid")}; };
  if (bytes.isEmpty() || bytes.size() > maximumActivationBytes || !validUtf8(bytes)) return invalid();
  const auto document = QJsonDocument::fromJson(bytes);
  if (!document.isObject()) return invalid();
  const auto value = document.object();
  if (!uniqueObjectKeys(bytes, value)) return invalid();
  if (!value.value(u"version").isDouble()) return invalid();
  if (value.value(u"version") != QJsonValue(1)) return {{}, QStringLiteral("activation-version-unsupported")};
  if (!value.value(u"id").isString() || !validId(value.value(u"id").toString()) || !value.value(u"action").isString()) return invalid();
  const auto name = value.value(u"action").toString(); ActivationAction action;
  if (name == u"activate") action = ActivationAction::Activate;
  else if (name == u"capture-region") action = ActivationAction::CaptureRegion;
  else if (name == u"edit-image") action = ActivationAction::EditImage;
  else if (name == u"run-workflow") action = ActivationAction::RunWorkflow;
  else if (name == u"capture-region-choose-actions") action = ActivationAction::CaptureRegionChooseActions;
  else if (name == u"capture-full-screen") action = ActivationAction::CaptureFullScreen;
  else if (name == u"capture-monitor") action = ActivationAction::CaptureMonitor;
  else return invalid();
  QSet<QString> keys{"version", "id", "action"};
  if (action == ActivationAction::EditImage) keys.insert("path");
  if (action == ActivationAction::RunWorkflow) keys.insert("workflow_id");
  for (auto entry = value.begin(); entry != value.end(); ++entry) if (!keys.contains(entry.key())) return invalid();
  QString path;
  if (action == ActivationAction::EditImage) {
    if (!value.value(u"path").isString()) return invalid();
    path = value.value(u"path").toString();
    if (!QDir::isAbsolutePath(path) || path.contains(QChar(0))) return invalid();
  }
  QString workflow;
  if (action == ActivationAction::RunWorkflow) {
    if (!value.value(u"workflow_id").isString() || !validId(value.value(u"workflow_id").toString())) return invalid();
    workflow = value.value(u"workflow_id").toString();
  }
  return {ActivationRequest{value.value(u"id").toString(), action, path, workflow}, {}};
}
QByteArray serializeActivationReply(const ActivationReply &reply) {
  const QString state = reply.state == ActivationReplyState::Accepted ? QStringLiteral("accepted") :
                        reply.state == ActivationReplyState::Busy ? QStringLiteral("busy") : QStringLiteral("rejected");
  QJsonObject value{{"version", 1}, {"id", reply.id}, {"state", state}};
  if (reply.state != ActivationReplyState::Accepted) value.insert(QStringLiteral("error"), knownDiagnostic(reply.diagnostic) ? reply.diagnostic : QStringLiteral("activation-handler-failed"));
  return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
std::optional<ActivationReply> parseActivationReply(const QByteArray &bytes, const QString &expectedId) {
  if (bytes.isEmpty() || bytes.size() > maximumActivationReplyBytes || !validUtf8(bytes)) return {};
  const auto document = QJsonDocument::fromJson(bytes); if (!document.isObject()) return {};
  const auto value = document.object();
  if (!uniqueObjectKeys(bytes, value)) return {};
  if (value.value(u"version") != QJsonValue(1) || value.value(u"id") != QJsonValue(expectedId) || !validId(expectedId)) return {};
  const auto state = value.value(u"state"); ActivationReplyState result;
  if (state == QJsonValue("accepted")) result = ActivationReplyState::Accepted;
  else if (state == QJsonValue("busy")) result = ActivationReplyState::Busy;
  else if (state == QJsonValue("rejected")) result = ActivationReplyState::Rejected;
  else return {};
  const QSet<QString> keys = result == ActivationReplyState::Accepted ? QSet<QString>{"version", "id", "state"} : QSet<QString>{"version", "id", "state", "error"};
  for (auto entry = value.begin(); entry != value.end(); ++entry) if (!keys.contains(entry.key())) return {};
  if (result != ActivationReplyState::Accepted && (!value.value(u"error").isString() || !knownDiagnostic(value.value(u"error").toString()))) return {};
  return ActivationReply{expectedId, result, value.value(u"error").toString()};
}
bool authorizedActivationPeer(quint64 expectedUser, std::optional<quint64> peerUser) {
  return peerUser && *peerUser == expectedUser;
}
QJsonObject activationLaunchResult(const ActivationRequest &request, const QString &delivery, const QString &diagnostic) {
  static const QSet<QString> errors{"usage-invalid", "workflow-id-invalid", "workflow-unavailable", "presentation-required",
      "instance-unavailable", "instance-storage-denied", "instance-endpoint-conflict", "instance-already-started",
      "instance-lock-failed", "instance-listen-failed", "activation-delivery-unknown"};
  QString code = diagnostic;
  if (!code.isEmpty() && !knownDiagnostic(code) && !errors.contains(code)) code = "activation-handler-failed";
  if (code.isEmpty() && delivery != "started" && delivery != "running-instance") code = "activation-handler-failed";
  QJsonObject result{{"schemaVersion", 1}, {"ok", code.isEmpty()}};
  if (validId(request.id)) result.insert("requestId", request.id);
  if (request.action == ActivationAction::RunWorkflow && validId(request.workflowId)) result.insert("workflowId", request.workflowId);
  if (code.isEmpty()) { result.insert("delivery", delivery); result.insert("state", "accepted"); }
  else result.insert("error", QJsonObject{{"code", code}});
  return result;
}
} // namespace xerahs::shell
