# Native workflow execution observations

WFCAT-002/003/004/006/014/017 candidate behavior for the currently implemented
region workflows. This does not activate the complete workflow catalogue or
establish shared CLI schema, security/privacy approval or release acceptance.

## Running and looking up an execution

```sh
xerahs --run-workflow FULL_WORKFLOW_UUID --json
xerahs --workflow-result REQUEST_UUID --non-interactive --json
```

The launch reply remains an admission acknowledgement. Its requestId is now the
stable invocation/execution ID for an admitted capture. Each pipeline keeps a
separate task ID. The explicit read-only lookup always prints one compact JSON
object and newline; exit 0 means the record was read, including failed/cancelled
executions. Exit 1 means invalid arguments/ID, missing, invalid or inaccessible
storage. --json suppresses fixed human hints. Help, lookup and discovery need no
graphical display, instance election, preferences or desktop service. Conflicting
operations fail before data initialization. Reading creates no missing store.

Lookup returns IDs, admission start time, trusted entry boundary, terminal/last
recorded state, optional finish time, ordered action/state pairs and an optional
pipelineTaskId. It excludes workflow names, settings, directories, filename
patterns, file paths, private exception text and unknown fields. Explicit native
retained-result/history details retain the existing private file facts and now
include extensions.com.xerahs.app.workflow_execution_id. The reference is metadata,
not permission to open/delete/retry a saved artifact. Old reports and all published
vectors are unchanged when no execution ID is supplied.

## Admission, acquisition and outputs

A request-aware Core dispatcher callback covers explicit saved workflow, default
region and choose-actions captures. Before ACK, the native controller resolves
and validates the saved definition, freezes effective output settings and
atomically creates a private execution record. Busy/modal admission creates no
record. Duplicate existing IDs cannot overwrite a record or start acquisition.
The bounded authenticated activation receipt still prevents redispatch; this
adds no durable exactly-once or resend guarantee.

startedAt is the admission time, before acquisition. Origins are assigned at the
native boundary: command-line for primary startup, external-activation for
same-user IPC (which cannot prove a caller is a CLI), native-shell for shared
GUI/tray actions, and global-shortcut for accepted desktop shortcut presses.
No wire payload can claim an origin, permission or human approval.

Normal release of an undispatched prepared capture records not-started. Capture
start is persisted before the portal request. Portal/selection/composition
cancellation, permission denial and source failure finish the same execution
without inventing a pipeline or output. After valid pixel acquisition, the
controller allocates the real pipeline task ID and persists its link before
starting post-capture actions. The pipeline's write-ahead journal also records
the execution reference before external actions. Actual receipts supply ordered
step states; cancelled/interrupted pipelines keep those outcomes. Completed
pipelines with failed/skipped actions report partial; completed actions alone
report succeeded. No new action ordering or failure policy is introduced.

A failed admission/start/link write prevents the next capture/action boundary.
File uploads also stop before HTTP when admission, start or the read-file step
cannot be persisted. FileUploadTest injects each of those three failures and
checks zero requests, no receipt and agreement with the last durable execution
observation. A failed terminal write after an acknowledged upload still keeps
the known remote result and never replays it.
Failed terminal persistence preserves completed files and retained pixels and
reports that the execution result was not confirmed. No write error silently
turns into success or replays an action. Normal early outcomes are persisted
before result presentation. Destruction during asynchronous capture/processing,
abrupt exit, interrupted persistence and process restart never infer a result
from absence: lookup returns state: outcome-unconfirmed, terminal: false and
runtimeStatus: not-queried with the last durable phase. This also applies to
currently running captures; this query does not contact the primary. It never
resumes, synthesizes failure, requests consent or repeats a side effect.

## Private storage and evidence

Records are separate from PCA journals at
$XDG_STATE_HOME/com.xerahs.app/workflow-executions (standard ~/.local/state
fallback). The existing descriptor I/O is extracted into NativePrivateRecords;
NativePipelineJournal retains its format/parser, sort order, public API and error
codes. Both stores retain no-follow ancestor/file traversal, owner/private-mode
and single-link checks, independent nonblocking directory locks, exact-byte CAS,
no-replacement creation, atomic replacement and file/directory fsync. Each record
is at most 1 MiB, a store at most 1024 records, and listing at most 16 MiB. Capacity
fails closed without pruning; retention/history management remains Required.
Settings include only known fields and no image bytes/unknown extensions.

