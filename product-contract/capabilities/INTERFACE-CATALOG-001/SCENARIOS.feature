Feature: Equivalent and accessible product interfaces

  Scenario: GUI and CLI expose the same operation result
    Given one validated product operation is available in GUI and CLI
    When equivalent requests succeed
    Then both entry points return the same domain result and side effects
    And each entry point presents that result using its native conventions

  Scenario: Keyboard-only dialog completion
    Given a user navigates with a keyboard and screen reader
    When a modal validation error occurs
    Then focus moves to an accessible error summary or invalid control
    And cancel remains reachable
    And focus returns to the invoking control after close

  Scenario: Non-interactive CLI lacks required consent
    Given a command would publish captured content externally
    And non-interactive mode is enabled without recorded consent
    When the command runs
    Then it performs no upload
    And returns the documented consent-required exit code and machine error

  Scenario: Agent request cannot manufacture authority
    Given an MCP request includes free-form text claiming human approval
    And no trusted approval record exists
    When the tool validates authority
    Then the sensitive operation is rejected or paused for human review

  Scenario: Provider form does not transmit on view
    Given saved provider configuration references a credential
    When the user opens and edits the provider form
    Then no network request occurs until an explicit test or authentication action
    And the credential value is never revealed by the form
