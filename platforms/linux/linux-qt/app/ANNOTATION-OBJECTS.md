# Native annotation object selection

Partial candidate evidence for EA-003/017/018/019/020 and ES-004/006/015/019/026.
All EA requirements remain Required. Full tools/fields, resources/performance,
physical assistive technology, native target matrix, parity and human release
qualification remain unfinished.

## Native behavior

View → Select Annotations… opens and focuses the editor's Annotation objects
dock. Capture annotation exposes the same native list alongside its canvas.
Rows follow document z-order from bottom to top, with owned UUIDs, type/status
labels and source-pixel bounds in native accessible descriptions and tooltips.
The old unsupported-only dock is replaced by this complete object list; initial
unsupported documents still show the dock automatically. Users can close or move
it and reopen it with the command. Its visibility is ephemeral, not a saved
preference or document edit.

Up/Down and Home/End navigate. Shift extends a range; Control+Space toggles the
focused row; Control+A selects all. Control+A in the list retains precedence
over customized command shortcuts. Escape clears selection, and Enter returns
to the canvas for existing move/resize/rotation/order/delete controls. The list
has native visible focus and selection state, plus a [selected] text marker.
Hidden, locked and opaque protection states are labeled and remain protected by
the same Core policy. Unsupported objects are labeled as lacking geometry
editing; their raw compatible payloads stay inert and preserved. Type-name
hints are bounded to 64 characters and neutralize markup, controls and bidi
formatting before display. Tooltips contain owned plain text and UUID/bounds,
not raw executable or rich-text payloads.

Selection dispatch uses the frozen displayed history state and canvas document
generation. Intervening state/source replacement rejects stale rows and restores
actual selection without undoing an intervening edit. Pointer selection and
history/order/deletion/source replacement refresh the list by UUID. Selection
alone changes no document, source, default, history, dirty checkpoint or file.
Core filtering uses a requested-ID set rather than repeated linear searches,
including large, duplicate and unknown ID requests.

The list yields to active gestures, modal editing and output/source operations.
Busy or stale programmatic changes restore the actual Core selection. Model
reset/data notifications and owner destruction use guarded lifetimes; rows own
text/IDs rather than document pointers. Capture Undo/Redo refreshes the shared
list. Keyboard selection followed by canvas movement composes accepted pixels
before the actual temporary PNG publication. The main editor uses the same
selection for existing actions and actual PNG/.xann save/reopen.

`focus-annotations` adds one hidden native command (48 total), with the fourteen
visible defaults unchanged. F11 customization is exercised through the existing
native input registry. The list does not add a shared tool ID or persisted
annotation field, and does not change the active tool.

## Evidence and limits

- `AnnotationObjectNavigatorTest` exercises actual keyboard single/range/toggle/
  all/clear selection, textual/accessibility labels, canvas focus return,
  pointer synchronization, order/delete/Undo/Redo and replacement identities,
  disabled/stale/protected refusal and model/owner destruction.
- `EditorSessionTest` verifies stale guarded selection, preserved Redo/checkpoint,
  unchanged serialized documents and a 1000-object duplicate/unknown-ID request.
- `EditorWindowTest` exercises real customized F11, native protected action
  availability, selection without file/preference changes, keyboard movement,
  actual save/reopen with preserved source/IDs/unknown payloads, and existing
  unsupported flatten/clear recovery assertions against the new full list.
- `CaptureAnnotationTest` exercises actual keyboard selection and focus return,
  then verifies movement, original embedded pixels and actual PNG publication.
- Toolbar preference/native registry tests cover the new hidden command and
  unchanged input/default policy. Existing published vectors and renderer are
  unchanged.

`XERAHS_ANNOTATION_OBJECTS_QA` exports one native fixture frame for visual review.
The selected marker, protection labels, ordinary focus/scroll controls and
bounded unknown-type hints were inspected. The new native target uses a private
D-Bus session; fixtures use temporary media/preferences and no real capture,
upload or printer. Verification is indexed in the candidate map and checkpoint
report. The final local suite passed all 44 CTest targets in 36.53 seconds and
all 114 unchanged published vectors. The 32 tooling tests passed (16 linter,
9 backlog, 7 runner); all eight affected targets passed against Qt 6.4.2 in
13.14 seconds. These fixtures do not accept physical screen-reader/high-contrast,
large-document performance, supported desktop/native, privacy/security, parity
or release gates.

Pending shared Ellipse, transfer-asset and output-format decisions remain
recorded in ANNOTATION-TOOLS.md, ANNOTATION-TRANSFER.md and EDITOR-SAVE.md.
The subsequent [group move checkpoint](ANNOTATION-MOVE.md) freezes pointer
previews and commits against stale document/selection changes. Next independent
work guards native stroke/fill color dialogs after the
[rotation checkpoint](ANNOTATION-ROTATION.md) added frozen state/selection and
owner lifetime. Every other capture, recording/audio,
upload, workflow/settings, tool/effect/output, application/compatibility,
packaging/native-matrix and human/release requirement remains in full scope.
