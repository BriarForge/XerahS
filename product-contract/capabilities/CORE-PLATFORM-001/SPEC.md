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

An acknowledged failure requires a started request, no prior-process `sending`
record, an HTTP response on every attempt, and no 2xx response on any attempt.
A 2xx status is evidence of acceptance, not of rejection; answered-attempt
accounting MUST still count it as answered. A failed upload with a 2xx response,
including an unparseable result or invalid result URL, is unconfirmed.

The first matching rule decides the publication:

1. A `sending` record left from an earlier process is `unconfirmed`, even when
   the new process's runtime fields are empty.
2. An upload that succeeded is `processed`. It MUST be checkpointed as complete
   and MUST NOT be uploaded again automatically under the same key.
3. A request that started and received a 2xx response on any attempt, without
   ultimately succeeding, is `unconfirmed`.
4. A request that started and received an HTTP response on every attempt, with
   no 2xx response on any attempt, is `released`. It MUST NOT be retried in the
   current session (P3) and MUST remain discoverable for a later session.
5. A request that started and has any unanswered attempt is `unconfirmed`.
6. A publication with no request started is `released` and MUST remain
   discoverable. This includes validation failure, a missing destination,
   cancellation before transfer, and an intent that was not saved.

An unconfirmed publication MUST NOT be uploaded again automatically under its
key. An implementation MUST NOT derive acknowledgement from the last status
alone. A later answered attempt does not repair an earlier unanswered attempt;
a later failed response does not erase evidence of an earlier 2xx acceptance.
An implementation unable to prove every attempt answered MUST treat the
publication as unconfirmed unless the upload ultimately succeeded.

### P2 Durable send intent

The intent to publish a watched file MUST be durably saved before the request
is dispatched, through the checkpoint work lane that already owns watch-folder
checkpoint writes. If the intent cannot be saved, the implementation MUST NOT
issue any HTTP request, MUST stop watching, and MUST report the failure as
`watch-store-write-failed`.

At startup, every leftover publication still recorded as `sending` is
`unconfirmed`, whatever the new process's runtime fields contain.

### P3 Retry eligibility and notices

A session is the life of the watch service in one application process, from its
first start to process exit. Stop and Start in the same process MUST NOT begin
a new session. An answered, released failure MUST NOT be retried in that
process whether it settled before or after Stop; a new process MAY retry it.
A request never started because of cancel or Stop is not a verdict and MAY be
retried at the next Start in the same process. Unconfirmed holds MUST remain
blocked across both Stop/Start and a process restart.

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

Retention MUST record each canonical parent directory's volume and object
identity: POSIX and macOS use `st_dev` and `st_ino`; Windows uses the volume
serial number and file ID. Both components MUST be decimal strings. Each
request MUST freeze the recorded parent map before worker observation.

For each key, the worker MUST observe parent identity before the file
fingerprint and again afterwards. The first matching rule applies:

1. A key with no observation facts (an unrecognized historic key) MUST be kept.
2. A `sending` key MUST be kept.
3. If either parent observation is missing, or the observations differ, the key
   MUST be kept.
4. If there is no recorded identity for the parent, the key MUST be kept. An
   unchanged file fingerprint MUST nominate its stable parent identity for
   adoption. Missing or changed files MUST NOT nominate an identity.
5. If the recorded identity differs from the observed identity, the key MUST
   be kept and the recorded identity MUST NOT be refreshed.
6. Otherwise the key MUST be kept when its fingerprint matches, and pruned
   when its fingerprint differs or the file is absent.

The output parent map MUST retain recorded identities referenced by kept keys.
It MUST also include a nominated identity for an unrecorded parent only when
all nominations for that parent agree. Conflicting nominations MUST adopt
nothing for that parent. Identities unreferenced by kept keys MUST be dropped.
An identity adopted in this write MUST NOT authorize pruning until a later
write. The GUI MUST adopt worker results only under the existing snapshot
fence.

Legacy keys can qualify through an unchanged sibling under their parent.
A recorded identity MUST NOT be refreshed even after a directory is recreated.
Such conservatively kept entries remain bounded by the 16 MiB store limit,
which fails closed with `watch-store-full`; a refresh policy is separate work.
A file that was pruned after deletion and later reappears with the same size
and modification time is treated as new and is accepted again.

### P6 Watch store format

The watch checkpoint store is version 2:
`{"items":[keys],"publications":[{"key":...,"state":"sending|unconfirmed","at":...}],"parents":[{"path":...,"device":...,"inode":...}]}`.

A missing `parents` field MUST read as empty, for version 1 and earlier
unpublished version 2 files. A present `parents` field MUST be an array. Every
entry MUST have an absolute string `path` and string `device` and `inode`
components of 1–20 ASCII digits. Duplicate paths or malformed entries MUST
fail closed with `watch-store-invalid-version-or-data`. Every version 2 write
MUST include `parents`.

Version 1 MUST remain readable and MUST upgrade on the next write. A reader
that only understands version 1 MUST refuse version 2 with
`watch-store-invalid-version-or-data` and MUST NOT republish watched files.
The watch rules store stays at version 1. This pre-release format adds no
live-migration user interface.

### P7 Implementation qualification

Linux CORE-025 requires the real-process SIGKILL proof on tmpfs, the approved
Qt 6.4.2 baseline, and a real mount-namespace proof that processed keys and
unconfirmed holds survive an unmount whose mountpoint directory remains.
Remount and later safe pruning MUST be proved too. These proofs qualify local
mount replacement within one boot only. Cross-boot filesystem identity stability,
removable-media identity reuse, and network/FUSE identity stability remain Layer 5
activation qualification; an unstable identity MUST keep entries. Periodic scanning
remains the notification fallback.

## Baseline traceability and disposition

The capability parity ledger supplies the exhaustive discovery IDs linked to
this package. Existing filename, post-capture, region-capture, and ImageEditor
packages remain more-specific authorities. No source class or current platform
implementation becomes normative merely through this package.

## Changelog

- **0.6.0, 2026-11-14:** Maintainer Aoife approved the unpublished CORE-025
  candidate and its amendment. Failed uploads with any HTTP 2xx response are
  unconfirmed while genuine answered attempts remain counted. Durable intent
  precedes dispatch; parent volume/object identities guard retention across
  unmounts, and identities are adopted only for later writes. Checkpoint v2
  adds optional-on-read, mandatory-on-write `parents`; rules stay v1. A session
  lasts for one application process, with unsent-cancellation retry preserved.
  Explicit uploads remain independent of watch holds. Contract 0.4.0 and this
  package version are amended in place before publication. No activation,
  census closure, release or other-platform implementation is approved.
