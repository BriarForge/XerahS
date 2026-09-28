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

  Scenario: A newer stable NuGet package is available
    Given a repository-owned NuGet dependency is due for a version check
    And its authoritative package source publishes a newer stable version
    When an agent performs dependency maintenance
    Then the latest stable version is selected without case-by-case approval
    And authoritative version declarations and lockfiles are updated together
    And applicable restore, build, test, security, licensing, and conformance checks pass
    And the source, versions, check time, and results are recorded

  Scenario: The latest stable NuGet package violates a protected boundary
    Given the latest stable package cannot satisfy an approved toolchain or protected boundary
    When the agent cannot complete a conforming adoption
    Then the newest conforming version is retained temporarily
    And the exact rejected version, evidence, reason, owner, remediation, and next review are recorded
    And the next review is no more than 30 days away

  Scenario: Switching Linux edition keeps user data
    Given the linux-qt edition is installed with settings and history
    When the user installs the linux-gnome edition on the same system
    Then the linux-qt edition is replaced rather than installed alongside it
    And the settings, history, and outputs are read without migration

  Scenario: A supported edition blocks the Linux disposition
    Given linux-qt and linux-hyprland are both supported editions
    And linux-hyprland has no accepted disposition for a requirement
    Then the requirement's Linux disposition is not accepted
