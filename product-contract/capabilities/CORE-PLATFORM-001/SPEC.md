# CORE-PLATFORM-001 Native platform capability foundation

Version: 0.2.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

## Shared requirements

- **CORE-001:** Every capability-ledger row linked to this package MUST retain
  its stable ledger ID and receive an approved user outcome, permission and
  authority model, inputs, outputs, cancellation and failure behavior,
  accessibility impact, privacy classification, persistence impact, and
  Windows, macOS, and Linux disposition before activation.
- **CORE-002:** The application MUST use the identity `com.xerahs.app` (or the
  platform equivalent) and display name `XerahS` as the replacement for the
  reference application. Migration of the reference application's live settings
  MUST require explicit user action, create a recoverable backup before mutation,
  and preserve source data when conversion is unsupported or fails.
- **CORE-003:** Native adapters MAY differ in presentation and OS mechanism but
  MUST return shared semantic results and stable error categories. Degraded,
  unavailable, and substitute behavior requires an accepted disposition.
- **CORE-004:** Long-running operations MUST expose progress where knowable,
  cooperative cancellation, deterministic terminal state, redacted diagnostics,
  and recoverable completed outputs.
- **CORE-005:** Permissions, credentials, external publication, destructive
  actions, plugin execution, and capture of sensitive content MUST apply least
  authority and the required human consent or review at the operation boundary.

## Application shell

- **CORE-006:** Launch MUST create at most one ordinary interactive instance per
  user profile. A repeated launch MUST deliver its validated activation request
  to the existing instance or return an actionable failure without corrupting
  state.
- **CORE-007:** Tray, main-window, hotkey, notification, and command-palette
  surfaces MUST invoke shared operations, expose current availability, remain
  keyboard accessible, and MUST NOT present inert actions as successful.
- **CORE-008:** Onboarding MUST be resumable, skippable where safe, accessible,
  and explicit about permissions, credentials, imports, telemetry, and default
  workflow consequences. Declining an optional integration MUST preserve core
  local operation.
- **CORE-009:** Theme and localization MUST follow explicit or system preference,
  preserve high contrast and reduced motion, support runtime text expansion,
  and fall back predictably without losing the user's stored preference.
- **CORE-010:** Notifications MUST identify the originating operation and true
  result, protect private previews, respect native notification policy, and
  provide actions only while their authority and target remain valid.
- **CORE-011:** Update checks MUST be explicit or policy-scheduled, verify signed
  provenance, expose version and release channel, never install silently without
  approved policy, and preserve rollback or recovery when installation fails.

## Configuration and secrets

- **CORE-012:** Configuration lifecycle MUST satisfy `SETTINGS-CATALOG-001`, use
  versioned atomic persistence, isolate profiles, recover from partial writes,
  and provide previewed import, migration, reset, and export operations.
- **CORE-013:** Secrets MUST be stored through approved OS credential services,
  referenced rather than serialized into ordinary configuration, redacted from
  every diagnostic and interface, and removable with clear impact on dependent
  destinations and workflows.

## Capture and recording

- **CORE-014:** Full-screen, monitor, and window capture MUST identify the
  requested target, map physical and logical coordinates correctly under mixed
  scaling, apply cursor and delay options, and return an explicit permission,
  cancellation, target-lost, or unavailable result.
- **CORE-015:** Scrolling capture MUST expose target selection, scroll strategy,
  progress, overlap and merge policy, cancellation, maximum bounds, and partial
  output handling; it MUST NOT synthesize a seamless result when alignment
  confidence is below the approved threshold.
- **CORE-016:** Automatic capture MUST require an explicit bounded schedule and
  target scope, remain visibly stoppable, avoid overlapping executions by
  documented policy, and respect screen-lock, permission, storage, and privacy
  conditions.
- **CORE-017:** Video and GIF recording MUST declare target, frame timing,
  cursor, codec, quality, container, output, pause, stop, abort, and recoverable
  failure behavior. Recording state transitions MUST be validated and observable.
- **CORE-018:** Audio recording MUST enumerate explicitly selected system and
  microphone inputs, indicate active capture, apply platform permission, define
  mixing and synchronization, and produce no audio track when consent or device
  availability is absent unless an approved degraded mode is selected.

## Media and uploads

- **CORE-019:** Video editing and media utilities MUST declare accepted formats,
  metadata policy, deterministic transforms, quality and codec selection,
  progress, cancellation, temporary-file handling, and recoverable output on
  dependency or encoding failure.
- **CORE-020:** Upload execution MUST validate local input and destination before
  network transfer, use secure credential references, expose progress and
  cancellation, bound retry, apply idempotency where possible, and distinguish
  local preparation from remote publication success.
- **CORE-021:** First-party and custom destinations MUST publish versioned
  capabilities, configuration fields, authentication, result schema, URL and
  deletion semantics, size and content constraints, rate behavior, and stable
  errors without executing untrusted configuration as code.
- **CORE-022:** Plugin extensibility MUST use an explicitly approved isolated
  host, signed or user-approved provenance, declared permissions, bounded IPC,
  secret mediation, crash containment, revocation, and audit. Plugins MUST NOT
  run in the trusted UI or credential process.

## History and automation

- **CORE-023:** Task and media history MUST use stable item IDs, record operation
  state and output references, support bounded search and pagination, enforce
  retention and deletion, protect private previews, tolerate missing files, and
  recover explicitly from corrupt indexes or stores.
- **CORE-024:** CLI, MCP, assistant, shell, and watch-folder automation MUST use
  shared versioned operations, authenticate callers where required, apply least
  authority, preserve human consent boundaries, bound concurrency, and expose
  correlation, progress, cancellation, and stable redacted errors.
- **CORE-025:** Watch folders MUST canonicalize and authorize roots, define file
  readiness and duplicate policy, avoid loops and traversal, persist checkpoints,
  isolate per-file failure, and stop cleanly without losing discoverable work.
- **CORE-026:** Assistant features MUST disclose provider and data flow, minimize
  transmitted content, require explicit authority for capture or history access,
  separate suggestions from actions, and require human review for sensitive or
  destructive execution.

## Diagnostics, distribution, and scope

- **CORE-027:** Diagnostics and recovery MUST use structured severity and
  correlation, redact secrets and private payloads, bound retention, support
  user-reviewed diagnostic export, detect corrupt state, and offer recovery
  without silently deleting valid configuration or user outputs.
- **CORE-028:** Native distribution MUST use platform-appropriate signed packages,
  honor the GPLv3 and dynamic LGPL Qt obligations, preserve the product identity,
  support clean install, replacement migration, update, rollback or recovery,
  and uninstall, and publish verified build provenance for Windows, macOS, and
  Linux.
- **CORE-029:** The pinned experimental mobile row is outside the BXIP001 native
  desktop scope. It MUST remain an explicit product-owner disposition and MUST
  NOT silently add a supported target, weaken desktop requirements, or be
  reported as completed functionality.
- **CORE-030:** Activation MUST include deterministic domain scenarios, security,
  privacy, accessibility, licensing, and compatibility review, permission and
  degraded-mode tests, cancellation and failure injection, and accepted results
  on Windows, macOS, and Linux for every linked ledger row.

## Baseline traceability and disposition

The capability parity ledger supplies the exhaustive discovery IDs linked to
this package. Existing filename, post-capture, region-capture, and ImageEditor
packages remain more-specific authorities. No source class or current platform
implementation becomes normative merely through this package.
