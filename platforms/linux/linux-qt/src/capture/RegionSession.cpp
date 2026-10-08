#include "capture/RegionSession.h"

#include <utility>

namespace xerahs::capture {

namespace {

struct StateName {
  SessionState state;
  const char *name;
};

constexpr StateName kStateNames[] = {
    {SessionState::Created, "created"},       {SessionState::Authorizing, "authorizing"},
    {SessionState::Preparing, "preparing"},   {SessionState::Idle, "idle"},
    {SessionState::Selecting, "selecting"},   {SessionState::Selected, "selected"},
    {SessionState::Confirming, "confirming"}, {SessionState::Completed, "completed"},
    {SessionState::Cancelled, "cancelled"},   {SessionState::Failed, "failed"},
};

}  // namespace

QString sessionStateName(SessionState state) {
  for (const StateName &entry : kStateNames) {
    if (entry.state == state) return QString::fromLatin1(entry.name);
  }
  return {};
}

std::optional<SessionState> sessionStateFromName(const QString &name) {
  for (const StateName &entry : kStateNames) {
    if (name == QLatin1String(entry.name)) return entry.state;
  }
  return std::nullopt;
}

bool isTerminal(SessionState state) {
  return state == SessionState::Completed || state == SessionState::Cancelled || state == SessionState::Failed;
}

RegionSession::RegionSession(Topology topology, SessionSettings settings, std::vector<SnapTarget> snapTargets,
                             Publisher publish)
    : m_topology(std::move(topology)),
      m_settings(settings),
      m_snapTargets(std::move(snapTargets)),
      m_publish(std::move(publish)) {}

void RegionSession::start() {
  if (m_state == SessionState::Created) m_state = SessionState::Authorizing;
}

void RegionSession::resumeAt(SessionState state) {
  m_state = state;
  // An overlay exists in every state from idle onwards (RC-001).
  m_overlayShown = state == SessionState::Idle || state == SessionState::Selecting ||
                   state == SessionState::Selected || state == SessionState::Confirming;
}

void RegionSession::permission(Permission value) {
  if (m_state != SessionState::Authorizing) return;
  if (value == Permission::Authorized) {
    m_state = SessionState::Preparing;
    return;
  }
  // RC-001, RC-002, RC-025: no overlay, no prompt loop.
  finish(SessionState::Failed, diagnostic::capturePermissionDenied);
}

void RegionSession::ready() {
  if (m_state != SessionState::Preparing) return;
  m_state = SessionState::Idle;
  m_overlayShown = true;
}

void RegionSession::pointerDown(PhysicalPoint point) {
  if (m_state != SessionState::Idle && m_state != SessionState::Selected) return;
  // STATE_MACHINE.md: pointer-down in selected starts a new selection.
  m_anchor = clampPhysical(m_topology, point);
  m_selection = normalized(*m_anchor, *m_anchor);
  m_state = SessionState::Selecting;
}

void RegionSession::pointerMove(PhysicalPoint point) {
  if (m_state != SessionState::Selecting || !m_anchor) return;
  m_selection = normalized(*m_anchor, clampPhysical(m_topology, point));
}

void RegionSession::pointerUp(PhysicalPoint point) {
  if (m_state != SessionState::Selecting || !m_anchor) return;
  const PhysicalPoint end = clampPhysical(m_topology, point);
  const PhysicalRect rect = normalized(*m_anchor, end);
  m_anchor.reset();
  if (rect.positive()) {
    release(rect);
    return;
  }
  // RC-026: a click selects a snap target only when snapping is enabled.
  const SnapTarget *target = m_settings.snapping && rect.width() == 0 && rect.height() == 0 ? snapTargetAt(end) : nullptr;
  if (target) {
    const std::optional<PhysicalRect> clamped = intersection(target->physical, boundingBox(m_topology));
    if (clamped) {
      release(*clamped);
      return;
    }
  }
  // RC-009, RC-026. Pending clarification (ROOT-ESCALATE-001): the contract
  // names a zero-area release; a drag with zero width or zero height (a line)
  // cannot be confirmed either and also returns to idle.
  m_selection.reset();
  m_state = SessionState::Idle;
}

void RegionSession::release(const PhysicalRect &rect) {
  // RC-007.
  m_selection = rect;
  m_state = m_settings.quickConfirm ? SessionState::Confirming : SessionState::Selected;
}

void RegionSession::keyEnter() {
  // RC-009: Enter confirms only a positive current selection.
  if (m_state != SessionState::Selected || !m_selection || !m_selection->positive()) return;
  m_state = SessionState::Confirming;
}

void RegionSession::confirm() { keyEnter(); }

void RegionSession::keyEscape() {
  // RC-008: no image, no last-region update, no post-capture actions.
  if (isTerminal(m_state)) return;
  finish(SessionState::Cancelled, std::nullopt);
}

void RegionSession::cancel() { keyEscape(); }

void RegionSession::topologyChanged() {
  // RC-003: a topology change cancels; restarting is a new session.
  if (isTerminal(m_state)) return;
  finish(SessionState::Cancelled, diagnostic::displayTopologyChanged);
}

void RegionSession::captureSucceeded() {
  if (m_state != SessionState::Confirming || !m_selection) return;
  // RC-017: the last region updates only now that pixels were acquired.
  m_lastRegionUpdated = true;
  finish(SessionState::Completed, std::nullopt);
}

void RegionSession::captureFailed() {
  if (m_state != SessionState::Confirming) return;
  finish(SessionState::Failed, diagnostic::captureSourceFailed);
}

void RegionSession::finish(SessionState terminal, std::optional<QString> diagnostic) {
  // RC-016: exactly one published result.
  m_state = terminal;
  m_diagnostic = std::move(diagnostic);
  if (terminal != SessionState::Completed) m_selection.reset();
  SessionOutcome outcome;
  outcome.state = terminal;
  outcome.diagnostic = m_diagnostic;
  if (terminal == SessionState::Completed) {
    outcome.rectangle = m_selection;
    outcome.lastRegion = makeLastRegion(*m_selection, m_topology);
  }
  ++m_published;
  if (m_publish) m_publish(outcome);
}

const SnapTarget *RegionSession::snapTargetAt(PhysicalPoint point) const {
  for (const SnapTarget &target : m_snapTargets) {
    if (target.physical.contains(point.x, point.y)) return &target;
  }
  return nullptr;
}

}  // namespace xerahs::capture
