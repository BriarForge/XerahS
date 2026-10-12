#include "ActivationDispatcher.h"
#include <QScopedValueRollback>
#include <utility>

namespace xerahs::shell {
ActivationDispatcher::ActivationDispatcher(ActivationOperations operations) : m_operations(std::move(operations)) {}
bool ActivationDispatcher::available(const ActivationRequest &request) const {
  switch (request.action) {
    case ActivationAction::Activate: return bool(m_operations.activate);
    case ActivationAction::CaptureRegion: return bool(m_operations.captureRegion);
    case ActivationAction::EditImage: return bool(m_operations.editImage);
  }
  return false;
}
bool ActivationDispatcher::blocked(const ActivationRequest &request) const {
  if (request.action == ActivationAction::Activate) return false;
  return m_dispatching || (m_operations.interactionBlocked && m_operations.interactionBlocked()) ||
      (request.action == ActivationAction::CaptureRegion && m_operations.captureBusy && m_operations.captureBusy());
}
ActivationReplyState ActivationDispatcher::admit(const ActivationRequest &request) {
  if (!parseActivation(serializeActivation(request)).request || !available(request)) return ActivationReplyState::Rejected;
  if (blocked(request) || (request.action != ActivationAction::Activate && m_pending)) return ActivationReplyState::Busy;
  if (request.action != ActivationAction::Activate) m_pending = request;
  return ActivationReplyState::Accepted;
}
QString ActivationDispatcher::dispatch(const ActivationRequest &request) {
  if (!parseActivation(serializeActivation(request)).request || !available(request)) return QStringLiteral("activation-unavailable");
  if (request.action != ActivationAction::Activate) {
    if (!m_pending || serializeActivation(*m_pending) != serializeActivation(request)) return QStringLiteral("activation-invalid");
    m_pending.reset();
  }
  try {
    if (blocked(request)) return QStringLiteral("activation-busy");
    const QScopedValueRollback<bool> dispatching(m_dispatching, true);
    switch (request.action) {
      case ActivationAction::Activate: m_operations.activate(); break;
      case ActivationAction::CaptureRegion: m_operations.captureRegion(); break;
      case ActivationAction::EditImage: m_operations.editImage(request.path); break;
    }
  } catch (...) { return QStringLiteral("activation-handler-failed"); }
  return {};
}
} // namespace xerahs::shell
