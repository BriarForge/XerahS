Feature: Atomic and explicit editor outputs

  Scenario: Failed overwrite preserves the old file
    Given a destination already contains a valid image
    When encoding or writing the replacement fails
    Then the previous destination remains readable and unchanged
    And the editor remains dirty

  Scenario: Upload failure does not close the editor
    Given an edited image and an available upload action
    When the destination returns a failure
    Then a typed failure and retry are shown
    And the editable session remains open

  Scenario: Cancel task returns no image
    Given the editor is running in task mode
    When the user confirms Cancel
    Then the host receives exactly one Cancel outcome
    And no rendered image is returned or copied

  Scenario: Missing host service is visible
    Given the host did not provide a print service
    When the user inspects output actions
    Then Print is disabled or marked unavailable with an explanation
