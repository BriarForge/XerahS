# Native keyboard Rectangle creation

Partial candidate evidence for EA-002/004/014/017/018/019/020 and
ES-003/005/006/024/026/027. All EA requirements remain Required. This adds
keyboard creation of the supported Rectangle fields; the full twenty-tool
surface, border/corner/shadow semantics, shared visual goldens, resource and
native target qualification and human reviews remain unfinished.

## Native behavior

Edit → Create Rectangle… opens four accessible source-pixel fields: Left, Top,
Width and Height. Capture annotation exposes the same command as a focusable
native toolbar button reachable by Tab and Space. The dialog supports Tab
navigation, explicit Create (Alt+R), and Cancel/Escape. Cancel is the default
button; the Create mnemonic is distinct from the native Cancel mnemonic.
Initial bounds start at the source origin and use up to 100 pixels in each
dimension, constrained to the image size. This is a native draft, not a saved
creation default. Positive finite bounds may extend outside the image; the
intro explains that export clips them.

The dialog owns a copy of creation style, document state and selection from
when the command began. It displays the stroke/fill/width/rotation/opacity
snapshot as plain read-only text. Users can cancel and change Drawing Defaults
before reopening. Later defaults changes do not alter this object or overwrite
the later defaults. The active tool stays unchanged, including Select. The
`create-rectangle` internal native command adds one hidden customizable command
(47 total); the fourteen visible defaults and protected shortcuts stay intact.
Text entry, modal input, active gestures and output operations retain precedence.

Create inserts one fresh-ID rectangle at the highest z-order and selects it as
one undo operation through the Qt Core session. Undo restores the exact earlier
document and selection, Redo keeps the new ID, and the existing checkpoint rules
apply. Invalid values keep the dialog open without model changes. Cancel,
parent destruction, stale document/selection or failed preparation commits no
part of this operation. Capture Apply cannot proceed while the creation dialog
is pending, and command availability restores immediately after its close.
Main-editor source/output/close commands use the existing guarded busy scope.
Pending-state observers may destroy the owner safely before the dialog opens.

Pointer Rectangle creation now freezes style, state and selection at drag start
and calls the same Core draft insertion path on release. It normalizes reverse
drags and refuses stale, non-finite, collapsed or overflowing geometry without
truncating Redo or replacing an intervening edit. Escape or changing tools
cancels the draft. Existing output rendering and published vectors are unchanged.

## Evidence and limits

- `EditorSessionTest` covers frozen style after defaults change, reverse bounds,
  source/assets/order, fresh identity, one history entry, earlier selection and
  exact Undo/Redo/checkpoint recovery, plus invalid/stale refusal preserving Redo.
- `EditorCanvasTest` uses real keyboard field entry, Tab focus, Alt+R and Escape
  for accepted/default/invalid/cancelled/stale/owner-destroyed drafts. It checks
  style/default isolation, active Select, exact earlier selection and pending
  lifetime guards. Actual reverse pointer creation checks style freezing and
  preservation of an intervening document edit.
- `EditorWindowTest` dispatches customized F9, verifies busy/source/close guards,
  cancellation and destruction without writes, then saves/reopens actual
  temporary PNG/.xann data with original pixels and stable bounds/style/ID.
- `CaptureAnnotationTest` reaches the native button by Tab/Space, checks its
  accessible name, blocks pending Apply, and uses actual temporary PNG
  publication to distinguish accepted green fill from cancelled original blue
  pixels. Capture defaults and the embedded unannotated source remain intact.
- Toolbar preference/native registry tests cover recognized F9 customization,
  unchanged placement and text/modal/busy/no-repeat input rules.

`XERAHS_RECTANGLE_CREATION_QA` exports a native offscreen dialog frame from the
accepted keyboard fixture; its layout and labels were inspected. Fixtures use
private preference stores, private buses and temporary files. No real capture,
printer or upload is used. Verification results are indexed in the candidate map
and checkpoint report. The final local suite passed all 43 CTest targets in
36.49 seconds and all 114 unchanged published vectors. The 32 tooling tests
passed (16 linter, 9 backlog, 7 runner); all seven affected targets passed
against Qt 6.4.2 in 13.02 seconds. These fixtures do not accept physical keyboard/assistive
technology, performance, privacy/security, native matrix, parity or release gates.

Ellipse schema/rasterization remains pending EA-005-LINUX-2026-10-14;
asset clipboard and export formats retain their recorded shared owner decisions.
No recognized Ellipse fields, new tool IDs, shared expected pixels or accepted
human dispositions are invented.

Subsequent [object selection](ANNOTATION-OBJECTS.md), [movement](ANNOTATION-MOVE.md),
[rotation](ANNOTATION-ROTATION.md), [colour](ANNOTATION-COLOR.md) and
[numeric draft](RECTANGLE-DRAFTS.md) checkpoints add shared native controls,
generation and owner guards. [Thickness](ANNOTATION-THICKNESS.md) now guards
document/default admission and publication lifetime. Next expose guarded native
ordering controls and catalogue gestures. All remaining tools/effects, capture topology,
recording/audio, uploads, workflows, application/compatibility/packaging,
supported Linux editions and human/release gates stay in full contract scope.
