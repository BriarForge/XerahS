# Owned New Image/Open preparation and replacement

Partial Linux native candidate evidence for EC-001/002/017 and ES-001/002/005/
006/007/008/016/019/029. Full requirements, target matrix and human/release gates
remain Required. Shared source/geometry/colour/persistence policy, schemas,
vectors, statuses and dispositions are unchanged.

## Production behavior

New Image owns each worker's requested dimensions/background/profile, initial
pixels, path, label, cancellation/progress state and result. Workers capture no
UI, session or stack progress references. Cancel Work retires the job and restores
the wizard immediately, preserving the prior initial input and chosen controls.
A new load/Create can succeed while cancelled work exits; late success/exception
cannot replace that result. Escape, reject, child/owner destruction and ordinary
completion never join unfinished decoding/composition/encoding. Failed work is
redacted and retryable. Create completes only after a prepared document exists.

Initial pixels, dimensions, label, validation status and Create availability are
published together with reentrant starts blocked. Native file and alpha-colour
pickers are owned and weakly guarded. Rejection, picker/owner destruction, closing
the wizard during a picker and cleanup observers deleting its parent all return
safely. File names/status render as plain text. Clipboard input remains explicit;
file input continues to ignore editing sidecars.

Open now uses the shared native owned preparation boundary. Path/document/result
values outlive Cancel, progress deletion and forced owner teardown; no future or
stack atomics remain borrowed by the worker. Existing typed failure diagnostics
are retained, and exceptions disclose no decoder/path details. Current/embedded
source choice is owned, defaults to Cancel and uses the already validated
snapshots. Its second preparation stage uses the same owned boundary.

A QImage can wrap external bytes. Native initial/retained pixel ingress validates
bounds and copies those pixels before returning control to a cancellable event
loop. Caller buffer release cannot invalidate a worker. This is a bounded ingress
copy on the GUI thread; decoding, colour conversion, composition and PNG/document
preparation remain on workers. Large-ingress copy latency, allocation pressure,
codec cancellation latency and process shutdown still require qualification.

Main-editor New/Open freeze state ID, exact selection and document generation
before native busy notifications. They guard outputs, mutations, nested entry,
Open and ordinary Close while preparing, confirming and publishing. Stale work
keeps the current session and reports that the image was not replaced. File,
confirmation and error dialogs are owned; standalone creation/opening under an
editor parent also survives forced parent deletion without member rollbacks or
publishing a new window.

Preparation still precedes dirty replacement consent. Save/Discard/Cancel keeps
its existing meaning; failed or cancelled Save prevents replacement. Saving over
the selected candidate reloads the just-saved raster/sidecar before adoption.
Successful New starts an unsaved untitled session; successful Open adopts the
validated saved session. Source/profile/alpha, document fields/assets and private
sRGB display are published together while the prior session remains alive until
the canvas switches references. Rejected/failed work preserves exact old history,
selection, dirty checkpoint, viewport and files, apart from an explicit Save.

## Verification

Seventy-four new native cases cover:

- `NewImageDialogTest`: 27 cases for both late cancelled load/Create outcomes,
  reject/Escape/child/owner teardown while work is blocked, frozen options and
  borrowed pixel buffers, fixed exception text/retry, file/colour picker acceptance
  and destruction, complete input publication and reentry refusal.
- New `EditorOpenTest`: 26 cases for file/retained pixels/retained document work,
  Cancel/reject/child/owner/cleanup recovery, redacted exceptions, private progress
  accessibility, released caller buffers and both explicit source choices with
  cancellation/destruction. Temporary PNG/.xann bytes remain unchanged.
- `EditorWindowTest`: 21 cases for New/Open blocked-worker release, early stale
  selection refusal, owned replacement/file-picker destruction, cleanup observers
  deleting the window and standalone editor-parent destruction. Exact source/
  fields/state/history/selection/title/status/viewport/files and restored controls
  are checked wherever an editor survives.

Existing real file/clipboard inputs, numeric/keyboard creation, independent alpha/
profile/crop/padding pixels, resource refusal, dirty replacement and failed-save
recovery, overlapping explicit-save reload and actual PNG/.xann save/reopen remain
verified. The new small Open target isolates its native coordinator checks while
the complete main-editor target retains its 30-second bound.

All 45 targets are verified on local Qt and cached Qt 6.4.2. The broader attempts
passed 44 targets; two new stale-selection rows tried to edit an already disabled
canvas and reached their deadlines, timing out the editor target. The corrected
injection uses the established early QAction notification boundary. The complete
final editor target passes in 24.66 s locally and 24.39 s on Qt 6.4.2. Complete
final New Image/Open targets pass in 2.15 s locally (1.43/0.71) and 1.95 s on the
baseline (1.28/0.67); 42 unchanged successful targets were retained. These are
verification sequences, not single final full runs. No timeout was increased.
All 114 unchanged published vectors and 32 tooling tests pass.

A new replacement fixture initially compared an untagged input QImage against the
correctly tagged sRGB output; it now asserts independent blue pixels, dimensions
and the selected profile. Missing test includes were corrected. Original outcome
assertions and shared expectations remain unchanged. Tests explicitly use Qt
picker widgets, private buses/preferences and temporary files; production still
honors desktop theme helpers. Synthetic New Image controls/policy/alpha/profile/
Create/Cancel and Open progress/Cancel frames were inspected for readability.
Physical helpers, assistive technology and human review remain unqualified.

Machine-readable checks are in `NEW-OPEN-WORKERS-EVIDENCE.json`. The checkpoint
report verifies the exact pushed Ubuntu 24.04/Qt 6.4.2 native and lint jobs.

## Next and remaining work

Capture source-history cancellation still waits for worker completion before
restoring controls. Next retire cancelled jobs immediately, fence coordinator job
identity and restore the frozen adjacent history target through the guarded Core
API before source/display publication. Preserve capture source-region proof,
exact fields/assets/checkpoints and actual output/retry.

Remaining native configuration/lifetime paths, all tools/effects/capture topology,
recording/audio/uploads/workflows/settings/automation/compatibility/packaging/
migration/updates/rollback, supported targets, resources/performance and human
privacy/security/a11y/parity/release gates remain Required. Recorded product-owner
clarifications are still pending; this checkpoint accepts no human gate.
