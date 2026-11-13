# CORE-PLATFORM-001 Native platform capability foundation

Version: 0.6.0

Status: Approved by the human product owner on 2026-08-31 and 2026-09-28 (0.4.0 Linux editions; 0.5.0 mobile targets); the CORE-025 watch-publication and retention subsection was approved by the maintainer on 2026-11-14 and is conformance-required before activation

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
- **CORE-029:** Native Android and iOS applications are supported targets
  governed by `MOBILE-PLATFORM-001`, which declares each capability's mobile
  scope. The baseline's experimental mobile projects are replaced by those
  applications and remain outside desktop parity measurement. Mobile scope
  MUST NOT weaken any desktop requirement.
- **CORE-030:** Activation MUST include deterministic domain scenarios, security,
  privacy, accessibility, licensing, and compatibility review, permission and
  degraded-mode tests, cancellation and failure injection, and accepted results
  on Windows, macOS, and Linux for every linked ledger row.
- **CORE-031:** Every repository-owned NuGet dependency MUST be checked against
  its authoritative package source whenever it is touched, before release
  qualification, and when the last recorded repository-wide check is more than
  30 days old. Agents MUST adopt the latest published stable release, including
  major versions, without case-by-case approval and MUST update the code,
  project files, central version declarations, lockfiles, tests, and
  documentation required for a conforming adoption.
- **CORE-032:** Each NuGet check MUST record source, check time, previous version,
  latest stable version, selected version, and verification result. Adoption
  MUST pass applicable restore, build, test, conformance, security, licensing,
  and packaging checks. A temporary retention below latest stable MUST identify
  the exact rejected version, protected-boundary or toolchain blocker, evidence,
  owner, remediation, and next review no more than 30 days away; silent or
  indefinite pinning is prohibited.

## Linux editions

- **CORE-033:** The Linux platform MAY be delivered as one or more native
  editions, each a complete application for a named desktop family and set of
  distributions. Every edition MUST satisfy every requirement that applies to
  Linux on each of its declared targets. An edition MUST NOT narrow product
  behavior; environment differences are expressed only through accepted
  dispositions.
- **CORE-034:** Each edition MUST have a profile in `linux-editions.json` that
  declares its ID, status, target distributions and versions, desktops, display
  server, UI toolkit and deciding architecture record, capture, shortcut,
  notification, tray, and credential mechanisms, and packaging formats.
- **CORE-035:** All editions MUST share the identity `com.xerahs.app`, the XDG
  configuration, data, cache, and state locations, the settings schema, and all
  persisted formats, so a user can change edition or distribution without a
  migration. Installing another edition MUST replace the installed edition
  rather than create a second identity.
- **CORE-036:** A requirement's Linux disposition is accepted only when every
  edition with status `supported` has an accepted disposition for it. An
  edition-specific Degraded or Unavailable disposition requires product-owner
  approval like any other platform disposition. Every edition MUST run the
  shared conformance vectors on each declared target distribution; editions
  MUST NOT fork expected results.
- **CORE-037:** An edition MUST follow its desktop's native conventions for
  layout, theming, accent colour, keyboard behavior, portals, notifications,
  status or tray presence, and configuration integration wherever they do not
  conflict with this contract. A candidate edition becomes release-blocking only
  when the product owner promotes it to `supported`.

## CORE-025 watch publication and retention

This subsection decides the outcome of a watch-folder publication, the evidence a
new process inherits after an unclean stop, and the retention rules for the
watch checkpoint store. It refines CORE-025 and creates no new requirement ID.

The normative evidence for this subsection is `test-vectors.json`, which
exercises two operations: `watch-publication-disposition` decides the
disposition of one publication, and `watch-retain` decides which checkpoint
entries survive a checkpoint write.

### P1 Publication disposition

A watched file's publication is acknowledged when the upload succeeded, or when
at least one request was started, no prior-process sending record survived a
restart, and every request attempt for that publication received an HTTP
response of any status. An acknowledged failure releases the file. If any
attempt ended without an HTTP response (timeout, network error, abort, or lost
reply), the publication is unconfirmed unless the upload ultimately succeeded,
whatever the later attempts returned. An implementation that cannot show that
every attempt was answered MUST treat the publication as unconfirmed. A sending
record left over from an earlier process is always unconfirmed, even when the
new process's runtime fields are empty.

The disposition of one publication attempt is decided by the first matching
rule, in this order:

1. A publication left over as `sending` from an earlier process is
   `unconfirmed`.
2. A publication that succeeded is `processed`. It MUST be checkpointed as
   complete and MUST NOT be uploaded again automatically under the same key.
3. A publication for which at least one request was started and every request
   attempt received an HTTP response, but which did not succeed, is `released`.
   It MUST NOT be retried again in the current session and MUST remain
   discoverable, so that a later session can find the work again.
