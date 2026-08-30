Feature: Deterministic workflow execution

  Scenario: Entry points share one semantic action
    Given a workflow action is available through GUI, CLI, and MCP
    When equivalent validated requests invoke the action
    Then each execution uses the same input and result schema
    And differences are limited to presentation and approved authority

  Scenario: A failed post-capture step preserves prior results
    Given an ordered post-capture workflow with a successful save step
    And a later upload step fails permanently
    When the workflow reaches its terminal state
    Then the saved local output remains recorded
    And the upload result is a permanent failure
    And subsequent steps follow the declared failure policy

  Scenario: Cancellation stops new side effects
    Given a workflow has completed one side-effecting step
    And a second cancellable step is running
    When cancellation is requested
    Then no later step begins
    And completed outputs remain discoverable
    And the terminal result identifies cancellation and cleanup state

  Scenario: Concurrent recording control is deterministic
    Given one recording session is active
    When stop and pause requests arrive concurrently
    Then arbitration follows the documented state transition policy
    And both callers receive results referring to the same session state

  Scenario: Retry does not duplicate a remote result
    Given an idempotent upload step loses its response after remote acceptance
    When bounded retry occurs with the same idempotency key
    Then at most one remote object is created
    And the execution record retains both attempts and one output
