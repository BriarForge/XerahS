// Qt Core-only native activation validation. This candidate internal protocol
// does not replace approved public CLI/MCP schema or compatibility review.
#pragma once
#include <QByteArray>
#include <QString>
#include <optional>

namespace xerahs::shell {
constexpr qsizetype maximumActivationBytes = 65536;
constexpr qsizetype maximumActivationReplyBytes = 4096;
enum class ActivationAction { Activate, CaptureRegion, EditImage };
struct ActivationRequest { QString id; ActivationAction action = ActivationAction::Activate; QString path; };
enum class ActivationReplyState { Accepted, Busy, Rejected };
struct ActivationReply { QString id; ActivationReplyState state = ActivationReplyState::Rejected; QString diagnostic; };
struct ParsedActivation { std::optional<ActivationRequest> request; QString diagnostic; };
ActivationRequest makeActivationRequest(ActivationAction action, const QString &path = {});
QByteArray serializeActivation(const ActivationRequest &request);
ParsedActivation parseActivation(const QByteArray &bytes);
QByteArray serializeActivationReply(const ActivationReply &reply);
std::optional<ActivationReply> parseActivationReply(const QByteArray &bytes, const QString &expectedId);
bool authorizedActivationPeer(quint64 expectedUser, std::optional<quint64> peerUser);
} // namespace xerahs::shell
