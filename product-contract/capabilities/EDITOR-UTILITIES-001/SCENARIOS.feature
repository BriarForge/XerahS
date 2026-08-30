Feature: Safe ImageEditor utilities

  Scenario: Hash cancellation releases the file
    Given a large file is being hashed with SHA-256
    When the user invokes Stop
    Then progress stops with a cancelled state
    And the file can be moved or deleted by another process

  Scenario: Background-removal failure retains source
    Given a valid source and an incompatible model
    When background removal is attempted
    Then a model-shape failure is shown
    And the source remains available unchanged

  Scenario: Unsupported hardware encoder offers recovery
    Given the selected hardware codec is unavailable
    When conversion starts
    Then no partial output is claimed
    And an actionable supported-codec fallback is offered

  Scenario: QR region scan respects cancellation
    Given capture permission is granted
    When the user cancels region selection
    Then no pixels are scanned or retained
    And the QR utility remains usable
