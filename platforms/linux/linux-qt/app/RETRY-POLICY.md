# Native retained-capture retry

Candidate implementation evidence for PCA-011/010/012/013/014/015/016 and
CORE-005/010/023. Full catalogue/prerequisite retry, restart media recovery,
shared compatibility and human parity/release reviews remain Required.

## Explicit native operation

The tray and main window share **Retry failed capture actions…**. The command
is available only for an idle controller's current immutable pixel receipt
with known failed Save, Save As, image/file/path clipboard or reveal actions, or
dependency-skipped versions of those actions. File consumers require either the
original acknowledged Save/Save As or an originally selected failed save prerequisite. The Core planner uses catalogue order. It excludes successful,
cancelled and other skipped actions, rejects unsupported failed actions instead
of silently omitting them, and blocks interrupted/unknown outcomes or missing
pixels. A restart metadata record is not a pixel receipt or retry authority.

The native confirmation lists the actual selected actions, defaults to Cancel
and describes one attempt. Escape/Cancel starts no pipeline. Acceptance checks
the exact receipt again; a newer capture, busy controller or destroyed owner
cannot dispatch the old approved target. The main window opens/restores to show
activity and cancellation. Its WindowShortcut Escape and Cancel Capture use
the same controller token; modal dialogs retain their own Escape behavior.

Each accepted retry executes the same Core action engine and native worker/
GUI clipboard boundary as capture. There is one attempt per selected action
per explicit command, with no automatic loop. An already running uninterruptible
save may finish after cancellation; later selected actions do not start. True
outcomes, cancellation arrival, files and pixels remain available.

The retry reads the original receipt's directory, filename pattern and retained
pixels. It neither reloads current preferences nor reconstructs pixels from a
saved path. A failed save followed by successful clipboard retries save only.
A successful save followed by failed clipboard retries clipboard only and
keeps the saved file. Repeated failure requires another explicit command.
Cancelled actions are not retry targets; resumption of cancelled work belongs
to its separate lifecycle qualification. Editing retained pixels remains an
independent explicit operation. Retrying Save As that failed before PNG publication asks for a new PNG
path; it does not silently reuse a failed destination or repeat the action
selector. Acknowledged Save and Save As files both remain retained, with
Save-first consumer precedence. The [Save As evidence](SAVE-AS.md) records
actual picker, retry and multiple-file coverage.

## Append-only results and journal

`src/actions/PipelineRetry` owns eligibility and lineage decisions with Qt Core
only. Each attempt has a new task ID. `extensions.com.xerahs.app.retry`, version
1, records `original_task_id`, `previous_task_id`, the positive integer `attempt`
and catalogue-ordered `preserved_successful_actions`. These preserved successes
are provenance; they receive no invented current execution results/timestamps.
Only actions attempted in this run appear in its canonical ordered results.
The index fails before integer overflow; configured retry policies remain
Required with workflows. The original result and journal bytes stay unchanged.

Native saved-file facts optionally carry `creator_task_id`. A new successful
save identifies the current task; a retry's inherited file retains its actual
earlier creator, path, byte length and naming context. Older in-memory native
receipts without that field bind it to their originating task. These are
observations of successful publication, never deletion or upload authority.
Image-clipboard retry uses retained pixels without reading or modifying that
path. Native file consumers validate a separate live device/inode/size/timestamp
binding on the worker and GUI. That binding is not persisted or reconstructed
from history. Changed/unavailable files fail without repeating successful Save;
URI/path consumers can still observe later external changes.

The [write-ahead recorder](JOURNAL-RECOVERY.md) publishes retry lineage and known
files before new external work, even if cancellation arrived before start.
Known saved files require durable intent before clipboard dispatch. Storage
failure preserves the true existing file and media and emits value-free stable
codes. Successful prior actions are not repeated to repair storage. Reopening
a killed retry preserves the terminal original record and reports its new
pending action as interrupted/unknown. Recovery never converts unknown work
into failed work or executes it automatically.

The shared native result dialog shows the attempt number and earlier successes,
with full linked identity/file facts confined to optional private details.
Reviewing or forgetting metadata does not grant retry authority. History stays
read-only except for its explicitly confirmed unchanged-record removal.

## Verified scope and remaining gates

- `PipelineRetryTest`: independent native-scope eligibility/order, successful
  and cancelled exclusion, dependency skips, unavailable actions/media,
  conflicting artifacts, root/previous linkage, integer bounds and malformed
  canonical/journal links.
- `CapturePipelineTest`: actual private PNG retry, unchanged saved bytes,
  preserved successful clipboard/save, original settings/pixels, repeated
  failure and success, native GUI dispatch, durable links, initial cancellation,
  unknown-source rejection and failed private storage.
- `PortalScreenshotTest`: actual private-bus capture followed by native shared
  keyboard Retry/Cancel controls, unchanged borrowed/saved files and no new
  portal acquisition; stale confirmation, first-availability cancellation,
  owner destruction and read-only linked result review.
- `ApplicationWindowTest`: reachable Escape cancellation and modal precedence.
- `PipelineJournalTest` and its real-process fixture: kill a linked retry just
  before publication or inside clipboard dispatch, reopen both journals and
  preserve the earlier terminal result and unknown current outcome.

The 114 published vectors retain their approved expected values. No generic
catalogue prerequisite replay beyond Save/Save As, upload/network retry, annotation/
effect replay, cancelled-work resumption, durable failed-save settings schema,
restart pixel reconstruction, shared recovery/retry vectors, automatic retry
policy, retention/cleanup acceptance or physical accessibility/native matrix
qualification is inferred. Those remain in the full Linux audit, together with
the product/conformance-owner decisions and human review gates. No contract
requirement or platform disposition has been weakened or self-approved.

[Native file/path clipboard and reveal](FILE-OUTPUTS.md) now extend the chooser
and linked retry with original-save prerequisites and live file validation.
Uncertain reveal delivery interrupts the engine and retains pending journal
intent; it cannot authorize retry. Native identity is deliberately excluded
from persisted metadata. The linked evidence records verification and remaining
catalogue/shared/native/human scope. Both Save aliases now have actual
multiple-file prerequisite and creator preservation evidence in SAVE-AS.md.

[Native annotation handoff](ANNOTATION-HANDOFF.md) now retains its successful
document and rendered pixels through output retry. Acknowledged annotation
media is admitted without repeating the transformation, and its actual media
ID enters both the Core result and pending retry journal. Missing native
documents and unsupported failed transformations stay unavailable; full
transformation/catalogue replay and restart media authority remain Required.

## Paired capture persistence checkpoint

[PAIRED-SAVE.md](PAIRED-SAVE.md) indexes explicit companion intent for both Save
aliases, typed raster/sidecar outcomes, known-PNG prerequisites without delete
authority, companion-only repair with original PNG creators, and write-ahead
known-PNG/unknown-companion restart review. Actual native/private-bus and
killed-process fixtures verify these additive candidate seams. Workflow defaults,
complete save/removal/format policy, shared persisted compatibility, catalogue
recovery and all native/human acceptance gates remain Required.
