Feature: Native Android and iOS applications

  Scenario: Shared items each start a pipeline in order
    Given the user shares an image, a link, and an unreadable file to XerahS
    When the share is received
    Then an image pipeline and a URL pipeline start in that order
    And the unreadable file reports "share-item-unsupported"

  Scenario: Android screen capture asks for consent each session
    Given screen capture is started on Android
    When the MediaProjection consent is granted
    Then a foreground notification is shown while projection is active
    And projection stops when the capture session ends

  Scenario: iOS explains why it imports rather than captures
    Given the user chooses a capture source on iOS
    Then the latest screenshot and the photo picker are offered
    And the screen states that other apps cannot be captured directly

  Scenario: Desktop-only actions in an imported workflow are skipped honestly
    Given an imported workflow selects save, pin, and path clipboard
    When it runs on a phone
    Then save succeeds
    And pin and path clipboard are skipped with "platform-not-applicable"

  Scenario: An upload continues after the app is backgrounded
    Given an upload is in progress
    When the user switches to another app and the system suspends XerahS
    Then the upload continues as a background transfer
    And its result is recorded when XerahS next runs

  Scenario: Editor output matches the desktop
    Given the document from EDITOR-SESSION-001 vector "render-stroke-coverage"
    When it is exported on Android and iOS
    Then every decoded pixel equals the vector
