Feature: Native platform capability invariants

  Scenario: Repeated launch activates the existing instance
    Given one interactive application instance owns the current profile
    When another process submits a valid activation request
    Then no second ordinary interactive instance is created
    And the existing instance receives the request or an actionable failure is returned

  Scenario: Capture permission is denied
    Given the selected capture operation lacks native screen permission
    When capture starts
    Then no image or recording is fabricated
    And the result identifies permission denial and an approved remediation path

  Scenario: Recording dependency fails after output begins
    Given a recording session has written recoverable media
    When the encoder dependency fails
    Then the session reaches a failure state
    And recoverable output and cleanup state are reported

  Scenario: Upload response is lost after remote acceptance
    Given an upload uses an idempotency key
    When the response is lost and bounded retry occurs
    Then at most one remote object is published
    And the result distinguishes attempts from remote outputs

  Scenario: Untrusted plugin requests a credential
    Given a plugin lacks declared credential authority
    When it requests a destination secret
    Then the trusted host denies the request
    And records a redacted auditable event

  Scenario: Corrupt history index preserves source data
    Given the history index is corrupt but task records remain readable
    When recovery runs
    Then it rebuilds or quarantines the index according to policy
    And does not silently delete valid task records or media
