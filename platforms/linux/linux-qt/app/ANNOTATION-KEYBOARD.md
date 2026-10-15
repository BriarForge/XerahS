# Guarded native keyboard transforms

This is partial native evidence for EA-003/014/017/018/019/020,
ES-004/005/006/007/008/019/026/027 and ESET-016. Full requirements and
native/human qualification remain Required. Shared schemas, vectors, expected
results, dispositions and approvals are unchanged. The shortcut catalogue still
awaits product-owner approval; native candidate gestures are not that approval.

## Production behavior

The focused editor and capture canvas route arrow movement, Alt+arrow resize
and R quarter turns through one native admission path. It freezes history state,
exact UUID selection and document generation before notifying controls. A
matching-state/UUID replacement cannot retarget the edit. Pending interaction
blocks recursive input. Disabled canvases, including one disabled by a pending
observer, cannot apply a transform. Weak-owner guards contain destruction during
pending, rejection and completion notifications.

Arrows move one image pixel; Shift moves ten. Alt+arrows change the bottom-right
corner by those steps, retaining the original coordinate-axis resize convention
even for rotated rectangles. R rotates clockwise 90 degrees about each object's
bounds centre; Shift+R rotates counterclockwise. Each held-key repeat event
freezes a fresh snapshot and commits at most one complete history operation.
Empty-selection arrow panning and existing modifier interpretation remain
unchanged. Accessible canvas instructions describe steps, rotation and repeat.

A new QtCore resize overload checks state and exact selection before delegating
to the existing resize operation, alongside the existing move/rotation guards.
Legacy Core/adapter paths and geometry algorithms are unchanged. Known selected
rectangles transform as one atomic batch. Hidden or locked members protect the
whole group. Compatible unknown objects remain inert, with raw payload retained;
an unknown-only selection commits no transform. Finite/positive geometry checks
refuse the entire batch, including collapse or overflow after an earlier member
would have changed. No-op, protected and stale requests retain Redo/checkpoints.

Accepted changes retain UUIDs, order, other object fields, source pixels,
embedded assets, selection, creation defaults and editor preferences. Undo/Redo
restores exact document and meaningful selection state. Real editor PNG/.xann
save/reopen and accepted capture publication use the edited document. Capture
Cancel or owner destruction keeps the original working capture.

The editor's ordinary Close guard now includes an active canvas interaction.
The new minimum-Qt test reproduced Close succeeding during a clean keyboard
edit's pending notification, persisting window preferences while admission was
still running. Close now waits for the edit or gesture to finish/cancel; source,
history and preferences remain owned. This does not replace dirty Save/Discard/
Cancel handling or the existing worker preparation guard.

## Indexed verification

- `EditorSessionTest::guardedGroupResizePreservesFieldsAssetsDefaultsAndHistory`
  checks independent fractional geometry, rotated members, inert unknown fields,
  root/source/assets/defaults, one group history step and exact Undo/Redo.
  `guardedResizeRefusalsKeepCompleteStateAndRedo` covers fourteen empty,
  unknown-only, hidden/locked/opaque-lock, stale state/selection, collapse,
  NaN/infinity/overflow and zero/rounded-no-op cases. Existing guarded move and
  rotation tests supply their corresponding finite/protection/history evidence.
- `EditorCanvasTest::keyboardTransformsUseImagePixelsAndKeepRepeatHistory`
  uses eighteen actual key combinations covering all four movement/resize
  directions, one/ten-pixel steps and both quarter turns. Independent expected
  bounds/angles, held-key repeat, exact two-step Undo/Redo, Redo truncation,
  selection/default/source and zoom/pan isolation are checked.
  `keyboardTransformsGuardPendingStateSelectionGenerationAndOwner` covers
  forty-two move/resize/rotate cases for recursive input, changed defaults,
  stale state/selection, matching-state replacement, disabled/late-disabled,
  protected/unknown-only groups and pending/rejection/completion destruction.
- `EditorWindowTest::keyboardTransformsPersistNativeGroupAndExactHistory`
  covers six actual key gestures, pending Close refusal and native action/list
  availability, one-step exact history and real PNG/.xann reopen. Independently
  sampled source/red pixels prove movement, resize and quarter-turn export.
  Source, assets, unknown/root fields, private preference bytes/revision and
  local defaults remain intact.
  `keyboardTransformsKeepNativeTextListModalAndBusyPrecedence` checks real text
  insertion/cursor movement, object-list movement/selection, modal text focus,
  blocked keys during a gated native save, preserved state and later recovery.
- `CaptureAnnotationTest::captureKeyboardTransformsPublishAcceptedStateAndPreserveSource`
  covers fifteen native move/resize/rotation cases: accepted, protected,
  cancelled, pending destruction and completion destruction. Actual keys,
  exact Undo/Redo/selection, local numeric defaults and list state feed the
  production annotation port and normal rendering/save worker. Independent
  red/blue pixels and real companion reopen retain source, assets and fields;
  cancellation/destruction publishes only the original capture. Private editor
  preferences remain unchanged. The fixture captures accepted values on the
  dialog's finished signal before its established delete-on-close lifecycle.

Fixtures use temporary files/preferences/QSettings, synthetic pixels and private
buses. No real capture/upload/print, live Avalonia configuration or system
settings are touched. The inspected capture frame is reproducible with
`XERAHS_KEYBOARD_QA=/tmp/xerahs-keyboard.png` on capture row `1-apply` under the
existing offscreen/private-bus setup. It shows selected resized rectangles,
non-color selection outlines, the selected object list, local 3 px default,
companion disclosure and Apply/Cancel. Physical keyboard/AT/high-contrast and
complete supported Linux matrices remain Required. The adjacent candidate
`ANNOTATION-KEYBOARD-EVIDENCE.json` records actual verification.

## Next work and full scope

[Native capture Crop](CAPTURE-CROP.md) now adds owned preview, frozen admission,
source/history restoration and independently verified crop handoff through real
publication. Next migrate main-editor numeric/pointer Crop to the owned preview,
retaining Core policy with frozen state/selection/generation and worker/close/
owner guards, and continue the full native implementation sequence.

The [ES-014/030 decision](EMPTY-SIDECAR-CLARIFICATION.md), Ellipse fields/rendering,
shared clipboard/assets and PNG/JPEG/WebP output policies remain pending. No
continuation message is treated as a policy approval. All annotation tools,
232 effects, canvas/output, capture mapping/topology, recording/audio, uploads,
436 workflows, 922 settings, application/history/automation/compatibility,
packaging/migration/updates/rollback, supported Linux targets, resource/performance,
privacy/security/accessibility/parity and human/release gates remain in scope.
