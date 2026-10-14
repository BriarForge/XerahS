# Guarded native selection rotation

This is partial candidate evidence for EA-003/005/017/018/019/020 and existing
ES session seams. All applicable requirements remain Required. This checkpoint
does not activate the full annotation capability, define new persisted fields or
expected vectors, approve parity, or replace human qualification.

## Native behavior

The main editor's existing Rotate Selection command and a shared capture
annotation control open one native dialog over an owned copy of the opening
document and selected UUIDs. The dialog accepts a finite clockwise degree delta;
negative values turn counter-clockwise. Its read-only text preview identifies
each selected rectangle, its original angle and normalized resulting angle.
Compatible unknown objects are labelled unchanged and never interpreted as
geometry. There is no source decode, render, upload or file publication during
this preview.

Core `rotateRectangleGeometry` normalizes the two angle operands before addition,
avoiding overflow from large finite deltas, and validates the complete resulting
geometry, including stroke-grown rotated export bounds. The dialog and ordinary
keyboard/guarded session edits share this production helper. Every selected
represented rectangle must validate before one complete history commit. A mixed
selection preserves inert unknown objects; hidden, locked and opaque-lock groups
are protected. An unknown-only selection has no native rotation action.

Angle edits update only the owned text preview. Explicit Rotate captures one
angle; Cancel/Escape, invalid entry and parent/dialog destruction do not apply an
edit. Native labels, descriptions, a read-only selectable result list, Tab,
Alt+A and Alt+R provide keyboard access. Cancel is the default. Invalid input
disables Rotate and shows a text explanation, rather than a color-only cue.
Source pixels, object bounds, IDs, order, other styles, assets, unknown compatible
fields, creation defaults, viewport, preferences and files remain intact.

The commit checks opening history state, exact selection and document generation.
A changed document/selection or a replacement session rejects the accepted draft
with `annotation-edit-stale`; it preserves the intervening state. Main editor
editing/output/selection controls are unavailable during the modal operation.
Capture shares the canvas pending-dialog guard, disables its editing/history/list
controls and refuses capture Apply until the draft finishes. Tool changes and
nested rotation cannot interrupt the pending draft. Busy-state restoration and
rejection notification tolerate owner destruction.

A successful rotation is one atomic edit. Undo/Redo restores the exact document
and selection, with the normal persistence checkpoint rules. Zero/full-turn
results that change no represented field preserve Redo and the checkpoint.
Preparation allocation failures use `annotation-edit-failed`; invalid geometry
uses `annotation-geometry-invalid`. No compatible raw payload is executed or
new annotation schema inferred.

## Evidence

- `EditorSessionTest` checks independent 350 + 17.25 → 7.25 and
  37.25 + 17.25 → 54.5 group results, complete unchanged fields/assets/unknown
  data and defaults, exact history/selection/checkpoints, stale state/selection,
  protected groups, non-finite deltas, derived quarter-turn overflow, huge finite
  normalization and unchanged full-turn/zero Redo branches.
- `EditorCanvasTest` uses the actual owned dialog and native activation,
  mnemonics and degree entry. Preview has no document/history mutation;
  Apply/Cancel/no-op/invalid, stale state/selection, replacement with identical
  history/IDs, destroyed dialog/parent, pending observer destruction and reentrant
  stale rejection are exercised. `XERAHS_ANNOTATION_ROTATION_QA` exports a native
  fixture frame for visual review.
- `EditorWindowTest` selects a mixed group through the native list and opens the
  existing command using customized F10. It checks native text preview and modal
  controls, cancellation/invalid input/owner destruction, unchanged files and
  preferences, exact Undo/Redo and actual PNG/.xann save/reopen with original
  source, IDs, bounds, alpha/styles, assets and compatible unknown payloads.
- `CaptureAnnotationTest` reaches the shared control through a native toolbar
  button and Space, refuses Apply during the draft, and exercises Apply/Cancel/
  parent destruction. One Undo/Redo step and actual temporary PNG publication
  preserve the original embedded source; independent inside/outside pixel samples
  verify the accepted quarter turn.

All 44 local CTest targets passed in 39.36 seconds, with 114 unchanged published
vectors and 32 tooling tests (16 linter, 9 backlog, 7 runner). Six affected targets
passed against Qt 6.4.2 in 15.56 seconds. The native dialog was visually inspected;
allocator exhaustion is not fault-injected in these fixtures. Verification is
indexed in `ANNOTATION-ROTATION-EVIDENCE.json`. Fixtures use
temporary media/preferences and private D-Bus sessions, with no real desktop
capture, upload or print. Full resource/performance, native supported targets,
physical accessibility/high contrast, privacy/security, shared goldens, parity
and human release qualification remain Required.

## Remaining work

The subsequent [colour checkpoint](ANNOTATION-COLOR.md) guards stroke/fill choices
against stale state/selection/defaults/generation/preferences and owner destruction,
preserving selected-object/default/history behavior. [Numeric drafts](RECTANGLE-DRAFTS.md)
now add generation guards and owned nested colour choices. Next guard native
thickness commands against default-only publication after a refused selected
batch. Resolve the pending shared Ellipse, clipboard-asset and output-
format decisions recorded in ANNOTATION-TOOLS.md, ANNOTATION-TRANSFER.md and
EDITOR-SAVE.md while continuing all tools/effects/canvas/output, workflows,
capture/recording/audio, uploads, compatibility, supported target/packaging and
human review/release requirements. Full Product Contract completion is unfinished.
