Feature: Deterministic filename generation

  Scenario: Date, time, dimensions, and counter expand from one context
    Given the expansion context from vector "date-counter"
    When pattern "%y-%mo-%d_%h-%mi-%s_%widthx%height_%i{3}" is expanded as a filename
    Then the result is "2026-08-30_17-04-05_1920x1080_008"
    And the next counter is 8

  Scenario: Counter advances only once for multiple counter tokens
    Given the counter is 7
    When pattern "%i_%i{3}_%ix_%iX" is expanded
    Then every counter token represents the value 8
    And the next counter is 8

  Scenario: Portable invalid characters are deterministic
    When pattern "report:<draft>|final?.png" is expanded as a filename
    Then the result is "report_draft_final_.png"

  Scenario: Unknown token survives round-trip
    When pattern "capture_%future" is expanded
    Then the result is "capture_%future"

  Scenario: Validation failure does not consume a counter
    Given the counter is 7
    And random-file access is denied
    When pattern "%i_%rf{secret.txt}" is expanded
    Then expansion fails at "%rf{secret.txt}"
    And the next counter remains 7
