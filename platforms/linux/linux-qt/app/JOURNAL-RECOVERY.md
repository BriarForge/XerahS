# Native capture result journal and recovery

Candidate implementation evidence for PCA-012/013/015/016 and CORE-023.
This covers the native Save/Save As/clipboard/reveal production paths. Full
history, all action recovery, shared persisted compatibility and human gates
remain Required.

## Production behavior

The application supplies `NativePipelineJournal` to its real capture
controller and worker pipeline. `src/actions/PipelineJournal` owns the
Qt Core-only write-ahead decisions, serialization and recovery interpretation.
The native boundary performs file I/O; the history and result dialogs present
immutable observations and never execute a recovered action.

Before native save starts, an owner-private record contains the task identity,
UTC creation time, frozen catalogue-ordered selected action IDs, completed
results and the pending action. Before PNG publication, the record also holds
the candidate absolute destination. This path is **unconfirmed** until the save
returns success; a journal path alone grants no file ownership or deletion
authority. Actual successful save metadata and actual action timestamps use
the same canonical serializer as the adapter and in-memory receipt.

Each acknowledged action updates the journal. Once a saved artifact exists,
the next clipboard effect requires another durable intent. The terminal
snapshot records completed or cancelled results, including the remaining
actions prevented from starting by cancellation. A failed journal write leaves
the last durable observation intact. The in-memory receipt still reports the
true saved file, pixels and stable storage diagnostic. No failure changes an
already successful save into a failed save. When no durable artifact or network
action exists, journal failure does not prevent an independent clipboard after
failed save. Failed intent before publication prevents publication.

On restart, a nonterminal record is `interrupted`. Its pending action has an
unknown outcome, rather than an invented failure. A publication candidate may
or may not exist; recovery does not probe, decode, overwrite or delete it.
Acknowledged earlier action results stay intact. Unstarted later actions remain
distinct from the unknown action. Terminal results retain their actual state.
The durable report sets `working_media_retained` to false: result metadata
survives, while unsaved pixels are not silently persisted or reconstructed.
Current retained pixels and the journal are separate lifetimes.

## Storage boundary and privacy

Records live in `$XDG_STATE_HOME/com.xerahs.app/pipelines`, or
`~/.local/state/com.xerahs.app/pipelines` when the environment value is absent
or relative. No store is created by startup or read-only history inspection.
The versioned envelope is `com.xerahs.app/pipeline-journal`, version 1. The
canonical result remains inside `result`; recovery observations appear only in
`extensions.com.xerahs.app.recovery`. No pixels, clipboard payload, OCR text,
credentials or exception messages enter this production boundary. Saved paths
and filename context are private metadata, revealed through explicit details.

