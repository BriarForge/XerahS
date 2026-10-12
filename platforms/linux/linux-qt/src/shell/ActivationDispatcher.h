// Production activation admission/dispatch, independent of the native UI/I/O.
#pragma once
#include "ActivationProtocol.h"
#include <functional>

namespace xerahs::shell {
struct ActivationOperations {
  std::function<void()> activate;
  std::function<void()> captureRegion;
  std::function<void(const QString &)> editImage;
  std::function<bool()> captureBusy;
  std::function<bool()> interactionBlocked;
};
class ActivationDispatcher final {
public:
  explicit ActivationDispatcher(ActivationOperations operations);
  ActivationReplyState admit(const ActivationRequest &request);
  // Empty means the operation was invoked, not that asynchronous work succeeded.
  // Non-activate requests must have an admission reservation first.
  QString dispatch(const ActivationRequest &request);
private:
  bool available(const ActivationRequest &request) const;
  bool blocked(const ActivationRequest &request) const;
  ActivationOperations m_operations;
  std::optional<ActivationRequest> m_pending;
  bool m_dispatching = false;
};
} // namespace xerahs::shell
