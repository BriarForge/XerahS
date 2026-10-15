# Owned comparison loading and previews

This is partial Linux native candidate evidence for EC-016/017 and editor-session
isolation under ES-002/005/006/007/008/019. Full capability, target matrix and human
acceptance remain Required. Shared geometry, raster policy, schemas, vectors,
dispositions and approvals are unchanged.

## Production behavior

Compare Images loads validated raster images or renders the current editor
snapshot on owned workers. Each job owns its source, annotations, selected path,
input index, label, cancellation/progress state and private result. It captures
no window, dialog, session or UI pointer. Result publication follows the worker's
release/acquire completion boundary; exceptions become a fixed redacted failure.
Private sRGB conversion, native pixel sizes, top-left/center alignment, transparent
padding, divider columns and unsupported-annotation refusal keep existing policy.
Raster comparison still ignores editing sidecars and never writes source files.

Cancel Loading retires the current job and immediately restores the controls.
The prior validated pair, alignment and divider remain usable; another image can
load while cancelled work exits. A late success or exception has no current job
identity and cannot publish. Close, Escape, programmatic completion, direct dialog
destruction and forced owner teardown cancel without joining decoding/rendering.
A worker can continue until a decoder returns or checks cancellation. This does
not establish bounded decoder latency or application-process shutdown behavior.

The GUI validates a candidate pair's union before replacing either input. Rejected
or cancelled work keeps the entire previous pair and layout. It retires the job,
publishes inputs/geometry/status, then restores enabled controls; reentrant loads
from progress and enable notifications are refused. Weak owner checks follow native
notifications. Labels render file names and diagnostics as plain text.

The file picker is owned and guarded across its nested event loop. Cancellation,
child destruction, comparison closure, parent destruction and a destruction
observer deleting the parent during cleanup all return safely. Cleanup hides and
detaches a surviving picker before deleting it. The production picker continues
to honor desktop theme helpers.

The main editor freezes its document/source before busy notifications and opens
an owned comparison dialog under the common output/mutation guard. Save, Copy,
Print, history, nested comparison, Open and ordinary editor Close cannot start
during the comparison. The weak restore path survives forced owner destruction.
Comparison never commits a document or history state. On return, source, fields,
selection, Undo/Redo, dirty checkpoint, viewport and files remain unchanged.

## Evidence

`ImageComparisonDialogTest` adds 22 native cases:

- Four blocked replacement rows cover both inputs and late success/exception;
  cancellation returns before worker release, a newer load succeeds, and late
  completion preserves its exact labels, pixels, status, alignment and divider.
- Five rows hold current rendering across Reject, Accept, Escape, dialog deletion
  and owner deletion. Accessible progress is named, completion is singular and
  teardown returns within 500 ms while the worker remains blocked.
- Two rows mutate the caller's source/annotations while rendering is blocked,
  then verify retained rendering, private linear-to-sRGB interpretation, fixed
  exception text and successful retry.
- One native union-limit case validates each thin image but refuses their huge
  combined canvas, preserving the prior pair, geometry and controls.
- Two rows cancel from ready progress or attempt new loads while controls become
  enabled; there is no partial admission or nested publication.
- Eight rows exercise accepted first/second file selection, cancellation, picker/
  dialog/owner destruction, cleanup-time owner deletion and closing the comparison
  while its picker is open. Temporary raster files stay untouched.

`EditorWindowTest` adds four blocked worker/owner/child/cleanup rows and strengthens
its existing comparison test with output/history/reentry refusal. It checks exact
serialized document, state, selection, dirty title, status, zoom/pan, file bytes
and post-return controls. Existing real divider keys/pointer input, accessible
slider values, independent expected native pixels/padding/alignment, invalid file
and sidecar isolation remain tested. Existing main-editor Undo/Redo and actual
PNG/.xann save/reopen assertions remain in place.

The full local run passed 44 targets in 65.53 s, including comparison dialog
3.19 s and editor window 26.62 s. Six affected Qt 6.4.2 targets passed in 33.42 s,
including comparison dialog 3.08 s and editor window 25.38 s. Final review found
that the divider could become available before geometry/status publication.
Enabling now follows complete publication, and its native enable observer asserts
layout/geometry/status plus nested-load refusal. Both complete affected targets
pass on the final source: 29.58 s locally (dialog 3.18 s, editor 26.40 s) and
28.44 s on Qt 6.4.2 (dialog 3.06 s, editor 25.38 s). The 42 unchanged local/four
unchanged baseline targets were retained. All 114 unchanged published vectors
and 32 tooling tests pass. No timeout was increased. The
synthetic 800 × 650 ready comparison frame was inspected for readable controls,
focus/divider, native-size policy, transparent padding and status. Machine-readable
verification is in `COMPARISON-WORKERS-EVIDENCE.json`; the checkpoint report
verifies the exact pushed Ubuntu 24.04/Qt 6.4.2 native and lint jobs.

An initial offscreen fixture timed out in the external GTK theme helper rather
than exposing a Qt picker to the timer. The fixture now explicitly selects owned
Qt picker widgets and private QSettings. This does not qualify physical GTK,
portal or other desktop helpers. A new union assertion initially used an absent
ImageExtent equality operator; independent width/height assertions corrected the
fixture. Final complete local and affected minimum-Qt runs pass.

## Remaining work

Next migrate New Image and Open preparation/configuration to owned workers and
nonblocking child/owner/cancel recovery. Preserve dirty replacement consent,
explicit save/reload, source/profile/alpha and exact old-session history.
Capture-history coordination also retains an earlier asynchronous lifetime path.
Full tools/fields/resources/performance, desktop helpers, supported native target
matrix, accessibility, privacy/security, compatibility, shared goldens, parity,
packaging and human/release acceptance remain Required. Recorded product-owner
clarifications remain unresolved; this checkpoint accepts none of those gates.
