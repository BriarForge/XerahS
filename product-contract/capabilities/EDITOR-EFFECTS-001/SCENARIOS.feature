Feature: Complete deterministic effect execution

  Scenario: The selected effect catalog is exhaustive
    Given the pinned ImageEditor discovery inventory
    When the selected effect IDs are compared with concrete discoverable effects
    Then all 232 IDs occur exactly once
    And category totals are 32, 16, 149, and 35

  Scenario: Cancelling a preview restores the document
    Given a dirty document with annotations and history
    When a previewable effect changes several parameters
    And the user invokes Cancel
    Then source pixels, annotations, history, and dirty state equal their pre-preview values

  Scenario: Seeded procedural output is repeatable
    Given identical pixels, parameters, seed, renderer version, and color profile
    When a procedural effect is applied on each supported platform
    Then decoded output pixels are identical

  Scenario: Invalid external asset is rejected atomically
    Given an effect parameter references an oversized or malformed image
    When the user applies the effect
    Then a typed validation error is shown
    And no document mutation is committed
