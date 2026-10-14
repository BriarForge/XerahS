# Native editor Print Image

Candidate evidence for EO-001/008/014/015/016 and related ES-009/019/027,
ESET-010/016 and CORE-004/007/027. All these output requirements retain a
Required disposition; full output conformance and native/human acceptance are
unfinished. [Machine-readable candidate evidence](EDITOR-PRINT-EVIDENCE.json)
records the implemented slice and the outstanding qualification.

## Plan and production path

This slice continues the completed editor Save and Copy work: freeze the same
owned source/document, prepare print pixels off the GUI thread, expose a Qt
preview and native printer confirmation, submit on a separate owned worker,
and preserve the session through cancellation/failure/explicit retry. Shared
geometry and white-paper composition decisions live in Qt Core `PrintLayout`;
native images, dialogs and printer I/O remain in `app/`. No conformance logic is
introduced into the adapter, and no missing EO vectors are invented.

**File → Print Image…**, Ctrl+Shift+P, includes the source and visible supported
annotations in one immutable snapshot. Separate editor background composition
and remaining annotation tools are still Required. The preview declares that
transparency is composited on white paper and colours use sRGB, including a
notice for untagged interpretation or source-profile conversion. Visible unknown
annotations fail rather than being silently omitted. Original source pixels,
profile, document, selection, dirty checkpoint, Undo/Redo, viewport, recent paths,
preferences, image/sidecar files and clipboard stay intact. Print does not save
or close the document.

The preview exposes page size, uniform margins, orientation, colour/grayscale,
and scale from 1–100% of fit within the printable area. The image is centred,
keeps its aspect ratio and is never cropped by that scale control. Preview uses
Qt's in-memory preview paint engine, with the same `paintEditorPrintPage` as
submission; no temporary PDF is made. The native QPrintDialog then selects the
printer, copies and final device/page options for this one image page. Its Cancel
returns to preview; preview Cancel submits nothing. No native printer yields a
visible unavailable explanation and disabled Print button. A missing host
configuration/submission callback disables the editor action with an explanation.

The native flow disables Print to File: PDF export and guarded file overwrite
policy remain separate unfinished work. PDF printers are used only at the
injected test boundary, with temporary destinations. Printer metadata uses the
value-free title `XerahS image` and creator `XerahS`, not the source filename/path.

Print is the 39th current toolbar command (`print`), hidden in the unchanged
14-command default layout. Its fixed menu/protected shortcut remain reachable
when hidden. Ctrl+P retains its protected pin-to-screen identity. Additional
hotkeys use the same text/modal/busy precedence and no-auto-repeat behavior.
Save, Copy and Print share `withOutputBusy`; document edits, gestures, replacement,
close and reentrant output are refused until the attempt completes. Named progress
and real Cancel controls cover preparation and submission.

## Ownership, cancellation, acknowledgement and retry

