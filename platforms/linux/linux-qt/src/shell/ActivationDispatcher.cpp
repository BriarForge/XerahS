#include "ActivationDispatcher.h"
#include <QScopedValueRollback>
#include <utility>

namespace xerahs::shell {
ActivationDispatcher::ActivationDispatcher(ActivationOperations operations) : m_operations(std::move(operations)) {}
bool ActivationDispatcher::available(const ActivationRequest &request) const {
  switch (request.action) {
    case ActivationAction::Activate: return bool(m_operations.activate);
    case ActivationAction::CaptureRegion: return bool(m_operations.prepareCapture) || bool(m_operations.captureRegion);
    case ActivationAction::EditImage: return bool(m_operations.editImage);
    case ActivationAction::RunWorkflow: return bool(m_operations.prepareCapture) || bool(m_operations.prepareWorkflow);
    case ActivationAction::CaptureRegionChooseActions: return bool(m_operations.prepareCapture) || bool(m_operations.captureRegionWithActions);
  }
  return false;
}
bool ActivationDispatcher::blocked(const ActivationRequest &request) const {
  if (request.action == ActivationAction::Activate) return false;
  return m_dispatching || (m_operations.interactionBlocked && m_operations.interactionBlocked()) ||
      ((request.action == ActivationAction::CaptureRegion || request.action == ActivationAction::RunWorkflow ||
        request.action == ActivationAction::CaptureRegionChooseActions) && m_operations.captureBusy && m_operations.captureBusy());
}
ActivationReplyState ActivationDispatcher::admit(const ActivationRequest &request) {
  if (!parseActivation(serializeActivation(request)).request || !available(request)) return ActivationReplyState::Rejected;
  if (blocked(request) || (request.action != ActivationAction::Activate && m_pending)) return ActivationReplyState::Busy;
  const bool capture = request.action == ActivationAction::CaptureRegion || request.action == ActivationAction::CaptureRegionChooseActions || request.action == ActivationAction::RunWorkflow;
  if (capture && (m_operations.prepareCapture || request.action == ActivationAction::RunWorkflow)) {
    try { const QScopedValueRollback<bool> preparing(m_dispatching, true);
      m_preparedWorkflow = m_operations.prepareCapture ? m_operations.prepareCapture(request) : m_operations.prepareWorkflow(request.workflowId); }
    catch (...) { m_preparedWorkflow = {}; return ActivationReplyState::Rejected; }
    if (!m_preparedWorkflow) return ActivationReplyState::Rejected;
  }
  if (request.action != ActivationAction::Activate) m_pending = request;
  return ActivationReplyState::Accepted;
}
QString ActivationDispatcher::dispatch(const ActivationRequest &request) {
  if (!parseActivation(serializeActivation(request)).request || !available(request)) return QStringLiteral("activation-unavailable");
  std::function<void()> prepared;
  if (request.action != ActivationAction::Activate) {
    if (!m_pending || serializeActivation(*m_pending) != serializeActivation(request)) return QStringLiteral("activation-invalid");
    m_pending.reset();
    prepared = std::move(m_preparedWorkflow);
  }
  try {
    if (blocked(request)) return QStringLiteral("activation-busy");
    const QScopedValueRollback<bool> dispatching(m_dispatching, true);
    switch (request.action) {
      case ActivationAction::Activate: m_operations.activate(); break;
      case ActivationAction::CaptureRegion: if (prepared) prepared(); else m_operations.captureRegion(); break;
      case ActivationAction::EditImage: m_operations.editImage(request.path); break;
      case ActivationAction::RunWorkflow: if (!prepared) return QStringLiteral("activation-invalid"); prepared(); break;
      case ActivationAction::CaptureRegionChooseActions: if (prepared) prepared(); else m_operations.captureRegionWithActions(); break;
    }
  } catch (...) { return QStringLiteral("activation-handler-failed"); }
  return {};
}
} // namespace xerahs::shell
