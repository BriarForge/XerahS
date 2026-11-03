# Native annotation tool modes and atomic geometry

This is partial candidate evidence for EA-002/003/004/014/017/018/020 and
ES-003/004/005/006/026/027. Every annotation requirement remains Required;
this checkpoint does not accept the full twenty-tool surface, shared goldens,
parity or human accessibility/privacy/security/release gates.

## Native behavior

The editor's Image → Annotation Tools menu exposes checked Select, Rectangle
and Crop actions. The same actions participate in toolbar customization and
its text/modal/busy shortcut precedence. Select and Rectangle add two hidden
commands (`select-tool`, `rectangle-tool`): 45 current commands, with the
existing fourteen visible defaults unchanged. Tool selection is ephemeral;
it does not write preferences, change pixels, dirty the document or create
history. New/Open resets to the existing Rectangle default.

Select clicks, Control-clicks and drags existing visible rectangles; blank
canvas clears selection without creating an object. Rectangle retains the
existing click-to-select behavior and creates an object on a blank-canvas
drag. Rectangle and Crop stay active after completion. Checked native controls,
compact visible pointer/cancellation guidance, full accessible descriptions
and tooltips communicate the mode. Rectangle explicitly allows out-of-canvas
bounds; the shared export renderer clips to the source. Existing border/fill/
thickness controls and Drawing Defaults remain available. Complete shape
options and assistive-technology qualification remain Required.

Changing tools cancels the live draw/move/pan preview. Escape cancels it;
document-edit keys are blocked while a gesture is live. Output busy state
disables all tool actions and the canvas, including programmatic checked-state
changes. Capture annotation uses the same Select/Rectangle controls, guidance
and Core model before its accepted snapshot enters the output pipeline.

Core rectangle creation checks both supplied points before normalization,
rejecting NaN, infinity, zero area and overflowing spans. Direct move/resize/
style edits prepare a private batch, reject invalid resulting bounds/style
before any commit and expose stable diagnostics. A change rounded away by
binary64 arithmetic records no mutation and preserves Redo/checkpoints.
Rotation reduces finite angles before addition, preventing an intermediate
overflow. Native geometry/protection failures show value-free messages and
keep the document. This is not complete imported-field/resource qualification.

Ordinary creation/edit/delete/order, transfers and canvas edits now share one
allocation-before-commit history boundary. Undo restores the actual selection
immediately before the command, including an intervening selection-only change;
Redo restores its resulting selection and stable object IDs. Failed geometry
does not truncate Redo or acknowledge a saved checkpoint. Existing exact
rectangle rasterization and unknown-type persistence remain unchanged.

## Verification

- `EditorSessionTest`: invalid coordinates in either point, overflowing spans,
  zero area, complete multi-object overflow refusal, rounded no-ops, finite
  rotation and exact pre-command selection/Redo/checkpoint recovery.
- `EditorCanvasTest`: real tool buttons and accessible checked state, Select
  blank-canvas refusal, reverse creation, persistent modes, pointer selection/
  movement, tool-change cancellation, blocked live-gesture keys, disabled tools
  and native geometry diagnostics.
- `EditorWindowTest`: actual tool menu/toolbar/customized keys, text precedence,
  busy guards, unchanged private preferences/source, Save and editable reopen.
  Small-screen placement assertions remain intact after compacting guidance.
- `CaptureAnnotationTest`: actual Select/Rectangle controls, cancelled creation,
  reverse drag, accepted composition and real temporary PNG publication.
- `ToolbarPreferencesTest` and `NativeEditorToolbarTest`: recognized new commands,
  unchanged default placement, customization/validation and dispatch precedence.
- All existing published vectors remain authoritative; no EA vector file exists.
  All 43 local CTest targets pass (34.40 seconds), including 114 published
  vectors. All eight affected targets pass against cached Qt 6.4.2 (10.30
  seconds). All 32 tooling/runner unit tests and contract/backlog/effective
  instruction checks pass. The rendered offscreen editor frame was inspected;
  checked Rectangle state and compact instructions are visible. The pushed
  revision receives separate Ubuntu 24.04/Qt 6.4.2 CI verification.

Fixtures use temporary files/settings and private buses; no real capture,
printer or upload is performed. An optional `XERAHS_TOOL_QA_IMAGE` on the single
editor fixture saves its offscreen native frame for visual inspection.

## Product-owner clarification EA-005-LINUX-2026-10-14

Status: pending. Owner: human product owner plus shared annotation/conformance
owners under ROOT-ESCALATE-001. EA-005/019/020, ES-015 and the ambiguity rule in
`product-contract/READINESS.md` require this decision before Ellipse becomes a
recognized persisted/rendered type. No approved disposition removes Ellipse.

