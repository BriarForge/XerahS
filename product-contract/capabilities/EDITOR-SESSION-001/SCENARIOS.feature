Feature: Re-editable native editor session

  Scenario: Reverse drag creates one normalized rectangle operation
    Given a loaded 800 by 600 source
    When the user drags the rectangle tool from 500,400 to 100,150
    Then one rectangle exists with bounds 100,150,500,400
    And one undo operation is available

  Scenario: Undo and redo restore rectangle state
    Given a rectangle was created and then moved
    When the user invokes Undo
    Then the rectangle returns to its pre-move bounds
    When the user invokes Redo
    Then the rectangle returns to its moved bounds

  Scenario: New edit invalidates redo
    Given the user undid a rectangle move
    When the user changes the rectangle stroke width
    Then redo is unavailable
    And undo restores the prior stroke width

  Scenario: Sidecar failure keeps session dirty
    Given raster export succeeds
    And annotation sidecar persistence fails
    When the save attempt completes
    Then the session remains dirty
    And the raster artifact is reported as succeeded
    And the sidecar artifact is reported as failed

  Scenario: Mismatched source requires a choice
    Given a sidecar hash differs from the current raster hash
    When the document is opened
    Then annotations are retained
    And the user chooses between the current raster and embedded source
    And neither source is selected silently
