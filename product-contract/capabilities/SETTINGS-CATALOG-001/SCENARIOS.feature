Feature: Safe and portable application settings

  Scenario: One invalid field does not reset valid settings
    Given a versioned settings document containing valid fields and one invalid field
    When the native application loads the document
    Then valid fields retain their approved values
    And the invalid field uses its approved recovery behavior
    And a field-specific diagnostic is recorded without secret material

  Scenario: Unknown compatible fields survive a save
    Given a settings document from a newer compatible version
    And the document contains an unknown non-secret field
    When the user changes a known preference and saves
    Then the known preference is saved atomically
    And the unknown compatible field is retained unchanged

  Scenario: Imported credentials do not remain plaintext
    Given a legacy configuration containing a plaintext credential
    When the user explicitly approves migration
    Then the credential is stored through the native secure credential service
    And the migrated configuration contains only a non-secret reference
    And diagnostics and exports do not contain the credential

  Scenario: Window placement recovers after monitor removal
    Given a remembered window rectangle on a disconnected display
    When the application restores the window
    Then the window is clamped to an available work area
    And all controls remain reachable with keyboard and pointer input

  Scenario: Import validation fails before mutation
    Given a configuration import with a malformed nested object
    When the user requests import
    Then the application reports retained, transformed, and rejected fields
    And no persisted setting changes until the user accepts a valid plan

  Scenario: Entry points resolve the same scoped value
    Given global, workflow, and task values for one setting
    When the setting is read through GUI, CLI, MCP, and automation entry points
    Then each entry point resolves the same documented precedence
