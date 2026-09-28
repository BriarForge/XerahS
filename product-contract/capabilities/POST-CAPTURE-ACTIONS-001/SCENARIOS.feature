Feature: Ordered and recoverable post-capture actions

  Scenario: Transformation precedes save and clipboard
    Given image effects, annotation, save, and image clipboard are selected
    When the pipeline completes successfully
    Then both save and clipboard receive the annotated transformed media
    And action results are ordered as image effects, annotation, save, image clipboard

  Scenario: Clipboard failure does not stop upload
    Given save, image clipboard, and upload are selected
    And save succeeds
    And the clipboard rejects the image
    When post-capture actions execute
    Then image clipboard is recorded as failed
    And upload uses the saved file
    And the capture remains available in the result

  Scenario: Dependent action is skipped honestly
    Given path clipboard is selected without an action that creates a file
    When post-capture actions execute
    Then path clipboard is skipped with diagnostic "dependency-unavailable"

  Scenario: Selection cancellation is side-effect free
    Given the after-capture selection window is selected
    When the user cancels the selection window
    Then no later action starts
    And the pipeline is cancelled
    And the original capture remains available

  Scenario: Retry does not duplicate a successful upload
    Given upload succeeded and reveal failed
    When failed actions are retried
    Then upload is not repeated
    And reveal is attempted in a linked retry

  Scenario: Declining the before-upload window does not cancel the pipeline
    Given save, the before-upload window, upload, and reveal are selected
    When the user declines the before-upload window
    Then upload is skipped with diagnostic "upload-declined"
    And reveal still runs
    And the pipeline is completed

  Scenario: Delete never removes the only copy after a failed upload
    Given save, upload, and delete are selected
    And upload fails
    When post-capture actions execute
    Then delete is skipped with diagnostic "delete-guard"
    And the saved file remains on disk
