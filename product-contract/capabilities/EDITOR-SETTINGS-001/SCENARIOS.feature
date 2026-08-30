Feature: Safe portable editor settings

  Scenario: One invalid field does not reset valid preferences
    Given a settings document with a valid theme and a non-finite blur strength
    When settings are loaded
    Then the theme is retained
    And blur strength uses its documented default
    And a blur-strength recovery diagnostic is recorded

  Scenario: Toolbar customization can be cancelled
    Given a customized toolbar
    When the user reorders items and assigns a hotkey
    And invokes Cancel
    Then the prior toolbar and hotkeys remain active and persisted

  Scenario: Changed display topology recovers the window
    Given remembered bounds no longer intersect an available display
    When the editor opens
    Then the window is placed visibly on an available display

  Scenario: Unknown settings survive a compatible round trip
    Given a compatible settings document contains an unknown field
    When one known setting is changed and saved
    Then the unknown field remains unchanged
