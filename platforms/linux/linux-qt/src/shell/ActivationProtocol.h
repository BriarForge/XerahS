// Qt Core-only native activation validation. This candidate internal protocol
// does not replace approved public CLI/MCP schema or compatibility review.
#pragma once
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <optional>

namespace xerahs::shell {
constexpr qsizetype maximumActivationBytes = 65536;
constexpr qsizetype maximumActivationReplyBytes = 4096;
enum class ActivationAction { Activate, CaptureRegion, EditImage, RunWorkflow, CaptureRegionChooseActions, CaptureFullScreen, CaptureMonitor, StartRecording, StopRecording };
// Every action that starts a capture through the default or a named workflow.
bool startsCapture(ActivationAction action);
struct ActivationRequest { QString id; ActivationAction action = ActivationAction::Activate; QString path; QString workflowId; };
enum class ActivationReplyState { Accepted, Busy, Rejected };
struct ActivationReply { QString id; ActivationReplyState state = ActivationReplyState::Rejected; QString diagnostic; };
struct ParsedActivation { std::optional<ActivationRequest> request; QString diagnostic; };
ActivationRequest makeActivationRequest(ActivationAction action, const QString &path = {}, const QString &workflowId = {});
QByteArray serializeActivation(const ActivationRequest &request);
ParsedActivation parseActivation(const QByteArray &bytes);
QByteArray serializeActivationReply(const ActivationReply &reply);
std::optional<ActivationReply> parseActivationReply(const QByteArray &bytes, const QString &expectedId);
bool authorizedActivationPeer(quint64 expectedUser, std::optional<quint64> peerUser);
// Launch acknowledgement only: no capture/output completion or private fields.
QJsonObject activationLaunchResult(const ActivationRequest &request, const QString &delivery, const QString &diagnostic = {});
} // namespace xerahs::shell
