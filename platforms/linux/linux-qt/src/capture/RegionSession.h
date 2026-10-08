// REGION-CAPTURE-001 session state machine (STATE_MACHINE.md, RC-025). The
// overlay feeds it events in desktop physical pixels; it decides transitions,
// the selection, the single published result, and the last-region update.
#pragma once

#include "capture/RegionGeometry.h"

#include <functional>
#include <optional>

namespace xerahs::capture {

enum class SessionState {
  Created,
  Authorizing,
  Preparing,
  Idle,
  Selecting,
  Selected,
  Confirming,
  Completed,
  Cancelled,
  Failed,
};
QString sessionStateName(SessionState state);
std::optional<SessionState> sessionStateFromName(const QString &name);
bool isTerminal(SessionState state);

enum class Permission { Authorized, Denied, Restricted };

struct SessionSettings {
  bool quickConfirm = true;  // SPEC "Settings and defaults"
  bool snapping = true;
};

// RC-011: a detected window or control.
struct SnapTarget {
  PhysicalRect physical;
};

// Published exactly once, on the terminal transition (RC-016).
struct SessionOutcome {
  SessionState state = SessionState::Cancelled;
  std::optional<PhysicalRect> rectangle;  // set only on completion
  std::optional<QString> diagnostic;
  std::optional<LastRegion> lastRegion;   // RC-017: set only after pixels are acquired
};

class RegionSession {
public:
  using Publisher = std::function<void(const SessionOutcome &)>;

  RegionSession(Topology topology, SessionSettings settings, std::vector<SnapTarget> snapTargets,
                Publisher publish);

  // created -> authorizing.
  void start();
  void permission(Permission value);
  void ready();
  void pointerDown(PhysicalPoint point);
  void pointerMove(PhysicalPoint point);
  void pointerUp(PhysicalPoint point);
  void keyEnter();
  void confirm();
  void keyEscape();
  void cancel();
  void topologyChanged();
  void captureSucceeded();
  void captureFailed();

  // Resumes at a later state, as the conformance vectors do.
  void resumeAt(SessionState state);

  SessionState state() const { return m_state; }
  std::optional<PhysicalRect> selection() const { return m_selection; }
  std::optional<QString> diagnostic() const { return m_diagnostic; }
  bool overlayShown() const { return m_overlayShown; }
  int publishedResults() const { return m_published; }
  bool lastRegionUpdated() const { return m_lastRegionUpdated; }

private:
  void finish(SessionState terminal, std::optional<QString> diagnostic);
  void release(const PhysicalRect &rect);
  const SnapTarget *snapTargetAt(PhysicalPoint point) const;

  Topology m_topology;
  SessionSettings m_settings;
  std::vector<SnapTarget> m_snapTargets;
  Publisher m_publish;

  SessionState m_state = SessionState::Created;
  std::optional<PhysicalPoint> m_anchor;
  std::optional<PhysicalRect> m_selection;
  std::optional<QString> m_diagnostic;
  bool m_overlayShown = false;
  int m_published = 0;
  bool m_lastRegionUpdated = false;
};

}  // namespace xerahs::capture
