# Native capture Save As

Candidate production evidence for PCA-004/007/010/011/012/014/018/021 and
CORE-004/005/007/010. Full save variants, configured workflows, shared artifact
compatibility, supported native targets and human review/release gates remain
Required.

## Actual operation

**Capture region and choose actions…** now offers **Save image as new PNG…**.
It runs in the same catalogue-ordered worker pipeline as Save and clipboard
outputs. Default capture still runs Save then Copy image. Save As opens an
asynchronous, application-modal Qt Widgets file picker, transient to the
restored command window, without a nested event loop. The picker supports
directory navigation, PNG filtering, literal filenames and a visible accessible
status. It makes no automatic output or journal while awaiting a new path.
Qt's filesystem dialog and physical focus/assistive-technology behavior still
need qualification across the supported native matrix.

The Qt Core-only `src/actions/SaveAsTarget` validates a bounded absolute literal
path, rejects control characters, malformed Unicode and dot/dot-dot components,
adds `.png` when no suffix exists, and admits case-insensitive PNG suffixes.
User text is not expanded as a filename pattern. Other formats remain visibly
outside this candidate picker. The parent directory must already exist; the
save worker does not implicitly create it.

An existing destination keeps the picker open with a stable diagnostic. The
worker encodes once into an owner-private temporary in the chosen filesystem,
flushes its bytes, persists publication intent and uses `RENAME_NOREPLACE` for
one attempt at the exact chosen path. A racing file or dangling symlink cannot
be replaced; failure preserves its bytes and the capture. Live artifact identity
comes from the still-open published file descriptor, as for ordinary Save.
The candidate does not promise a directory-flushed raster transaction or
permanent path binding; filesystem/security qualification remains Required.

Cancel, Escape or picker close cancels **only Save As**. Independent later
actions continue; file consumers use an existing successful Save or report a
missing prerequisite. Cancel Capture requests the pipeline token, closes the
picker and prevents later actions. Worker encoding checks cancellation before
encoding, after encoding and immediately before publication; an uninterruptible
publication can still finish and its real result records cancellation arrival.
Owner destruction releases the worker and closes the dialog. Only the worker's
own unpublished temporary is cleaned during normal failure/cancellation; crash
cleanup authority remains unresolved.

## Multiple files, retry and recovery

The native immutable receipt now retains both `save.file` and `save-as.file`,
with actual paths, media types, lengths, contexts, live bindings and creator task
IDs. Its canonical report and journal serialize both artifact facts, excluding
native bindings. The compatibility `file` member remains the preferred file;
file/path clipboard and reveal resolve **Save first, then Save As**, following
PCA-018. A failed Save can fall back to successful Save As without inventing a
Save result. Notifications use the actual artifact for the action acknowledged.

Explicit shared Retry admits a failed Save As, reuses retained pixels and frozen
settings and asks for a new path. It never repeats a successful Save As, Save,
clipboard or other successful action to repair failure. A consumer can use an
original acknowledged file or the originally selected failed Save/Save As
prerequisite. A retry that newly succeeds at Save thereafter prefers that file
while preserving the previous Save As file and its original creator. Both
inherited prerequisite aliases supply availability without becoming files
created by the current attempt; the Core delete guard protects inherited-only
files. Native deletion remains unimplemented.

Before publishing Save As, the write-ahead journal records `save-as` and the
exact unconfirmed candidate. When Save already succeeded, its acknowledged
result/file precedes the new pending intent. Process-kill recovery retains that
earlier file and reports the pending Save As outcome as unknown; it never probes,
decodes, deletes or claims the candidate. The native version-1 parser now admits
`pending_save_path` for both Save aliases. Shared persisted schema/upgrade
compatibility remains a separate required qualification; records grant no live
file consumption or retry authority.

## Verification and concrete remaining decisions

`CaptureSaveAsTest` drives the actual picker through existing-file rejection,
folder navigation, literal suffix completion, Escape, global cancellation and
owner destruction. It checks actual PNG pixels, both artifact facts, Save
precedence/fallback, collision at publication, temporary cleanup, linked retries,
unchanged earlier files/creator IDs and private terminal journals.
`PortalScreenshotTest` acquires a private-bus fixture image, selects Save As,
observes failed encoding, then uses shared Retry and its actual picker to save
the retained pixels without another portal request. Core tests verify inherited
Save As prerequisites and guarded deletion. Real-process `PipelineJournalTest`
kills both Save-As-only and Save-then-Save-As runs before publication and reopens
their unknown pending records. Fixtures isolate preferences, buses, images,
outputs and journals; live Avalonia settings and real capture/upload commands
are untouched. All 37 CTest targets include the unchanged 114 published vectors;
exact pushed-revision baseline evidence comes from Ubuntu 24.04/Qt 6.4.2 CI.

The initial baseline run exposed a fixture selection error: Qt 6.4's
[`selectFile()` implementation](https://github.com/qt/qtbase/blob/v6.4.2/src/widgets/dialogs/qfiledialog.cpp#L972)
keeps a visible, focused filename field unchanged. The corrected fixtures move
focus to Save before Qt's public selection API, verify the actual chosen path,
activate the real Save control, and assert the controller's final chosen path.
They retain existing-file rejection and folder
navigation assertions. Directory navigation delegates to Qt's normal handler;
suffix validation remains in Core. The cached isolated Qt 6.4.2 runtime also
reproduced the original failure and verifies the corrected native cases.

Product/conformance owners still need to specify and qualify replacement consent
and transaction policy for an existing Save As destination, supported capture
formats/defaults and encoding options, raster/sidecar paired persistence,
shared artifact/diagnostic/recovery vectors and configured path/symlink policy.
This new-PNG candidate adds no accepted unavailable disposition or waiver.
Thumbnail save, annotation/effects handoff, full workflow persistence, remaining
catalogue/retry/media recovery, retention/cleanup and security/privacy/
accessibility/parity/release reviews stay in the full Linux audit. Next implement
the native annotation handoff and compatible re-editable persistence under
PCA-008, preserving its before-save dependency and honest raster save failures.