`EditorPrintPorts` explicitly separates a GUI-only configuration callback from
worker-only preparation/submission. GUI configuration borrows an immutable
snapshot and parent, submits no job, and returns an exclusively used owned
printer. Submission borrows that printer and snapshot for the worker call; its
captures must outlive a destroyed editor and cannot contain GUI references.
Qt documents [painting onto QPrinter from a worker thread](https://doc.qt.io/qt-6/threads-modules.html#painting-in-threads),
with only one thread using that device at a time. The preview is destroyed before
the selected printer crosses into submission. `printFinished` reports the typed
GUI acknowledgement while outputs remain busy; reentrant calls and owner
deletion during configuration/completion cannot cause late editor access. Throwing
completion observers are contained and visibly reported without relabelling the
acknowledged print result or submitting again. Teardown after a completed worker
acknowledgement retains that actual result even when the owner is destroyed.

Cancel or owner destruction before submission has no print effect. Owner
destruction releases without joining; worker state owns pixels, printer and
callbacks, with no window/session/stack references. During submission the GUI
stays responsive. The worker checks cancellation before starting, before painting
and before completing, calling QPrinter::abort at safe boundaries. A blocking
native begin/draw/end cannot be interrupted from another thread using the same
printer; cancellation is therefore best effort once printing starts.
[QPrinter's cancellation contract](https://doc.qt.io/qt-6/qprinter.html#abort)
does not prove that an already delivered job can be withdrawn.

Successful begin/paint/end without an engine error means **submitted**, never
proof of physical paper delivery. Failure after begin, exception after a possible
effect, malformed callback facts and owner destruction during a job become
`print-delivery-unknown`. Cancellation arriving during a successful acknowledgement
is retained as a separate fact rather than relabelling success. The UI asks the
user to check the queue before another print and grants no retry authority for
uncertain delivery. Host diagnostics are mapped to stable value-free codes.
Known Failed/Cancelled callback outcomes promise no effect; hooks may not claim
those outcomes after submission.

**Retry image print…** reuses a prepared snapshot only after a known failure at
the same history state. Confirmation defaults to Cancel; an approved attempt
opens fresh preview/native printer confirmation and renders no new pixels.
Changing the document disables that retry; Undo can restore eligibility without
truncating Redo. Stale confirmation cannot select another receipt. Rendering
failure without a snapshot instead explains using Print Image again. No automatic
or restart retry, printer-queue mutation, journal or editor persistence is added.

## Verification and limits

- `EditorPrintTest` exercises independent fit/centering/white-paper arithmetic,
  frozen caller changes, worker/GUI boundaries, actual Qt colour/grayscale PDFs
  rasterized by Poppler with independent page/pixel observations, source profile
  conversion without mutation, native preview page/scale/margin/orientation/colour
  and default-Cancel controls, unavailable printers, real preparation/submission
  Cancel, owner destruction without joining, unsupported annotations, missing
  callbacks, configuration exception/invalid scale, known-failure replay, a host
  exception after a temporary recorded effect, malformed acknowledgement and
  native begin failure with truthful uncertain delivery.
- `EditorWindowTest` covers actual menu/Ctrl+Shift+P and real temporary PDF output,
  unchanged dirty/history/Redo/viewport/preferences/source/sidecar/clipboard,
  responsive cancellation and refused reentrant actions, default-Cancel replay
  with fresh confirmation, state eligibility, missing-service explanation,
  unknown outcome without retry, and owner deletion during configuration/completion.
  Existing Save and Copy tests regress the common busy/lifetime guard.
- Toolbar fixtures preserve default placements, protected Ctrl+Shift+P/Print
  ownership, Ctrl+P pin ownership and custom input/disabled/restart behavior.

Fixtures use private buses, temporary settings/files and a nonexistent CUPS
socket inside their private temporary directory. No physical printer or live
print service is contacted. Poppler is a verification dependency, not an app
runtime dependency; Qt PrintSupport is dynamically linked at the existing minimum
Qt 6.4 baseline. The initial native preview fixture exposed QPageSetupDialog's
non-native-printer restriction; explicit preview page/margin controls corrected
that production path without dropping the page/cancellation assertions. A final
teardown fixture reproduced SIGSEGV on local Qt and Qt 6.4.2 when a progress
destruction observer deleted its parent. The coredump/Qt backtrace showed the
parent's recursive child teardown entering the partially destroyed widget/focus
chain. Print now hides and detaches the live progress widget before destruction;
the regression preserves completed acknowledgement through that owner deletion.
No live document or printer was involved in either failure.

The completed checkpoint passes all 42 local CTest targets, all 114 existing
published vectors, contract lint/backlog and 32 tooling/runner unit tests. The six
affected editor/toolbar targets also pass cached Qt 6.4.2. Exact pushed-SHA Ubuntu
24.04/Qt 6.4.2 CI supplies the authoritative baseline result. Native fixtures are
candidate evidence, not shared output conformance or acceptance.

Physical CUPS printer discovery/selection/submission, permission/authentication
rejection, queue loss, device media/copy/grayscale constraints, cancellation,
app exit/full shutdown, disk/resource failures, print colour/goldens/tolerances,
and GNOME/Plasma/wlroots Wayland/X11 across Ubuntu/Fedora/Arch remain Required.
Physical assistive-technology/input, privacy/security, licensing and human release
reviews remain Required. No acceptance or approved Degraded/Unavailable platform
disposition is inferred from a missing printer on a test host.

EO-004-LINUX-2026-10-14 remains pending; white print paper does not choose JPEG
flattening or export defaults. Next extend native annotation duplication/object clipboard under EA-016/017
with owned history and bounded untrusted-payload handling, alongside native output
service handoffs, the pending format policy and full qualification of every
domain/target/human gate retained in CONTRACT-AUDIT.md.
