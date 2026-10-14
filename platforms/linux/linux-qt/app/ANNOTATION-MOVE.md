# Frozen native annotation movement

This is partial candidate evidence for EA-003/017/018/019/020 and the existing
ES-004/005/006/008/019/026/027 session seams. Every applicable requirement
remains Required. It does not activate the complete annotation capability,
change the shared document schema or expected vectors, or approve parity,
privacy, accessibility or release gates.

## Native behavior

Dragging a selected rectangle keeps the current group selected. Control-click
retains the existing additive/toggle behavior. The shared main-editor and capture
canvas copy the selected rectangle bounds/styles, exact selected UUID set,
history state, document generation and source-pixel pointer origin at press.
No borrowed document pointer survives a mutation.

Move previews use total source-pixel displacement from that origin, never
incremental additions to a prior preview. The Core `moveRectangleGeometry`
helper preserves style and rejects non-finite, collapsed or overflowing derived
export geometry. A complete group preview is prepared before it is displayed;
it changes neither the document nor history, dirty checkpoint, source, assets,
creation defaults or files. Painting uses the resulting bounds and rotation
centres directly, including at non-unit zoom.

The displayed history state, exact selection and document generation must still
match the draft. A stale or invalid draft displays committed geometry with a
contrasting white/black cross at the pointer, rather than an apparently accepted
translation. Existing dashed selection outlines and keyboard movement remain
available. Object selection and editing/output actions are unavailable during
the live gesture; capture Apply cannot publish it before release.
Capture Undo/Redo availability follows the gesture and history, and Redo accepts
both Ctrl+Y and Ctrl+Shift+Z consistently with the main editor on Qt 6.4 onward.

Release sends one guarded Core move against the original history state and
selection. Every selected represented rectangle validates before the complete
document is committed once. Stale releases report `annotation-edit-stale` and
preserve intervening edits/selections. Hidden, locked or opaque-lock selections
remain protected; invalid geometry reports `annotation-geometry-invalid`.
No-motion, return-to-origin and binary64-rounded no-ops preserve Redo and the
checkpoint. A successful move retains IDs, order, styles, source/assets and
compatible unknown fields; Undo/Redo restores the exact document and selection.

Unknown compatible annotation types remain inert and round-trippable. As in the
existing keyboard movement path, an unlocked unknown object in a mixed selection
stays unchanged while represented rectangles move. Its raw payload is never
interpreted as geometry. Complete multi-tool transformations still require their
governed schemas and vectors; this checkpoint does not invent those fields.

Escape, focus loss, tool changes, zoom, wheel/pan, source changes or document
replacement cancel the draft. Release after cancellation changes nothing.
Copied drafts disappear with the canvas. A rejection observer may destroy the
canvas without a subsequent use or second notification.

## Evidence

- `EditorSessionTest` exercises independent fractional group translation,
  unchanged source/assets/defaults/unknown data, exact history and selection,
  stale state/selection, protected complete groups, non-finite and whole-batch
  overflow, derived rotated overflow, rounded no-ops and preserved Redo/checkpoint.
- `EditorCanvasTest` exercises actual pointer preview/release at three zooms
  and rotations 0/90/37, independently calculated screen-to-source displacement,
  unchanged preview documents, pixels at both translated centres, one history
  commit, blocked live edits, cancellation, replacement, stale previews/releases
  and reentrant owner destruction. `XERAHS_ANNOTATION_MOVE_QA` exports a native
  fixture frame for visual inspection.
- `EditorWindowTest` selects two objects through the native keyboard list,
  drags the group, observes disabled output/selection during preview, then
  checks Undo/Redo and actual PNG/.xann save/reopen. Source pixels, IDs, alpha,
  rotation, compatible unknown fields/assets and preferences are preserved.
- `CaptureAnnotationTest` selects the group with native Ctrl+A, rejects Apply
  during preview, moves both objects in one undo step, and verifies actual
  temporary PNG publication and preserved original embedded capture pixels.

The final local suite passed all 44 CTest targets in 36.62 seconds and all 114
unchanged published vectors. The 32 tooling tests passed (16 linter, 9 backlog,
7 runner); six affected targets passed against Qt 6.4.2 in 13.28 seconds. The
native group preview was inspected. Verification is indexed in
`ANNOTATION-MOVE-EVIDENCE.json`. Native
fixtures use temporary media/preferences and private D-Bus sessions; they do not
capture the real desktop, upload content or submit print jobs. Full resource/
performance, supported desktop, physical assistive-technology/high-contrast,
privacy/security, shared goldens, parity and human acceptance remain Required.

## Remaining work

The subsequent [rotation checkpoint](ANNOTATION-ROTATION.md) guards the native
multi-object rotation dialog against intervening document/selection changes and
owner destruction. The subsequent [colour checkpoint](ANNOTATION-COLOR.md) guards
the stroke/fill pickers' selection/defaults/generation/preferences and lifetime.
[Numeric drafts](RECTANGLE-DRAFTS.md) now guard replacement documents with matching
state and IDs and nested colour lifetime. Next guard native thickness commands
against default-only publication after a refused selected batch. Keep the pending shared
Ellipse, clipboard/assets and output-format decisions in ANNOTATION-TOOLS.md,
ANNOTATION-TRANSFER.md and EDITOR-SAVE.md; continue all other contracted tools,
effects, workflows, captures, recording/audio, uploads, compatibility and release
qualification. Full Product Contract completion is not established here.
