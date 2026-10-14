# Atomic native stroke thickness

This is partial native evidence for EA-003/005/014/017/018/019/020 and
ESET-006/009/015. All requirements and full Product Contract/native/human gates
remain Required. No shared schema, expected vectors or acceptance changed.

## Production behavior

The QtCore session now admits guarded stroke-width edits against frozen history,
exact selection and the complete creation style. Hidden/locked groups and
unsupported-only selections refuse before changing document or defaults. Mixed
editable rectangles and unknown objects change only the rectangles. Finite width
validation and whole-batch derived geometry admission precede one history commit;
failure restores the creation default too. Colour and thickness share admission,
with the existing colour result name retained as an alias. Legacy setters and
adapter calls remain unchanged so published seams retain their existing behavior.

Editor Stroke +/Stroke − now use this guard, freezing document generation and
preference revision before busy notifications. Reentrant changes, replacement,
ordinary close and overlapping document commands cannot retarget the operation.
Successful selected edits make one undo step and update future creation defaults;
empty selection changes only defaults, and matching/boundary choices add no
history. Undo/Redo preserves the separately updated future default. Existing
native command clamps remain 1..10000; Core property widths retain their existing
positive finite range up to 10000, including fractional values.

Only admitted choices persist drawing defaults through the atomic native store.
A store failure retains the accepted undoable document edit and in-memory default,
keeps persisted/global preferences unchanged and reports the failure. The shared
colour/thickness publisher copies its style and checks a weak window owner after
synchronous publication, including observers that destroy the editor. Successful
preferences survive restart. Main command count and fourteen visible defaults
remain unchanged.

Capture annotation now exposes keyboard-focusable Stroke +/Stroke − beside its
colour controls. A visible, accessible numeric label states the default for new
rectangles. Capture choices use frozen state/selection/defaults/generation and
pending/reentrancy/owner guards, update only that capture, and never persist editor
preferences. History refreshes the style controls while retaining future defaults.
Actual capture Apply uses the committed annotations and keeps its original source.

## Indexed verification

- `EditorSessionTest::guardedThicknessCommitsOneGroupAndSnapshotsDefaults` checks
  four positive widths including fractional and maximum bounds, mixed unknown
  objects, source/assets/other-field preservation, one-step exact history and new
  object snapshots. Thirteen refusal cases cover protection, unsupported-only,
  geometry overflow, stale state/selection/defaults, zero/negative/over-limit/NaN/
  infinity. Separate no-op/default-only checks preserve checkpoints and Redo.
- `EditorCanvasTest::thicknessCommandsFreezeStateDefaultsGenerationAndLifetime`
  checks fourteen increase/decrease cases: group/default-only edits, matching-state
  replacement, stale selection/defaults, reentrant refusal and destruction during
  pending notification/rejection. Source and replacement documents stay intact.
- `EditorWindowTest::thicknessCommandsGuardPreferencePublicationHistoryAndOwners`
  checks twenty-four actual hotkey cases: group/default-only/boundary behavior,
  protected and unsupported availability, intervening selection/preferences/
  generation, busy/publication destruction, locked store, Undo/Redo, canonical
  preference restart and actual PNG/.xann save/reopen. Existing colour and
  multi-window preference tests exercise the shared publisher too.
- `CaptureAnnotationTest::captureThicknessControlsStayLocalThroughHistoryAndPublication`
  checks four actual keyboard button cases, accessible names/current value,
  selected/default-only/new-object styles, one-step history and recovered control
  availability. Actual saved PNG/receipt retain the chosen widths and original
  source, with independent green/blue pixels. Private editor preference bytes,
  values and revision remain unchanged.

All fixtures use temporary files/preferences/QSettings and private buses. No real
capture/upload/print or live Avalonia settings are used. The optional inspected
capture frame is reproducible with `XERAHS_THICKNESS_QA=/tmp/xerahs-thickness.png`
on the capture thickness fixture under its existing offscreen/private-bus setup.
It shows the four style buttons, visible numeric future-default label, selected
rectangles, editable-companion disclosure and explicit Apply/Cancel. Physical AT,
high-contrast and complete desktop/target qualification remain Required.
`ANNOTATION-THICKNESS-EVIDENCE.json` indexes verification.

## Next work and full scope

Next expose the four ordering commands through accessible native editor/capture
controls, with guarded state/selection/generation dispatch to the existing Core
ordering logic, catalogue Home/End/PageUp/PageDown gestures with text/list/canvas
precedence, exact relative-order/history and actual persistence/publication
failure evidence. Current native ordering is reachable through canvas bracket
shortcuts; visible editor/capture command surfaces remain unfinished.

Ellipse schema/rasterization, clipboard assets and output-format decisions remain
pending in ANNOTATION-TOOLS.md, ANNOTATION-TRANSFER.md and EDITOR-SAVE.md. All tools,
fields, effects, canvas/output, capture mapping/topology, recording/audio, uploads,
workflows/settings/catalogues, application/history/automation/compatibility,
packaging/migration/updates/rollback, supported Linux matrices, resource/performance,
privacy/security/accessibility/parity and human/release gates stay in full scope.
