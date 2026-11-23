# CORE-027 bounded retention across the private stores (decided 2026-11-23)

Decision ID: CORE-027-STORE-RETENTION-2026-11-23
Requirement: CORE-PLATFORM-001 CORE-027 (diagnostics and recovery MUST use
structured severity and correlation, redact secrets and private payloads,
bound retention, support user-reviewed diagnostic export, detect corrupt
state, and offer recovery without silently deleting valid configuration or
user outputs).
Decided by: the build loop under the maintainer's delegated owner-block
authority. No SPEC text change; the MUST stands and this record settles the
retention piece the CORE-027 row named ("full retention/recovery policy
across stores and outputs remains incomplete").

## Question

The diagnostics log is RAM-only and session-bounded, temp and recovery
retention is settled (decision ES-020-RETENTION-2026-11-23), and corrupt
records are quarantined rather than deleted. What bounds the retention of
the remaining persistent private stores - the workflow-execution records
and the pipeline journal that backs execution history and interrupted-upload
recovery - and what is the default?

## Research

- The behavioural reference (ShareX, CleanupManager.cs + ApplicationConfig.cs,
  read directly 2026-11-23): the in-app history itself has no retention, but
  the persisted history files are cleaned opt-in and count-based
  (`CleanupKeepFileCount = 10` keeps the newest files by write time). The
  reference never bounds in-app history by age.
- GNU coreutils `shred(1)` (fetched 2026-11-23 from man.archlinux.org): the
  same in-place-overwrite CAUTION already cited by ES-020's decision applies
  to the secure-delete option on these stores.
- This edition's landed invariants: the journal is "a write-ahead
  observation, never authority to repeat an action" and a leftover sending
  record "is never uploaded again automatically under the same key" - an
  old unconfirmed record has no restart authority, so an age bound destroys
  no product guarantee; and the stores already quarantine corrupt records
  rather than deleting them.

## Decision

- **`editorRetention.historyDays`** - default 30, integer 0..365; 0 keeps
  today's unbounded behaviour as an explicit user choice. The default
  satisfies the clause's MUST (retention IS bounded out of the box) with a
  month of redacted, value-free history records - generous against the
  reference's own ten-file cleanup bar.
- The bound applies to the workflow-execution records (by `startedAt`) and
  the pipeline-journal records (by `createdAt`) at startup, alongside the
  ES-020 sweeps, honouring `editorRetention.secureDelete` through the same
  shared storage primitive.
- The sweep runs under the stores' directory flock with their read and
  validate discipline: an unreadable, unsafe or invalid record is never a
  retention candidate - the quarantine paths own those - and no non-record
  file is touched. Removal is unlink (plus the optional overwrite); no
  capture artifact, setting or user output is ever a target.
- The History window simply shows fewer entries once records age out; that
  is the policy, not data loss of anything the product promises to keep.

## Sources

- ShareX `CleanupManager.cs` (count-based newest-first cleanup) and
  `ApplicationConfig.cs` (`CleanupKeepFileCount = 10`, opt-in toggles).
- GNU coreutils shred(1), https://man.archlinux.org/man/shred.1.
- This edition's landed journal semantics (PipelineJournal.h's
  write-ahead-observation note and the never-retried-automatically rule for
  leftover sending records) and NativePrivateRecords' quarantine behaviour.