The native boundary traverses absolute directory components with descriptor
relative `openat` and `O_NOFOLLOW`; it rejects symlinks, non-private/unowned
stores and non-regular, shared, hard-linked or oversized journal files. It
does not repair unrelated directories' permissions. Independently opened
directory descriptions carry advisory locks. Writes compare exact previous
bytes, create a 0600 exclusive temporary, flush the file, atomically rename and
flush the directory. New identities use `RENAME_NOREPLACE`. A directory-flush
failure reports durability unknown instead of inventing a successful commit.
The file/directory flushing distinction follows the
[Linux fsync contract](https://man7.org/linux/man-pages/man2/fsync.2.html).

Each record is bounded to 1 MiB. New records stop at 1,024 existing identities
without silently pruning results; updates remain possible. History inspection
is bounded to 1,024 identities and 16 MiB of encoded input, reports any limit or
read error and preserves undisplayed records. Complete pagination/resource
policy remains Required. Corrupt, unsafe and newer records remain untouched and
unavailable. Readers never interpret a record as retry or artifact authority.

## Native recovery surface

The main window and tray share **Capture result history…**. A worker loads the
records without blocking the GUI; Refresh reloads their actual current state.
Reads defer while capture runs, and a read racing a new capture reloads after
completion, so a live write-ahead observation is never presented as a restarted
interrupted pipeline.
Review opens a read-only value snapshot using the same outcome/details dialog
as the latest retained capture. Keyboard activation, cancellation, plain text
and accessible widget names remain native Qt behavior.

**Forget record…** defaults to Cancel and explains that recovery evidence will
be lost. Acceptance removes only the selected unchanged metadata record. A
stale confirmation, malformed record or active capture cannot authorize its
removal. Saved files, clipboard contents, retained pixels and editor documents
are not affected. Reading, closing, reviewing and destroying the surface never
retry an action or change artifacts. Forgetting has no automatic expiry policy.

## Verification and remaining decisions

`PipelineJournalTest` covers canonical recovery, future/corrupt records, exact
snapshot conflicts, permissions, symlink ancestors/files, FIFO rejection,
capacity back-pressure, failed intent, post-save journal failure, cancellation
and actual saved pixels. Its real-process fixture is killed before PNG
publication or inside native clipboard dispatch, then a fresh store recovers
the last durable observation. No capture portal or real uploader is invoked.
`PipelineHistoryDialogTest` covers native keyboard review, hidden private
details, Escape, cancel/accept/stale forget, busy availability, artifact
preservation and unknown upload presentation. New fixtures use private buses
and temporary stores/files. Published PCA vectors keep their independent
expected values and use the unchanged canonical engine/serializer.

The following owner/conformance clarifications remain concrete release work:

1. Decide the configured journal/media retention fields, default lifetime,
   privacy opt-out behavior, capacity/pagination policy and whether unsaved
   working media is ever retained across restart. No implicit pixel persistence
   or automatic forgetting is inferred from PCA-012.
2. Decide task-bound temporary authority and cleanup after crash, including
   the interval between publication and its acknowledgement. Existing private
   `.xerahs-capture-*` and journal `.pending-*` crash temporaries are preserved;
   a guessed pathname/prefix is not permission to delete user data.
3. Qualify a shared persisted schema and independently derived recovery/retry
   vectors, linked retry identity, prerequisites and explicit resolution of
   unknown network outcomes across platforms/editions. Upload and managed
   temporary-file native execution remain unimplemented; presentation of an
   injected unknown upload is not production uploader evidence.

[Explicit retained-pixel save/clipboard retry](RETRY-POLICY.md) now creates a
new linked journal, preserving original/previous identity, actual file creator
and earlier successful actions. Restart records themselves never dispatch retry.

[Native per-run selection](ACTION-SELECTION.md) updates the recorder's effective
plan before a chosen output begins. A real-process kill before selected PNG
publication recovers the successful selector and unknown save; deselected Copy
is absent. Cancelled selection creates no output or journal store. The native
fixture keeps the production tray lifetime when the chooser closes.

Full media reconstruction, catalogue/prerequisite retry, search/pagination/retention, all artifact and
action types, upgrade/recovery compatibility, filesystem/distro/compositor
qualification and human security/privacy/accessibility/parity/release approvals
remain Required. No gate is self-approved by these tests or this record.

[Native Save As](SAVE-AS.md) now records its exact unconfirmed path before
publication and retains both acknowledged Save aliases in the canonical file
array. An unopened Save-As-only choice creates no journal; when an earlier file
exists its metadata remains durable before the picker. Real-process fixtures
kill Save-As-only and Save-then-Save-As attempts before publication, preserving
the earlier acknowledgement and unknown pending Save As. This native parser
extension still needs shared format/upgrade qualification; it grants no restart
pixel, consumption or retry authority.

[Native file/path clipboard and reveal](FILE-OUTPUTS.md) now extend the chooser
and linked retry with original-save prerequisites and live file validation.
Uncertain reveal delivery interrupts the engine and retains pending journal
intent; it cannot authorize retry. Native identity is deliberately excluded
from persisted metadata. The linked evidence records verification and remaining
multiple-artifact/catalogue/shared/native/human scope.
