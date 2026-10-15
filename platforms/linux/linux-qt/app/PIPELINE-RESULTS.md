# Native pipeline result reports

Candidate implementation evidence for PCA-007/013/016. This does not accept
restart recovery, retry, privacy/accessibility review or full native parity.

`src/actions/PipelineResultJson` is the Qt Core-only production serializer.
The conformance adapter invokes it after the same action engine used by the
application. `CapturePipelineResult::canonicalResult()` supplies actual native
file facts to that serializer; no adapter defines result behavior.

The canonical fields remain `pipeline_state`, `final_working_media`,
`ordered_results` and `working_media_retained`, as published in
`post-capture-result-v1`. Each ordered result includes action/state and the
applicable diagnostic, missing/input artifact and cancellation-arrival fields.
Additional metadata lives in `extensions.com.xerahs.app`, version 1:

- `task_id` is the existing isolated task identity.
- `action_timings` contains actual UTC ISO timestamps, to milliseconds, only
  when execution timestamps exist. Dependency skips and actions prevented from
  starting receive no invented timestamps.
- `file_artifacts` contains canonical artifact identity, absolute native path,
  media type, actual integer byte length and the immutable naming context.
  Save and Save As retain separate file entries and actual creator IDs. Native
  consumers prefer Save, then Save As. Serialization never rereads, updates or
  removes a file or persists its live native identity binding.
- `ignored_selection`, when present, reports unknown selected IDs that did not
  execute. This is report data, not an ordinary diagnostic.

The result boundary accepts no raster bytes, clipboard payload, recognized text
or uploader credentials. Native executor exceptions have already become stable
value-free codes before serialization. Artifact paths and naming settings are
private report data and do not enter ordinary notifications or logs.

The shared Review Retained Capture Result action opens a native read-only
snapshot of the latest receipt. The main window and tray use the same actual
QAction. Native action/outcome columns show success alongside individual failures;
optional report details reveal the exact serialized JSON with a privacy notice.
The surface supports keyboard activation, focus, text selection and Escape/Close.
Opening/closing/review-owner destruction never retries an action, changes the
clipboard, modifies files or consumes the receipt. It does not open an editor
or require an editor opener. Later captures cannot rewrite this value snapshot.

The serializer itself grants no retry, deletion or media authority. The native
save/image-clipboard path now supplies its canonical results to a separate
[write-ahead journal and restart history](JOURNAL-RECOVERY.md). That record
preserves acknowledged outcomes and reports unacknowledged actions as
interrupted/unknown. It retains no raster bytes and re-executes nothing. [Explicit native retry](RETRY-POLICY.md) adds namespaced original/previous task
links, attempt index, preserved-success provenance and actual file creator IDs,
while current ordered results describe only this attempt. Full
media/artifact recovery, catalogue/prerequisite retry, configured retention, crash-temporary cleanup,
shared persisted compatibility and human/native qualification remain Required.

`PostCapturePipelineTest` independently checks canonical fields, catalogue order,
namespace isolation, exact integer metadata, UTC times and absence of invented
skip/cancellation times. All 17 published PCA vectors use this production path.
`CapturePipelineTest` checks a real private native save followed by clipboard
failure, exact file/context facts and absence of private exception/pixel payloads.
`PortalScreenshotTest` uses a private fixture bus and actual keyboard controls to
check the review surface, redacted outcomes, plain read-only JSON, Escape/owner
cleanup and unchanged receipt, clipboard, borrowed source and saved file.
Physical screen-reader/compositor qualification and human reviews remain Required.

[Native Save As evidence](SAVE-AS.md) verifies both real PNG artifacts,
canonical/journal facts and linked retries preserving the earlier creator and
bytes. The preferred single-file compatibility member does not truncate the
multiple-file report.

## Paired capture persistence checkpoint

[PAIRED-SAVE.md](PAIRED-SAVE.md) indexes explicit companion intent for both Save
aliases, typed raster/sidecar outcomes, known-PNG prerequisites without delete
authority, companion-only repair with original PNG creators, and write-ahead
known-PNG/unknown-companion restart review. Actual native/private-bus and
killed-process fixtures verify these additive candidate seams. Workflow defaults,
complete save/removal/format policy, shared persisted compatibility, catalogue
recovery and all native/human acceptance gates remain Required.

[Workflow executions](WORKFLOW-EXECUTIONS.md) now add an optional namespaced
workflow_execution_id to admitted region reports and write-ahead journals. It
links the separate pipeline task back to its pre-acquisition request/execution;
explicit retained retries keep that reference and their own pipeline lineage.
Reports without an injected ID and shared canonical vectors remain unchanged.
