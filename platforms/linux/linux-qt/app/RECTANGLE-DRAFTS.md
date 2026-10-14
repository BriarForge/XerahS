# Numeric Rectangle draft identity and nested colour lifetime

This is partial native evidence for EA-003/004/014/017/018/019/020 and
ESET-006/007. Every requirement remains Required; the full Product Contract and
human/native/parity/release gates remain unfinished.

## Production behavior

The editor and capture numeric Create Rectangle and Rectangle Properties paths
now freeze document generation together with their existing Core history and
selection guards. Matching history numbers and object UUIDs in a replacement
session no longer admit a draft opened for the previous document. An accepted
stale draft reports `annotation-edit-stale` before invoking a mutation; Cancel
remains non-mutating. The replacement, original session, defaults, checkpoints,
selection and Redo branch remain intact. A rejection observer may destroy the
canvas safely. Capture creation also captures image dimensions before pending
notifications, retaining the opening bounds/default snapshot.

Rectangle Properties now uses the shared owned alpha picker. Its visible scope
states that a colour changes only the properties draft and requires the outer
Rectangle Properties OK to edit the document. An accepted nested choice updates
only the corresponding canonical RGBA text field. Cancel, picker destruction,
properties-dialog destruction and editor/canvas destruction return safely
without an accepted document operation. Weak dialog/field owners prevent access
to deleted widgets. Future creation defaults and editor preferences are unchanged.
The original finite property validation, imported endpoint precision, atomic
Core mutation, unknown/asset preservation and exact Undo/Redo remain in use.
No recognized fields, annotation schema, resource policy, adapter operation or
shared expected vectors changed.

## Indexed verification

- `EditorCanvasTest::numericRectangleDraftsRejectMatchingStateReplacement` has
  eight create/properties cases with replacement after opening, replacement
  during the pending notification, Cancel after replacement and destruction
  during typed rejection. Both sessions deliberately have equal history state
  and UUID selection plus a reachable Redo branch. Exact documents, history,
  selection, defaults and source are preserved, and pending controls recover.
- `EditorCanvasTest::nestedRectangleColorsOwnDraftAndLifetime` has twelve actual
  keyboard picker cases for stroke/fill: accepted colour plus outer OK, outer
  Cancel, picker Cancel, picker destruction, properties destruction and canvas
  destruction. Opening RGBA and scope are checked; preview and accepted nested
  choice alone leave the Core document unchanged. Accepted outer OK makes one
  history edit; refusal/no-op preserves the existing checkpoint/Redo.
- `EditorWindowTest::numericRectangleReplacementRefusalKeepsFilesAndAllowsFreshEdit`
  has four native hotkey cases. Matching-state replacement and Cancel preserve
  actual PNG/.xann/preference bytes and original Redo. Ordinary source adoption
  and close are blocked while the modal operation is pending. Reloading the
  original image and accepting a fresh draft recovers one-step Undo/Redo and
  actual PNG/.xann save/reopen, preserving fields, source, assets and unknown data.
  Save's SHA-256 binding is checked against actual exported PNG bytes; its UTC
  modification timestamp is validated separately from unchanged document fields.
- `EditorWindowTest::rectangleNestedPickerSurvivesEditorDestruction` covers both
  nested colour targets and destruction of the editor while the child picker
  executes. Original image/preference bytes are unchanged and no companion is
  written.
- `CaptureAnnotationTest::numericRectangleReplacementRefusalPreservesActualCapturePublication`
  has four create/properties Apply/Cancel cases with equal state/UUID selection
  and a live Redo branch. Capture Apply is refused during the draft. Subsequent
  actual saved PNG/receipt retain the original capture's committed annotations
  and immutable source; an independent green/blue source pixel check guards
  against publishing the stale replacement or applying its width draft.

Existing creation, properties, color, movement, rotation, persistence and shared
conformance fixtures remain active. Every fixture uses private buses and temporary
files/preferences/QSettings; no real capture/upload/print or live Avalonia settings
are used. `RECTANGLE-DRAFTS-EVIDENCE.json` indexes the verified checks.
The optional inspected frame is reproducible through
`XERAHS_RECTANGLE_NESTED_COLOR_QA=/tmp/xerahs-rectangle-nested-color.png` on
`editor-canvas-test nestedRectangleColorsOwnDraftAndLifetime:fill-apply`, with the
existing private test bus and offscreen Qt platform. Alpha, focused OK/Cancel and
wrapped draft-only scope are visible. Physical AT/high-contrast/target acceptance
remains Required.

## Next work and full scope

The subsequent [thickness checkpoint](ANNOTATION-THICKNESS.md) guards complete
document/default admission and native publication/lifetime while keeping capture
defaults local. Next expose four guarded accessible ordering controls and the
catalogue gestures, retaining text/list/canvas precedence and actual
history/persistence/publication evidence.

Shared Ellipse schema/rasterization, clipboard assets and output-format decisions
remain pending in ANNOTATION-TOOLS.md, ANNOTATION-TRANSFER.md and EDITOR-SAVE.md.
Complete annotation fields/tools/effects/canvas/output, capture topology,
recording/audio, uploads, workflows/settings/catalogues, application/compatibility,
packaging/updates/rollback, supported Linux matrices, resource/performance,
privacy/security/accessibility/parity and human/release gates remain Required.
