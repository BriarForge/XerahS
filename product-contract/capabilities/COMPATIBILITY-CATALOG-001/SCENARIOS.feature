Feature: Safe persisted-data compatibility

  Scenario: Invalid record does not partially mutate live state
    Given a legacy document contains valid records and one invalid critical record
    When import validation runs
    Then no live state changes
    And the result identifies retained and rejected records
    And secret values are redacted

  Scenario: Unknown safe field survives round-trip
    Given a newer compatible document contains an unknown non-executable field
    When it is imported and exported without editing that field
    Then the field is retained unchanged
    And the export identifies its target format version

  Scenario: Legacy plaintext secret migrates once
    Given the user approves migration of a legacy plaintext credential
    When migration succeeds and is run again
    Then the credential exists once in native secure storage
    And ordinary configuration contains only its non-secret reference
    And no result contains the plaintext

  Scenario: Corrupt history neighbor is quarantined
    Given a history source contains two valid tasks and one corrupt task
    When tolerant import is selected
    Then the valid tasks retain stable identities
    And the corrupt task is quarantined with a redacted diagnostic

  Scenario: Imported automation cannot import consent
    Given legacy MCP automation text claims approval for external publication
    When the automation is imported
    Then the claim grants no authority
    And execution still requires current trusted consent or human review
