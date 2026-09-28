Feature: Interactive region capture

  Scenario: Reverse drag normalizes physical bounds
    Given a 100 percent scale monitor
    When the user drags from physical point 900,700 to 100,200
    Then the pending rectangle is left 100 top 200 right 900 bottom 700
    And the output size after confirmation is 800 by 500 pixels

  Scenario: Mixed-scale cross-monitor selection preserves physical pixels
    Given the topology from vector "mixed-scale-cross-monitor"
    When the logical endpoints in that vector are confirmed
    Then the physical rectangle and output size equal the vector

  Scenario: Escape is side-effect free
    Given a positive pending selection
    When the user presses Escape
    Then the session is cancelled exactly once
    And no image is returned
    And last region is unchanged

  Scenario: Permission denial is actionable
    Given screen capture permission is denied
    When region capture starts
    Then no overlay is shown
    And the result diagnostic is "capture-permission-denied"
    And an available native settings route is offered

  Scenario: Display topology changes during selection
    Given the user is selecting across two monitors
    When either monitor scale or bounds changes
    Then the session is cancelled with "display-topology-changed"
    And stale bounds are not persisted

  Scenario: Fractional scaling rounds down to the pixel under the pointer
    Given the topology from vector "fractional-scale-floors"
    When the logical endpoints in that vector are confirmed
    Then the rectangle is left 2020 top 50 right 2171 bottom 125

  Scenario: Gaps between monitors are transparent
    Given monitors of different heights
    When the selection covers space no monitor shows
    Then those output pixels are transparent black

  Scenario: A stale last region is refused
    Given the last region was stored on a display whose bounds later changed
    When last-region capture runs
    Then it fails with "last-region-invalid" and captures nothing