4. A publication for which at least one request was started and any attempt
   ended without an HTTP response is `unconfirmed`. It MAY or MAY NOT have
   reached the remote, and it MUST NOT be uploaded again automatically under
   the same key.
5. A publication for which no request was started is `released`. A publication
   that was never sent includes validation failure, a missing destination,
   cancellation before transfer, and a send intent that was not saved. It MUST
   remain discoverable.

An implementation MUST NOT derive the acknowledged condition from the last
attempt's status alone. A later answered attempt does not repair an earlier
unanswered attempt.

### P2 Durable send intent

The intent to publish a watched file MUST be durably saved before the request
is dispatched, through the checkpoint work lane that already owns watch-folder
checkpoint writes. If the intent cannot be saved, the implementation MUST NOT
issue any HTTP request, MUST stop watching, and MUST report the failure as
`watch-store-write-failed`.

At startup, every leftover publication still recorded as `sending` is
`unconfirmed`, whatever the new process's runtime fields contain.

### P3 Retry eligibility and notices

Retry eligibility MUST remain controlled by explicit user action. The user MAY
use the existing Upload file command for an explicit upload independently of the
watch key, or change the file, which gives it a new fingerprint and therefore a
new key. This subsection adds no new user interface.

During a session, an `unconfirmed` publication MUST be named in the current
notices, together with the file it names. At startup, the implementation MUST
show one `watch-publication-unconfirmed` notice that names up to five base
names and then reports how many further names remain. The notice MUST NOT
disclose directory paths, remote URLs, or any other location detail.

### P4 Local copy and move are unchanged

Local copy and move behavior is unchanged by this subsection. A crash before
the checkpoint is written MAY repeat the copy. Generated output names MUST NOT
overwrite an existing file, and a move MUST remove the source only after the
copy has committed.

### P5 Checkpoint retention

Each checkpoint write MUST prune a `processed` or `unconfirmed` entry when the
entry's parent directory exists AND the file's current fingerprint differs from
the fingerprint recorded in the key. The entry was then deleted, moved, or
changed.

A `processed` or `unconfirmed` entry whose parent directory does not exist MUST
be kept, because the parent may be an unmounted or removed root rather than a
deleted file.

An entry recorded as `sending` MUST NEVER be pruned.

A file that is deleted and later reappears with the same size and modification
time is treated as a new file and is accepted again.

### P6 Watch store format

The watch checkpoint store is version 2 and holds
`{"items": [keys], "publications": [{"key": ..., "state": "sending" | "unconfirmed", "at": ...}]}`.

A version 1 store MUST be readable, and MUST be upgraded on the next write.
An implementation that only understands version 1 MUST refuse a version 2 store
with `watch-store-invalid-version-or-data` and MUST fail closed without
republishing any watched file.

The version of the watch rules store is unchanged.

This is a pre-release format. It carries no live-migration user interface.

### P7 Implementation qualification

CORE-025 is implemented on Linux only after a real process `SIGKILL` proof on
`tmpfs` and the approved Qt 6.4.2 baseline. Network and FUSE qualification is
a separate Layer 5 concern, and a periodic scan fallback exists.

## Baseline traceability and disposition

The capability parity ledger supplies the exhaustive discovery IDs linked to
this package. Existing filename, post-capture, region-capture, and ImageEditor
packages remain more-specific authorities. No source class or current platform
implementation becomes normative merely through this package.

## Changelog

- **0.6.0, 2026-11-14:** Maintainer Aoife approved the CORE-025 watch
  publication and retention subsection. A publication is acknowledged when it
  succeeded, or when at least one request was started, no prior-process sending
  record survived a restart, and every attempt received an HTTP response of any
  status. An acknowledged failure releases the file; an attempt that ended
  without an HTTP response is unconfirmed unless the upload ultimately
  succeeded; an implementation that cannot prove every attempt was answered
  MUST treat the publication as unconfirmed. The send intent MUST be durably
  saved before dispatch, every startup leftover `sending` record is
  unconfirmed, retry eligibility resolves through an explicit Upload file
  command or a changed file with a new fingerprint, local copy and move are
  unchanged, checkpoint retention prunes by
  parent existence and fingerprint change, and the watch checkpoint store
  becomes version 2 with a version 1 read and next-write upgrade. Contract
  0.4.0 records the decision and the vectors. Linux implements it first, and
  only after a real process `SIGKILL` proof on `tmpfs` and the approved Qt
  6.4.2 baseline; macOS and Windows remain unimplemented and MUST follow this
  policy when built. No platform waiver, activation, census closure or release
  approval is granted. See
  [decision CORE-025](../../decisions/CORE-025-watch-publication-retention.md).
