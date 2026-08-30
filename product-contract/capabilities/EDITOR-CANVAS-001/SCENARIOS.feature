Feature: Native editor canvas operations

  Scenario: Viewport zoom does not dirty the document
    Given a clean open document
    When the user zooms in and pans
    Then document pixels and coordinates are unchanged
    And history and dirty state are unchanged

  Scenario: Cancelled custom rotation is lossless
    Given a document with annotations
    When the user previews a 17 degree rotation
    And invokes Cancel
    Then pixels, annotations, history, and dirty state equal their prior values

  Scenario: Resize canvas does not resample retained pixels
    Given a 100 by 80 image
    When the canvas is extended to 120 by 100 from the center anchor
    Then original decoded pixels are unchanged
    And their origin is translated by 10,10

  Scenario: Invalid replacement preserves the current session
    Given a dirty document is open
    When the user selects a corrupt image to open
    Then a typed decode error is shown
    And the dirty document remains available unchanged
