# Native editor Copy Image

Candidate evidence for related EO-001/002/014/015/016 behavior and
ES-009/019/027, ESET-010/016 and CORE-004/007/027. Full output conformance,
physical clipboard interoperability and native/human acceptance remain Required.
This follows the verified editor-save checkpoint without changing its raster,
companion, path, checkpoint or recovery policies.

## User-visible behavior

**Edit → Copy Image**, Ctrl+C, copies the source pixels and visible supported
annotations from one owned document snapshot. Its tooltip and named progress
dialog declare sRGB, alpha and PNG output. Separate background composition
remains unimplemented. Visible unsupported objects fail before publication
instead of being silently omitted. The source, document, selection, viewport,
dirty checkpoint, Undo/Redo, recent paths and image/companion files stay intact.
Copy neither saves nor closes the editor.

Rendering and PNG preparation run on an owned worker. The GUI remains responsive
with a real Cancel control; document/output actions are disabled and reentrant
Copy, Save, replacement and close are refused while the output is pending.
The common `EditorWindow::withOutputBusy` guard restores only a surviving owner
and serves both Save and Copy, keeping their admission rules aligned.
Cancellation or owner destruction before publication leaves the clipboard
untouched; owner destruction releases the wait without joining or giving the
worker GUI references. Late cancellation retains the actual publication outcome.

The native provider offers both QImage and `image/png`, containing only the
rendered pixels, never the editable document or original embedded source.
Preparation uses the existing native sRGB conversion and strict Core rectangle
compositor shared with image comparison. Alpha is retained in both offers.
Untagged interpretation and non-sRGB conversion are disclosed in completion
text. Original pixels/profile remain unchanged. Format/profile acceptance,
wide-gamut tolerances and paste-target behavior still need shared and physical
qualification; advertised alpha cannot guarantee every receiving application
will preserve it.

**Edit → Retry image copy…** is available only for a known failed publication
with a prepared snapshot and the same history state. Its confirmation defaults
to Cancel and explains that retry replaces current clipboard contents while
rendering no new pixels. Changing the document disables that retry; Undo restores
eligibility without truncating Redo. Stale confirmations cannot publish another
receipt. A new Copy is a new explicit action. Unknown delivery, callback exception
or inconsistent callback facts never authorize this repair.

Copy is the 38th current native toolbar command (`copy-rendered-image`), hidden
in the unchanged default toolbar and always reachable through its fixed menu
and protected shortcut. Customization can show/reorder/group it or add a hotkey.
Text entry, modal/popup state, disabled actions and active operations retain
precedence. Copy's built-in and additional shortcuts do not auto-repeat. A missing
publication callback disables Copy and explains why.

## Publication and callback contract

The native provider runs only on the GUI thread. It creates MIME data with owned
image/PNG values and transfers it to Qt. Qt documents the MIME ownership transfer
and the clipboard ownership query in [QClipboard](https://doc.qt.io/qt-6/qclipboard.html#setMimeData).
Production acknowledgement additionally requires the unchanged live offered
MIME object, so synchronous replacement cannot manufacture success. The provider
does not restore or clear foreign contents. Lack of acknowledgement is a typed
unknown outcome, even if offered pixels are readable locally.

`EditorCopyPorts` declares worker-only rendering/encoding hooks with borrowed
arguments and captures that must outlive the worker. Its GUI-only publisher
borrows a ready payload for the call and must retain owned copies if needed.
The GUI callback is frozen for the attempt and is never captured by the worker.
Known Failed/Cancelled responses mean no effect; unknown responses mean delivery
cannot be established. A publication exception may occur after an effect and is
therefore unknown. Invalid state/selection/artifact responses also become unknown;
untrusted diagnostic values are mapped to stable value-free native codes.
An empty provider is unavailable, with no implicit fallback.

`copyFinished` reports GUI acknowledgement while outputs remain busy, with a
distinct unknown state and late-cancellation flag. Reentrant output/document
actions are refused. Deletion during publication or completion cannot cause
late window access; already published MIME values retain their own lifetime.
The native fixture API accepts an owned prepared payload, while the editor is
the admission boundary for confirmed retry. No restart metadata can reconstruct
that authority, and no journal, file, clipboard polling or automatic retry is
introduced by Copy.

## Verification and remaining work

- `EditorCopyTest`: worker/GUI thread separation, caller mutation and retained
  snapshot lifetime, actual native QImage/PNG offers with alpha, ownership-based
  acknowledgement, synchronous foreign replacement, real Cancel/owner teardown,
  encoder failure, unavailable provider, visible unsupported-object refusal,
  known-failure replay without rendering/encoding, callback exception after an
  effect, late cancellation and independent linear-to-sRGB conversion/source
  preservation. The offscreen backend may not claim clipboard ownership; its
  readable offer is then correctly unknown, not proof of physical delivery.
- `EditorWindowTest`: actual menu/Ctrl+C and local text-copy precedence;
  unchanged dirty/history/selection/viewport/preferences/files, responsive busy
  controls and cancellation, known-failure/default-Cancel replay with intact
  Redo, stale-state eligibility, unknown delivery without retry, missing-service
  explanation and owner deletion during publication/completion. Existing Save
  failure, repair, close and lifetime fixtures verify the shared busy guard.
- `ToolbarPreferencesTest` and `NativeEditorToolbarTest`: protected Ctrl+C
  ownership, rejected annotation-copy collision, preserved default layout and
  native customization/input/disabled/restart behavior.

The checkpoint passed all 41 local CTest targets, all 114 existing published
vectors, contract lint/backlog and 32 tooling/runner unit tests. Affected native
targets also pass cached Qt 6.4.2. Exact pushed-SHA Ubuntu 24.04/Qt 6.4.2 CI is
the authoritative baseline result. EO still has no published vector package;
native fixtures are not shared conformance acceptance.

Physical GNOME/Plasma/wlroots Wayland and X11 paste targets, input serial/focus
and ownership behavior, clipboard-manager/app-exit persistence, complete resource
and shutdown policy, color/alpha goldens, accessibility and human privacy/security
review remain Required. The existing host contract and fixture evidence do not
accept those gates or imply another platform's disposition.

EO-004-LINUX-2026-10-14 in EDITOR-SAVE.md remains pending; this independent output
work does not decide the draft format defaults. The subsequent
[Print checkpoint](EDITOR-PRINT.md) now adds the non-mutating native print flow.
Continue owned output service handoffs, PNG/JPEG/WebP policy and all domains
in CONTRACT-AUDIT.md.
