# Native rectangle resize and properties

Partial candidate evidence for EA-003/005/014/017/018/019/020 and
ES-004/005/006/024/026/027. All EA requirements remain Required. The full
twenty-tool surface, border/corner/shadow semantics, shared goldens, resource
qualification, native target matrix and human reviews are unfinished.

## Pointer resize

One selected visible, unlocked Rectangle shows eight black-bordered white
square handles outside its selection outline. They are eight logical screen
pixels wide, with twelve-pixel hit targets and rotation-aware resize cursors.
They retain their screen size at different zoom levels. Multiple selections,
hidden/locked rectangles, unsupported types and Crop mode do not offer handles.

Dragging snapshots the object, selected ID and history state. The Qt Core
`RectangleGeometry` implementation converts source-pixel pointer displacement
to local rectangle axes. Corners resize both dimensions; edge handles constrain
to one local axis. The opposite displayed handle remains fixed when the bounds
centre changes, including arbitrary rotation. A copied preview changes neither
the document nor source pixels. Release calls the same geometry path and
records one session mutation. UUID, style, unknown compatible fields, assets,
order and creation defaults remain intact.

Crossing/collapsing the opposite handle, non-finite/overflowing bounds or derived
export geometry is rejected. An invalid preview displays a contrasting X and
release reports a stable, value-free diagnostic. Escape, switching tools,
viewport navigation or focus loss cancels the preview. Live edit keys remain
blocked. A changed document or selection rejects the frozen resize and preserves
the intervening edit, rather than rolling it back. No-motion resize creates no
history and does not truncate Redo or acknowledge a checkpoint.

## Selected-object properties

Edit → Rectangle Properties exposes source-pixel left/top/width/height, stroke
width, clockwise rotation, opacity and stroke/fill #AARRGGBB controls. Native
color pickers retain alpha. Fields have accessible names, keyboard tab order
and textual validation results. The dialog explains out-of-canvas geometry and
export clipping, and offers explicit OK/Cancel. Invalid values keep the dialog
open without mutating the document. Unchanged imported coordinates retain their
exact binary64 endpoints; subtracting/re-adding an unchanged extent does not
silently change a stored endpoint.

One accepted properties edit changes only the selected rectangle as one undo
operation. Creation defaults, source pixels/profile, assets, order and compatible
unknown fields remain intact. Cancel, unchanged OK and parent destruction make
no mutation. Core commits require the frozen history state and exact single
selection, and refuse hidden/locked/unsupported objects. The main editor uses
the existing guarded busy/lifetime scope during the modal edit; output/source/
tool commands remain disabled. Capture annotation shares the same dialog and
Core API, prevents Apply while it is pending, then composes accepted pixels
before the remaining output pipeline. Cancel restores capture property-command
availability immediately. Initial canvas fitting uses the actual first layout;
later resizes preserve the user's chosen zoom.

`rectangle-properties` adds one hidden customizable native command: 46 current
commands, with the existing fourteen visible defaults unchanged. Additional
keys use the established text/modal/busy/no-repeat rules. This is an internal
native command ID, not an invented shared annotation tool or interchange format.

## Evidence and limits

- `EditorSessionTest` supplies independent bounds for all eight handles,
  a hand-computed quarter-turn example, exact UUID/field/asset/history/checkpoint
  recovery, creation-default isolation and stale/protected/collapsed/invalid/
  derived-overflow/no-op refusal.
- `EditorCanvasTest` exercises all eight physical handle positions at 0°, 90°
  and 37° with two zoom levels (48 cases), fixed opposite-point invariants,
  copied preview, one commit, cancellation, stale intervention and protected
  objects. Real dialogs cover unchanged high-precision endpoints, native alpha
  picker, invalid fields, Cancel, OK and owner destruction. QSettings stays
  inside a private temporary store.
- `EditorWindowTest` exercises an actual customized F8/menu action, guarded
  modal edit, Undo/Redo, real PNG/.xann save/reopen with unchanged source/assets/
  unknown fields, subsequent creation defaults and owner destruction without
  file writes. Existing small-screen placement assertions remain intact.
- `CaptureAnnotationTest` exercises native properties before actual temporary
  PNG publication and verifies that a later rectangle retains its creation
  defaults. Its fixture uses the native Fit command so the intended blank point
  lies outside the fixed screen-sized handle targets.
- `ToolbarPreferencesTest` and `NativeEditorToolbarTest` cover the new recognized
  action, unchanged visible defaults and input precedence. Existing published
  expected vectors and the export renderer remain unchanged.

Optional single-fixture `XERAHS_RECTANGLE_HANDLES_QA` and
`XERAHS_RECTANGLE_PROPERTIES_QA` paths export native offscreen frames for visual
inspection. No real capture, printer or upload is used. Test and minimum-Qt/CI
results are indexed in the candidate map and checkpoint report. The final local
suite passed all 43 CTest targets in 34.64 seconds and all 114 published vectors.
The 32 tooling/runner tests passed (16 linter, 9 backlog, 7 runner); all eight
affected targets passed against Qt 6.4.2 in 11.49 seconds. Native handle and
properties-dialog frames were inspected. Offscreen tests
do not accept physical assistive-technology, performance, privacy/security,
parity or release gates.

Full Rectangle border styles, corner radii and shadows still need the governed
schema/rasterization and visual evidence. Ellipse remains pending shared
clarification EA-005-LINUX-2026-10-14 in ANNOTATION-TOOLS.md; transfer assets and
export formats remain pending their recorded owner decisions. No approval or
degraded disposition is inferred from these native controls.

Next independent work: keyboard-accessible Rectangle creation with numeric
bounds and a frozen creation style, sharing the editor/capture session history.
All remaining tools/effects, recording/audio, uploads, workflows, application,
compatibility, packaging, supported Linux editions and human/release gates stay
in the full contract scope.