The approved EA intent requires Ellipse bounds, stroke, fill, border style,
thickness, shadow, opacity, rotation, visibility and order. Its selected tool
metadata remains draft; the shared `.xann` v1 schema defines only Rectangle's
shape-specific fields. Other `type` values have only the generic id/type
boundary. ES-025 decides Rectangle pixel coverage, not Ellipse coverage.

Concrete compatibility counterexample: the current schema permits an inert
`type: "ellipse"` with `center`, `radiusX`, `radiusY` and opaque compatible
fields, as well as an ellipse with normalized `start`/`end`. Treating every
ellipse as a Rectangle-shaped payload would reject or reinterpret previously
round-trippable unknown data. A chosen native layout cannot decide that policy.

Concrete rendering counterexample: an ellipse with radii 10 and 2 and stroke
width 2 has different boundaries under a true constant-width normal offset and
under concentric ellipses with radii increased/decreased by 1. At parameter
angle 45°, the normal's unit components are approximately (0.196, 0.981);
concentric growth instead displaces (0.707, 0.707). The fill/stroke coverage and
quantized pixels differ, especially for rotation, small radii and thick strokes.
Choosing native QPainter or a polygon approximation is not shared approval.

Required decision: publish the recognized Ellipse document fields/version and
unknown-compatible-field handling; define stroke/border/shadow/rotation/opacity
and exact pixel coverage or approved tolerance; add deciding schema, scenarios,
vectors and visual fixtures through the owners. Existing Ellipse data remains
an inert preserved placeholder. Native Select/Rectangle work proceeds
independently without inventing Ellipse serialization or golden pixels.

Next independent work: native Rectangle pointer resize handles and complete
selected-object controls under EA-003/005/018; implement Ellipse from the shared
decision when supplied. All remaining tools, assets, effects, outputs and the
full application/capture/recording/upload/workflow/packaging matrix stay in scope.

## EA-005 border style, corner radius and shadow (Rectangle and Ellipse)

Persisted per annotation using the reference XerahS `.xann` format (camelCase
properties and enum values) so existing files interoperate: `borderStyle`
(`solid`, `dash`, `dot`, `dashDot`, `dashDotDot`; read case-insensitively),
`cornerRadius` (integer, Rectangle only; an Ellipse keeps any such field as an
unknown field), `shadowEnabled`, `shadowColor`, `shadowBlurRadius`,
`shadowOpacity`, `shadowOffsetX`, `shadowOffsetY`. Defaults match the
reference: solid, 0, shadow off, `#80000000`, blur 4, opacity 1, offset 3/3.
Absent means default and the writer omits a group that is at its default.
Ranges (corner 0..10000, blur 0..1000, offsets within 10000, opacity 0..1) are
this build's bounded validation. Shadow semantics (decided under the
similar-apps rule): the shadow comes from the shape's painted alpha, its offset
stays in document axes under rotation, and hit testing ignores it. A Rectangle
with any of these fields renders through the shared painter; an undecorated one
keeps the exact ES-025 renderer. Edit > Rectangle Properties edits a selected
shape (one undo step); Edit > Shape Style Defaults sets session creation
defaults without touching existing objects. Ellipse stroke-geometry golden
parity remains pending clarification EA-005-LINUX-2026-10-14.

## Line and Arrow style (EA-006)

- Fields: `borderStyle` and the `shadow*` set exactly as for Rectangle and Ellipse; Arrow adds `style` (classic, modern, double, basic, line; omitted when classic). Omitted at default; older documents load at the defaults.
- Curve: optional third entry of `points` (fraction of the bounds) that the quadratic curve passes through at its middle. The reference `curvePoint`/`curvePointActivated` (document pixels) are read and converted. Modern arrows are straight.
- Edit in Edit > Properties (border, arrow style, curve, shadow); one undo step. Shape Style Defaults also sets the arrow style for new arrows.

## Active tool, options and keep-active (EA-002)

Choosing any of the 20 tools checks its action and unchecks the others.
`EditorCanvas::toolInstructions()` (canvas and instruction-label accessible
description, label tooltip) states, per tool: the pointer gesture, an
`Options:` sentence from `EditorCanvas::toolOptions` listing the options that
apply to that tool (the same sentence is appended to the action's status tip and
tooltip), keyboard guidance (Escape cancels a live gesture, arrow/R/Delete/
ordering keys) and, for every creation tool, "stays active". Tools stay active
after a completed object: the reference XerahS option
`SwitchToSelectionToolAfterDrawing` defaults to false, and no Linux preference
turns it on, so a return-to-Select behaviour is not offered and not claimed.
`AnnotationToolMatrixTest::everyToolShowsStateOptionsInstructionsAndKeepsItselfActive`
checks all of this for every contracted tool and draws two objects in a row to
prove the tool is still checked and active (Crop and CutOut request two
operations; Select creates nothing).
