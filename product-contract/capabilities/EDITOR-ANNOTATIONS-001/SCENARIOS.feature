Feature: Complete native annotation tool surface

  Scenario: Every contracted tool is reachable
    Given an editable image is open
    When the user enumerates the annotation tools
    Then every ID in annotation-tools.json is reachable or has an approved platform disposition

  Scenario: A reverse arrow drag preserves direction
    Given the Arrow tool is active
    When the user drags from 500,400 to 100,150
    Then the arrow head remains at 100,150
    And one undo operation is committed

  Scenario: Cancelling text input makes no mutation
    Given the Text tool has an in-progress empty editor
    When the user invokes Cancel
    Then no text annotation exists
    And document history is unchanged

  Scenario: Effect annotation samples immutable source
    Given a pixelate annotation overlaps itself during a move preview
    When the gesture is committed
    Then its pixels are derived from the source and lower ordered objects once
    And the result does not recursively accumulate preview pixels

  Scenario: Pasted annotation receives a new identity
    Given one image annotation with an embedded asset is copied
    When it is pasted
    Then both annotations reference durable asset data
    And their annotation IDs differ
