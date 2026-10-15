# Main-editor owned Crop preview

This is partial native candidate evidence for EC-007/017, EA-013/017/018/019/020
and session history, persistence and close behavior. Every full requirement,
target and human/release gate remains Required. No shared contract, vector,
expected result, schema, approval or persisted format changes.

## Production behavior

Image → Crop Image and the canvas Crop tool share the same owned native preview
as capture annotation. A reverse pointer drag supplies the normalized retained
rectangle; the numeric route starts with the full source. Labelled Left, Top,
Width and Height controls, keyboard mnemonics, the actual source/annotation
preview, named progress, Apply Crop and Cancel expose one explicit edit.
Changing a bound cancels obsolete preparation. Only the current complete preview
can apply. Existing Core normalization, bounds, intersection and unknown-object
rules remain authoritative; no new geometry or pixel policy is introduced.

The main window freezes its document, source, history state, exact selection and
canvas generation before any busy notification. Its existing weak-owner output
guard disables editing/output/menu controls and prevents ordinary Close. A nested
owned dialog processes native events; Cancel, child deletion or forced window
destruction returns without joining an unfinished worker. Workers own only value
snapshots and shared cancellation state. Late completion cannot commit or save.
Reentry is refused. Stale state, selection or replacement generation refuses the
whole edit and reports that it was not applied.

A changed accepted preview enters Core canvas history once, preserves surviving
selection and publishes the prepared source/private sRGB display to the canvas.
It avoids another GUI-thread source decode/conversion on crop admission. Main
editor Crop continues to work after rotation/resize, which invalidate the private
capture source-lineage proof; ordinary Core canvas commit is protected by frozen
native admission instead. Capture's independent source-proof path is unchanged.
The shared dialog lives in the native editor-canvas library, with its existing
native codec dependency, so editor and capture use one implementation without
linking unrelated action chooser code. Core still depends only on QtCore and Qt
remains dynamically linked.

Cancel and full-image no-op preserve history, Redo, selection, files and drawing
preferences. Apply translates known objects, removes wholly outside objects and
retains intersecting geometry under the documented export clipping policy. It
retains independent embedded assets and unknown root/object fields. The dialog
explicitly states that Undo history and embedded assets can retain earlier pixels.
It makes no claim to remove all prior or sensitive pixels from an editable file.
Save remains an explicit later action, with its existing immutable PNG/companion
worker, source binding and checkpoint semantics.

## Candidate verification

`EditorWindowTest::ownedCropUsesNativeBoundsHistoryAndPersistence` has five rows:
numeric keyboard controls and Apply, reverse pointer drag, Crop after two inverse
canvas rotations, full-image no-op and Cancel. It verifies real native Button/
SpinBox roles/names, dimensions, alpha/profile/source pixels, selected-object
mapping, outside removal, fields/assets, no implicit file writes, preference
isolation, Redo truncation, exact one-step Undo/Redo, viewport source dimensions,
actual PNG/.xann reopening and dirty/saved checkpoint restoration. Save-time hash
and timestamp changes are checked as binding fields rather than incorrectly
expected to equal the pre-save snapshot.

`EditorWindowTest::ownedCropRefusesStaleEditsAndReleasesBlockedWorkers` has ten
rows: Cancel, child destruction, owner destruction, redacted worker failure,
unknown geometry, stale state, stale selection, equal-state/selection replacement,
owner deletion during busy admission and owner deletion during final status
notification. Deliberately blocked workers run off the GUI thread; Cancel and
forced teardown return in under 500 ms while their private gate remains blocked.
All unrelated bytes remain unchanged and surviving controls recover. Earlier
crop/save/open tests now drive the owned preview and retain their original pixel,
history and persistence expectations.

A synthetic 540 × 620 preview was inspected: all numeric labels and values,
alpha checkerboard, gradient source, translated red rectangle, retained-area
message, progress, Apply Crop/Cancel and truthful history/asset disclosure are
readable. This is offscreen candidate evidence; physical keyboard, screen reader,
contrast and native target acceptance still require their human matrix.

Verification totals and the exact prior/pushed CI evidence are indexed in
[EDITOR-CROP-EVIDENCE.json](EDITOR-CROP-EVIDENCE.json). The first focused run
found fixture-only assumptions about save bindings and the dimension JSON key;
they were corrected without changing shared vectors or production behavior.

## Remaining work

The subsequent [owned history checkpoint](EDITOR-HISTORY.md) moves main-editor
source-changing Undo/Redo preparation off the GUI thread with exact guarded
admission and nonblocking cancellation/owner teardown. Other canvas operations
still use the older stack-owned worker/progress path and must migrate before full
resource/shutdown qualification; that migration is the next implementation step.

All remaining annotation tools/fields, effects, inserted images/background,
general affine canvas transforms, output formats, capture topology, recording/
audio, upload/secrets, workflows/settings/catalogues, application/history/
automation/compatibility, packaging/migration/updates/rollback, supported target
matrix, resources/performance and human privacy/security/accessibility/parity/
release gates remain Required. EA-005, EA-016, EO-004, ES-014/030 and the draft
shortcut catalogue still await the recorded owner decisions. This checkpoint
neither approves those policies nor proves a production-readiness percentage.
