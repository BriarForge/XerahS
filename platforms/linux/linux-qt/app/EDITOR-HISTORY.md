# Owned main-editor source history

This is partial candidate evidence for ES-005/006/007/008/019, EC-017 and
EA-017/018/020. Full requirements, native targets and human/release gates remain
Required. Shared contracts, schemas, vectors, expected results, dispositions and
approvals are unchanged.

## Production behavior

Main-editor Undo/Redo now inspect the adjacent Core history state before changing
history. Source-changing restoration copies that immutable document before busy
notifications, then prepares validation, source pixels, normalization and private
sRGB display on an owned worker. A named native progress dialog exposes Cancel.
Only successful preparation against the frozen history state, exact selection,
canvas generation and target history ID may restore Core history and publish the
prepared source/display. Publication sets the source cache before native refresh,
so this path does not decode/convert the restored source on the GUI thread.

Core restores its original document, selection, source lineage, referenced assets
and checkpoint semantics. The worker's preparation document is not adopted: its
capture-oriented helper clears path/hash, whereas the actual history document's
identity, timestamps, source bytes, annotations and fields must remain exact.
Annotation-only Undo/Redo takes the same guarded admission without starting an
image worker or copying the complete target document.

The new QtCore history guard also checks the target's unique history ID. A new
edit followed by Undo can return to the same current state and selection while
replacing Redo. Preparing the old target then restoring a different branch is
refused. Missing/stale current state, selection or target changes nothing and
reports the existing annotation-edit-stale diagnostic. Replacement-session native
generation is independently checked.

The existing weak-owner output guard disables editing, output and menu controls,
prevents ordinary Close and restores surviving controls. Reentrant Undo/Redo,
canvas keys and quarter-turn commands are refused. Cancel button, Escape, child
or owner destruction releases the nested wait without joining a blocked worker.
The worker owns document values and cancellation/progress state, never widgets,
sessions or borrowed history pointers. Late completion cannot restore history or
write output. Normal progress cleanup disconnects its cancellation observers,
hides and detaches the child before deletion so a destruction observer can safely
delete the parent. Worker exceptions are redacted; failures retain history and
report a native diagnostic. No file is read or written by history preparation.

## Candidate verification

Seven Core rows verify exact Undo/Redo, missing target, stale state, stale
selection, stale target and replacement of the Redo branch while current state
and selection match. Refusal preserves document, assets, counts, dirty state and
selection; accepted restoration retains exact state/checkpoint semantics.

Three actual main-window rows use Ctrl+Z/Ctrl+Y over cropped sources with alpha
and Display P3 metadata. They preserve whole-document fields, original source
bytes/identity, surviving and restored selection, assets, viewport dimensions,
source/annotation pixels, dirty state and saved checkpoints before Crop, after
Crop or after Undo. Explicit Save after Undo reopens the actual PNG/.xann and
retains original source/profile/assets. Annotation-only move Undo/Redo does not
start another worker. History alone preserves destination/companion/preferences.

Twenty-two native rows cover both Undo and Redo with Cancel button/Space,
Escape, child destruction, owner destruction, worker failure, stale state,
stale selection, matching-state/selection replacement, busy-admission owner
deletion, progress-cleanup owner deletion and final-status owner deletion.
Blocked workers run off the GUI thread; cancellation and teardown return under
500 ms while their private gate remains blocked. Surviving controls and exact
history eligibility recover. Three direct native preparations reject corrupt
source, mismatched dimensions and excessive dimensions without partial results,
file reads or document mutation. The first focused run exposed an offscreen
focus fixture after closing Crop; keyboard cases now wait for the active parent
and canvas focus. A QList selection fixture typo was corrected during compilation.
Neither correction changes production policy or shared expected results.

The synthetic progress frame was inspected: restoring-history text, progress and
a readable native Cancel control are visible. Button role and native accessible
progress name are checked. This does not accept physical keyboard, screen-reader,
contrast, large-image performance, shutdown or supported target/human reviews.
Verification totals and exact previous CI are indexed in
[EDITOR-HISTORY-EVIDENCE.json](EDITOR-HISTORY-EVIDENCE.json); the checkpoint report
verifies its exact pushed revision and Ubuntu 24.04/Qt 6.4.2 CI.

## Remaining work

The subsequent [canvas worker checkpoint](CANVAS-WORKERS.md) migrates main-editor
mutation preparation and custom rotation previews to owned workers and guarded
publication, including numeric/clear dialog ownership and private view preparation.
The subsequent comparison and New/Open checkpoints own those preparation and
picker lifetimes. [Capture source-history](CAPTURE-HISTORY.md) now retains its
asynchronous UI while retiring cancelled jobs immediately and using the same
guarded adjacent-target Core admission. Full shutdown, resource and native
qualification remains Required.

Full tools/fields/effects/canvas/output, capture topology, recording/audio,
uploads/credentials, workflows/settings/catalogues, application/history/
automation/compatibility, packaging/migration/updates/rollback, supported Linux
matrix, resources/performance and human privacy/security/accessibility/parity/
release gates remain Required. Recorded Ellipse, clipboard-assets, output-format,
legacy-companion and shortcut decisions remain pending. No completion percentage
or release acceptance is established.
