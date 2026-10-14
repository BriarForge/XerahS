# Guarded annotation colour choices

This is partial native evidence for EA-014/017/018/019/020 and
ESET-006/007/015. Every requirement remains Required. This checkpoint does not
activate the full annotation/settings capabilities or approve parity/release.

## Production behavior

The editor's Stroke colour and Fill colour commands and the capture annotation
colour toolbar share an owned Qt widget picker with alpha controls, explicit
OK/Cancel and a visible explanation of the affected objects and defaults.
The opening RGBA value is set after choosing the widget backend, because switching
backends can reset QColorDialog's constructor colour. Picker previews change
only the picker; no document or preference is published before OK.

The QtCore EditorSession guarded operation freezes history state, exact selection
and the complete creation style. Native callers additionally freeze document
generation; editor callers freeze the preference revision. Intervening edits,
selection/default changes, replacement documents with matching state/IDs, and
preference changes reject the choice with a typed stale diagnostic. Hidden or
locked selections and selections containing only unsupported objects refuse the
operation. Mixed editable rectangles and inert unknown objects change only the
rectangles, preserving every unknown payload, ID, order, asset and other field.
Derived geometry admission applies to the entire batch before committing, and a
failed batch restores its creation defaults too.

Accepted selected-object changes create one undo operation with exact selection
recovery. Undo/Redo retains the separately updated future defaults. Empty
selection changes only future defaults, preserving document, checkpoint and Redo.
A matching colour adds no history. New rectangles snapshot the chosen RGBA value.
The existing legacy setters and published adapter vectors are unchanged.

Editor choices persist canonical #AARRGGBB stroke/fill and thickness through the
existing atomic preference store, then publish creation defaults to editor
windows. A save failure retains the accepted, undoable document edit and current
in-memory defaults, keeps persisted/global preferences unchanged and reports the
failure. The picker and publication callbacks use weak owners, including an
observer that destroys the window during preference publication. Busy controls
block another document command, ordinary close and source adoption during the
picker. Owner/dialog destruction cancels an unaccepted choice safely.

Capture choices update only that capture's creation defaults. A separate native
colour toolbar provides keyboard-focusable buttons without crowding the existing
annotation toolbar. Pending choices block history, object selection and capture
Apply. Actual accepted capture publication composites alpha once over the original
source and retains the source in the editable document. Editor preference bytes,
values and revision remain unchanged.

## Indexed verification

- `EditorSessionTest`: both colour targets, mixed unknown objects, independent
  expected field changes, new-object snapshots, one-operation history, checkpoints,
  Redo/no-op/default-only behavior, stale state/selection/defaults, invalid targets,
  protected/unsupported selections and whole-batch geometry failure/default rollback.
- `EditorCanvasTest::colorPickerGuardsDocumentSelectionDefaultsAndOwner`: 22 native
  picker cases covering actual OK/Escape input, exact opening RGBA, preview isolation,
  no-op/default-only, intervening state/selection/defaults, matching-state replacement,
  owner/dialog destruction and rejection observers. A pending observer can destroy
  the canvas before the picker opens.
- `EditorWindowTest::colorChoicesHaveGuardedDefaultsPublicationAndActualPersistence`:
  18 native hotkey/picker cases, busy/close guards, stale preferences, replacement,
  owner/dialog/publication destruction, locked preference storage, Undo/Redo,
  actual PNG/.xann save/reopen, source/assets/unknown preservation and canonical
  preference restart. Existing multi-window drawing/default tests remain active.
- `CaptureAnnotationTest::captureColorChoicesPreserveAlphaAndStayLocalBeforePublication`:
  eight keyboard picker cases, accessible names/focus, group Apply/Cancel,
  future-default creation, one-step Undo/Redo, destruction, untouched private editor
  preferences and actual saved PNG. Independent source-over samples expect
  `(0,128,127)` for half-alpha green over opaque blue; Cancel preserves blue/red.

All fixtures use temporary files/preference/QSettings stores and private buses.
They perform no real capture/upload/print and do not access live Avalonia settings.
`ANNOTATION-COLOR-EVIDENCE.json` indexes the verified checks. The optional native
frame is reproducible with `XERAHS_ANNOTATION_COLOR_QA=/tmp/xerahs-annotation-color.png`
on `editor-canvas-test colorPickerGuardsDocumentSelectionDefaultsAndOwner:fill-apply`
under the existing private test bus and offscreen Qt platform. The inspected frame
shows colour/alpha fields, focused OK, Cancel and the wrapped scope description.
Physical AT/high-contrast/desktop/native acceptance remains Required.

## Remaining work

The subsequent [numeric draft checkpoint](RECTANGLE-DRAFTS.md) adds document-
generation guards and an owned nested alpha picker to Rectangle creation/properties,
including matching-state replacement and actual persistence/publication recovery.
The [thickness checkpoint](ANNOTATION-THICKNESS.md) now guards document/default
admission and publication lifetime. Next expose guarded native ordering controls
and catalogue gestures. Complete tool and
field coverage, shared Ellipse schema/rasterization, clipboard assets and output
format decisions, resource/performance/native matrices, privacy/security/a11y/parity,
packaging and human/release gates remain Required. All other contracted application,
capture/recording/audio/upload/workflow/settings/effect/compatibility domains remain
in scope. Full Product Contract completion is unfinished.
