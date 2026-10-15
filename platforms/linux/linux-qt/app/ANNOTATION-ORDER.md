# Guarded native annotation ordering

This is partial native evidence for EA-003/015/017/018/019/020 and ESET-011/016.
All full Product Contract and native/human gates remain Required. Shared schemas,
expected vectors, dispositions and approvals have not changed. The shortcut
catalogue still says `draft-native-equivalence-pending-product-owner-approval`;
these are native candidate bindings, not an approval of that catalogue.

## Production behavior

Bring Forward, Send Backward, Bring to Front and Send to Back are available in
the editor Edit menu and custom toolbar registry, and in a separate focusable
capture ordering toolbar. The editor now registers 52 commands with its fourteen
visible defaults unchanged. Existing explicit empty custom layouts stay empty;
fixed menus remain reachable. The four stable IDs follow the existing catalogue.

A new QtCore admission overload freezes history state and exact UUID selection,
rejects invalid commands, and delegates accepted requests to the existing
ordering algorithm. It preserves relative order within the selected group and
within other objects. Hidden or locked members protect the whole selection.
Compatible opaque objects can change array position but retain their raw payload,
including unknown fields and opaque zIndex values, without executing it. Array
order remains authoritative; known object zIndex serialization follows position.
Legacy adapter calls and the ordering algorithm are unchanged.

Native commands also freeze document generation before notifying observers. A
matching-state/UUID replacement cannot retarget an operation. Pending/busy and
weak-owner guards contain reentrant commands, ordinary close and destruction
while disabling actions or reporting rejection. Actual changes commit one history
operation and retain exact selection; Undo/Redo restores it and checkpoint state.
An empty/protected selection is unavailable. Edge/all-selected no-ops and invalid
or stale requests keep history, Redo and checkpoints. Creation defaults and editor
preferences never change through ordering.

On the focused canvas, Home brings to front, End sends to back, Page Up brings
forward and Page Down sends backward. Existing brackets and Control+brackets
route through the same guard. One physical press creates at most one ordering
operation; repeated key events do not accumulate history. These default gestures
are canvas-local, preserving native object-list/text navigation. Custom toolbar
bindings to the protected ordering keys likewise yield to native list and text
navigation, modal dialogs and busy operations. Native key normalization maps
Qt Page-key spellings to the existing catalogue names without changing persisted
hotkey syntax. Other custom commands keep their
existing routing. Accessible canvas instructions and action tips describe the
keys and operation; capture buttons are keyboard-focusable with native names.

## Indexed verification

- `EditorSessionTest::guardedOrderingPreservesGroupPayloadAndHistory` declares
  twelve independent expected array orders across four commands and single,
  discontiguous group and opaque-only selections. It checks source/assets/fields,
  exact selection, one undo step, Redo truncation, dirty checkpoint and Undo/Redo.
  `orderingNoopAndRefusalKeepCheckpointAndRedo` covers twelve empty/all/edge,
  hidden/locked/opaque-lock, stale state/selection and invalid-command cases.
- `EditorCanvasTest::catalogueAndBracketOrderingKeysCommitOneGroup` exercises
  eight actual default/alias gestures and repeated-key suppression.
  `nativeOrderingGuardsPendingStateSelectionReplacementAndOwner` covers eight
  pending/reentrant/disabled/stale/replacement/destruction cases, including an
  independent creation-default change which ordering leaves intact.
- `EditorWindowTest::nativeOrderingCommandsPersistArrayOrderAndRenderedPixels`
  covers eight actual menu/custom-hotkey cases with fixed-menu reachability,
  object-list order, one-step checkpoint/selection history and real PNG/.xann
  save/reopen. Independent blue/yellow overlapping pixels prove export uses the
  new order. Source, embedded assets, unknown payload, root fields and source
  binding persist; private preference bytes/revision stay unchanged.
  Eight refusal/no-op/stale-selection/matching-state-replacement/busy/refusal-destruction
  cases preserve current documents and files without output.
- `CaptureAnnotationTest::captureOrderingButtonsKeepGroupOrderThroughHistoryAndPublication`
  exercises all four native buttons with Space, accessible role/name, local
  defaults, list order, exact Undo/Redo and empty-selection availability. A
  fixture-populated native dialog runs through the production annotation port;
  the normal worker validates and saves the accepted document and companion.
  Independent overlapping blue/yellow pixels, editable reopen and original
  source/asset retention verify publication. Preference bytes/values/revision
  remain unchanged. The ordinary coordinator is covered by the existing native
  capture fixtures; the new ordering fixture uses the real port with seeded data.
- `NativeEditorToolbarTest::customizedOrderingNavigationKeepsNativeListAndTextPrecedence`
  checks canvas activation, actual Home/End/Page list navigation and text cursor
  movement, empty toolbar reachability, modal and busy refusal, and recovery.
  `ToolbarPreferencesTest` checks matching protected IDs and collision rejection.

Fixtures use temporary files/preferences/QSettings and private buses. No real
capture/upload/print or live Avalonia configuration is touched. The inspected
capture frame is reproducible with `XERAHS_ORDER_QA=/tmp/xerahs-order.png` on the
capture ordering `front` row using the existing offscreen/private-bus setup. It
shows all four order buttons, selected objects in the updated list, the numeric
local default, companion disclosure and explicit Apply/Cancel. This synthetic
review does not replace physical keyboard/AT/high-contrast/desktop qualification.
`ANNOTATION-ORDER-EVIDENCE.json` indexes the verification results.

## Next work and full scope

[Guarded native deletion](ANNOTATION-DELETE.md) now supplies state/selection/
generation admission, capture Delete and native lifetime/history/persistence
coverage. [Guarded keyboard transforms](ANNOTATION-KEYBOARD.md) now retain native
move/resize/quarter-turn steps and repeats with exact group history and output.
Next add native capture Crop with an owned cancellable Core canvas handoff and
truthful coordinate/history/source/default/unknown-geometry behavior. Continue
remaining annotation tools/fields and shared decisions with the full contract sequence.

Ellipse schema/rasterization, clipboard assets and output-format decisions remain
pending in ANNOTATION-TOOLS.md, ANNOTATION-TRANSFER.md and EDITOR-SAVE.md. All tools,
232 effects, canvas/output, capture mapping/topology, recording/audio, uploads,
436 workflows, 922 settings, application/history/automation/compatibility,
packaging/migration/updates/rollback, supported Linux matrices, resource/performance,
privacy/security/accessibility/parity and human/release gates remain in scope.
