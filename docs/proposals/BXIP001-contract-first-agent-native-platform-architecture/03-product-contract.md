# 4. Product Contract Design

## 4.1 Proposed repository structure

The implementation SHALL use a structure similar to:

```text
AGENTS.md

product-contract/
  AGENTS.md
  README.md
  manifest.yaml
  glossary.md
  reference-baselines/
    kova-0.29.0.yaml
    provenance/
  parity/
    capability-ledger.yaml
    settings-ledger.yaml
    workflow-ledger.yaml
    interface-ledger.yaml
    compatibility-ledger.yaml
    baseline-deltas.yaml
  capabilities/
    capture/
      REGION-CAPTURE-001/
        SPEC.md
        SCENARIOS.feature
        STATE_MACHINE.md
        settings.schema.json
        test-vectors.json
        fixtures/
        platforms/
          windows.md
          macos.md
          linux.md
    image-editor/
      AGENTS.md
      EDITOR-SESSION-001/
      ANNOTATION-DOCUMENT-001/
      ANNOTATION-TOOLS-001/
      IMAGE-EFFECTS-001/
      schemas/
      scenarios/
      fixtures/
      test-vectors/
      compatibility/
        xann-v1.md
    video-editor/
      AGENTS.md
      VIDEO-EDITOR-SESSION-001/
      MEDIA-OPERATIONS-001/
      schemas/
      scenarios/
      fixtures/
      test-vectors/
  schemas/
  decisions/
  waivers/

platforms/
  AGENTS.md
  windows/
    AGENTS.md
    image-editor/
      AGENTS.md
    video-editor/
      AGENTS.md
  macos/
    AGENTS.md
    image-editor/
      AGENTS.md
    video-editor/
      AGENTS.md
  linux/
    AGENTS.md
    image-editor/
      AGENTS.md
    video-editor/
      AGENTS.md

conformance/
  AGENTS.md
  runner/
  adapters/
    windows/
    macos/
    linux/
  image-editor/
    AGENTS.md
    golden-images/
    compatibility-fixtures/
  video-editor/
    AGENTS.md
    media-fixtures/
    compatibility-fixtures/
  reports/

tools/
  contract-linter/
    AGENTS.md
```

This layout is the implementation format ([D-CON-001](08-decisions.md#d-con-001-contract-schema-versioning-and-tooling)). The first four capabilities validate the format; passing that qualification expands the same structure across every baseline-ledger domain without requiring a second architecture approval.

## 4.2 Required contents of a capability contract

Each capability contract SHALL contain:

1. Stable capability and requirement identifiers.
2. User intent and the problem being solved.
3. Definitions for domain terms.
4. Preconditions, inputs, outputs, and persisted state.
5. Normative workflow and ordering rules.
6. Failure, cancellation, retry, and recovery behavior.
7. Settings, defaults, validation, and migration rules.
8. Cross-platform invariants.
9. Permitted native adaptations.
10. Accessibility, privacy, security, and performance expectations.
11. Acceptance scenarios.
12. Deterministic schemas, fixtures, or test vectors where applicable.
13. Known platform limitations and approved deviations.
14. Compatibility expectations between contract versions.
15. Baseline-ledger IDs and pinned source evidence from which the capability was discovered.
16. A statement covering known KovaForge behavior differences: preserve, native equivalent, corrected defect, or approved retirement.

## 4.3 Example contract excerpt

```md
# POST-CAPTURE-CLIPBOARD-001 Copy captured image to clipboard

## User intent

After completing a valid image capture, the user can have XerahS place the
captured image on the system clipboard automatically.

## Requirements

- PCC-001: When the action is enabled, XerahS MUST attempt to write the final
  captured image to the system clipboard.
- PCC-002: Clipboard failure MUST NOT discard the captured image.
- PCC-003: A failed clipboard action MUST be recorded as failed in the task
  result and MUST NOT prevent later independent actions from running.
- PCC-004: The clipboard representation MAY differ by platform, but pasting
  into the platform's standard image-capable applications MUST reproduce the
  captured image without changing its pixel dimensions.
```

The corresponding scenario can be expressed as:

```gherkin
Scenario: Clipboard failure does not stop later actions
  Given a valid image capture
  And copy-to-clipboard is enabled before save-to-file
  And the system clipboard rejects the image
  When post-capture actions execute
  Then the clipboard action is recorded as failed
  And the save-to-file action still executes
  And the captured image remains available to the workflow
```

## 4.4 Exact behavior requires exact evidence

The following categories SHOULD include machine-readable definitions or reference vectors:

- Configuration and history formats
- Filename token expansion and escaping
- URL construction and uploader requests
- Image transformations and encoders
- Workflow ordering and state transitions
- Data migrations
- Cryptographic and security behavior
- Cross-process or plugin protocols

Example:

```json
{
  "requirement": "FILENAME-COUNTER-004",
  "input": {
    "pattern": "%date%_%counter%",
    "date": "2026-08-30",
    "counter": 7,
    "counter_padding": 3,
    "extension": "png"
  },
  "expected": "2026-08-30_007.png"
}
```

Shared code MAY remain where a single exact implementation is safer or materially more economical. Such code is an implementation choice, not the definition of product behavior. The contract and conformance evidence remain authoritative.

## 4.5 Baseline traceability

Every contract capability SHALL map bidirectionally to the parity ledgers in [section 20](11-reference-baseline.md):

- a baseline ledger row cannot reach `contracted` without a valid contract path and requirement IDs
- a contract cannot become release-active without one or more ledger rows or an explicit greenfield-only classification
- deletion or retirement of a contract fails CI while a baseline row still depends on it
- changes to source evidence, classifications, or supported-platform dispositions require product-owner review

This makes completeness enforceable without treating legacy source code as normative.
