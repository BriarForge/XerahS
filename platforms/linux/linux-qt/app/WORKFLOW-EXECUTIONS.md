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