WorkflowExecutionTest covers private admission/reopen, abandoned and in-flight
ownership, duplicate/stale writes, redacted states/order, malformed/future/unsafe/
symlink storage and actual headless CLI lookup. PortalScreenshotTest covers actual
new-primary/forwarded acknowledgements and headless denial lookup against a
private non-activating portal, real controller PNG output and immutable settings,
linkage before publication, cancellation/denial/source/composition failure,
destruction and storage faults at admission/start/link/terminal boundaries.
Existing killed-worker pipeline journal tests exercise the extracted storage.
All fixtures use temporary files, private sockets/buses and synthetic pixels;
no live capture/upload, Avalonia config or compositor files are touched.

The audit/checkpoint report records full native/vector/tooling and exact pushed
Ubuntu 24.04/Qt 6.4.2 CI verification. No CORE/WFCAT vectors are published yet.
Full status taxonomy/action families, live progress/wait/cancel APIs, unified
execution/history retention, complete retry invocation/attempt indexing, all
entry points, settings/acquisition coverage, platforms and human privacy/security/
accessibility/parity/release gates remain Required. Explicit retained retries keep
the original execution reference and their own existing pipeline attempt lineage;
they do not rewrite the original execution outcome. Next add reviewed metadata
retention controls and atomic filename-counter reservation, then remaining
capture families and the rest of the full contracted application.

## Consistent terminal surfaces

Capture acquisition outcomes (including denied, cancelled, failed and unconfirmed
storage), capture-action results, recordings, file uploads and watch-folder
copy/move/upload now publish the same terminal state and opaque output references
to logs, native shell activity, notifications and durable workflow history.
Partial results never announce completion. Private result windows retain the
usable files or upload URLs; ordinary observations contain no filenames, paths,
URLs, credentials or payload text. Fixed diagnostic guidance remains available.
File operations read the last successfully saved execution snapshot for this
projection, so a failed step/terminal write cannot publish an absent output
reference or announce success. Actual completed effects stay retained and watch
uploads remain checkpointed against replay even if terminal storage failed.

A watch-folder publication that cannot be confirmed publishes an unconfirmed
terminal state. The state is recorded in the durable watch checkpoint store
rather than in the upload result, because the uncertainty survives the process:
a request that was started and never answered may or may not have reached the
destination. A failed upload that received any HTTP 2xx is also unconfirmed,
because acceptance can precede an unusable result URL. An unconfirmed publication is never checkpointed as processed, so
it is not uploaded again automatically under the same key. Its notice names the
file and discloses no path, no remote URL and no other location detail. An
explicit Upload file command is independent of the watch hold. Changing the
file gives it a new fingerprint and watch key. Neither action silently clears
the hold on the original key.

Real native tests compare all four projections: PortalScreenshotTest covers
pre-pipeline cancellation/denial/failure and partial action results;
RecordingWorkflowTest covers Stop, Abort, denial, encoder failure and unconfirmed
storage; FileUploadTest covers successful/failed/cancelled uploads, real shell
activity and terminal-write failure; WatchFolderServiceTest covers copy/move,
partial removal, cancellation, upload and admission/start/step/terminal faults.
This closes the former file-operation log/progress omission. WFCAT-017 remains
partial for the full catalogue: utility/application-control families and
unimplemented admitted workflow families still need the same lifecycle coverage.

Watch retention records each parent directory device/inode as decimal strings.
Every checkpoint worker freezes the recorded map and observes parent identity
before and after the file fingerprint. A changed, missing or unqualified parent
keeps entries; qualification from an unchanged file authorizes only a later
write. This preserves publication holds across unmount/remount.

An answered watch-upload failure remains attempted for the life of the process,
including Stop/Start before settlement finishes. A new service can retry that
released failure. Work stopped before sending remains retryable on Start.

CORE-025 is implemented on Linux; activation blocked by CORE025-FU-IDENTITY.
Cross-boot btrfs identity stability and possible removable-media identity reuse
remain mandatory activation qualification, with maintainer review by 2026-12-14.
