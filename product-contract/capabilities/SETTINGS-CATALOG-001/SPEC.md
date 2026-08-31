# SETTINGS-CATALOG-001 Application settings catalog

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

## Requirements

- **SETCAT-001:** Every settings-ledger row linked to this package MUST retain
  its stable ledger ID and MUST receive an approved semantic name, value type,
  default, validation rule, persistence scope, privacy class, migration rule,
  downstream effect, and Windows, macOS, and Linux disposition before this
  package can become active.
- **SETCAT-002:** Source declarations and initializer expressions are discovery
  evidence only. A default becomes normative only when acceptance evidence
  records the user outcome, boundary behavior, and supported-platform result.
- **SETCAT-003:** Settings loading MUST distinguish absent, valid, invalid,
  unknown, deprecated, and newer-version fields. Failure of one field MUST NOT
  discard unrelated valid fields, and recovery MUST produce a field-specific
  diagnostic that does not disclose secret values.
- **SETCAT-004:** Settings saving MUST be atomic and crash-safe, MUST preserve
  compatible unknown fields for round-trip, MUST use locale-independent
  encodings, and MUST retain a recoverable previous version under the approved
  configuration-retention policy.
- **SETCAT-005:** Boolean, numeric, enum, string, color, geometry, duration,
  collection, dictionary, and nested-object fields MUST have explicit type and
  range constraints. Non-finite numbers, invalid enum values, malformed colors,
  impossible geometry, and unbounded collections MUST be rejected or repaired
  according to the field's approved rule.
- **SETCAT-006:** Paths, file names, URLs, time zones, languages, fonts, hotkeys,
  monitors, devices, codecs, and native handles MUST use portable semantic
  representations. Platform-native values MUST NOT leak into the shared format
  without a documented compatibility adapter.
- **SETCAT-007:** Credentials, OAuth material, API keys, cookies, tokens,
  account identifiers, proxy secrets, and provider configuration classified as
  secret MUST be stored through an approved platform credential service. Plain
  settings files, diagnostics, telemetry, clipboard output, and exports MUST
  NOT contain secret material unless the user explicitly selects a separately
  reviewed encrypted export flow.
- **SETCAT-008:** Loading legacy plaintext secrets MUST require an explicit,
  auditable migration into secure storage and MUST remove the plaintext only
  after successful verification. A failed migration MUST preserve recoverability
  without continuing ordinary use of the plaintext value.
- **SETCAT-009:** User-visible preferences MUST declare whether they are global,
  profile, workflow, task, provider, device, or window scoped. Precedence MUST
  be deterministic, inspectable, and identical for UI, CLI, MCP, and automation
  entry points.
- **SETCAT-010:** Reset, import, and migration operations MUST identify the
  affected scope, preview destructive effects when material, preserve unrelated
  scopes, and provide cancellation before persistence. A full reset MUST NOT
  silently delete history, credentials, presets, workflows, or user files.
- **SETCAT-011:** Hotkey and gesture settings MUST use stable action IDs,
  validate collisions and reserved platform gestures, remain keyboard
  accessible, and expose an actionable conflict result instead of silently
  replacing another binding.
- **SETCAT-012:** Capture, recording, upload, workflow, and post-processing
  settings MUST NOT bypass consent, OS permissions, credential authority,
  destructive confirmation, or contractually required failure reporting.
- **SETCAT-013:** Window placement and geometry MUST adapt to display removal,
  scaling changes, and work-area changes; restored UI MUST remain visible and
  operable. Native handles and transient runtime state MUST NOT be persisted as
  portable configuration.
- **SETCAT-014:** Lists of workflows, destinations, accounts, presets, recent
  items, watch folders, external programs, and filters MUST use stable item IDs,
  deterministic ordering, duplicate policy, bounded size, and per-item error
  isolation. Missing referenced resources MUST produce a recoverable state.
- **SETCAT-015:** Recent files, history-derived suggestions, watch folders,
  clipboard-derived settings, and local paths are private local data. They MUST
  be removable, excluded from telemetry and support bundles by default, and
  governed by explicit retention behavior.
- **SETCAT-016:** Provider and destination settings MUST separate shared
  behavioral configuration from credentials, validate endpoints and scopes,
  avoid network access during ordinary load, and require explicit user action
  for connection tests or authorization.
- **SETCAT-017:** Update, plugin, external-program, and executable-path settings
  MUST honor signature, provenance, sandbox, and user-consent policy. A setting
  MUST NOT make untrusted code executable merely because it was imported from a
  compatible configuration file.
- **SETCAT-018:** Accessibility preferences and system-derived theme, contrast,
  reduced-motion, input, and notification settings MUST preserve native user
  choices. Unsupported visual substitutions MUST have an accepted equivalent
  or degraded disposition rather than silently ignoring the preference.
- **SETCAT-019:** Configuration import and export MUST be versioned, validate
  before mutation, report retained, transformed, rejected, and secret fields,
  and satisfy the separate configuration compatibility contract and fixtures.
- **SETCAT-020:** Activation MUST include generated coverage proving every linked
  ledger ID has an accepted field disposition, boundary and malformed-input
  vectors for every value family, upgrade and downgrade fixtures, unknown-field
  round-trip, atomic-write failure, secret redaction, and execution on all three
  supported platform adapters.

## Baseline traceability and disposition

The `settings-ledger.yaml` inventory is the exhaustive pinned-baseline queue for
this package. Rows already governed by `EDITOR-SETTINGS-001` remain with that
more-specific package. Linking a row here records its contract owner; it does not
approve the source initializer as product intent.
