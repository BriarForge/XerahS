# Capture source-history lifetime and publication

Partial candidate evidence for ES-002/005/006/007/008/019, EC-007/017 and
PCA-003/008/010/011/015. Full native behavior, supported targets and human/release
gates remain Required. No shared contract, policy, vector, schema, status label,
disposition or approval changes.

## Production behavior

Capture annotation Undo/Redo keeps its asynchronous interface. Before notifying
native busy controls, the coordinator copies the target document and worker hook
and freezes the current state, exact selection, native document generation and
adjacent Core history ID. A worker owns those inputs and its cancellation,
progress and prepared result; it holds no widget, session or borrowed history
pointer. The capture dialog has one owned polling timer and identifies the active
job by its shared work identity.

Cancel, Escape, closing/rejecting progress or directly deleting it retires that
identity, stops polling and restores editing immediately. No worker is joined.
The progress widget is disconnected, hidden, detached and safely deferred for
deletion when a cancellation signal may still be on its stack. A retry, crop,
annotation edit or capture Apply can proceed while cancelled work finishes.
Late success and late exceptions cannot change a newer document, controls,
history eligibility or status. Worker errors show fixed native messages.

Normal completion keeps controls busy while progress is disconnected and deleted.
Only a surviving owner with matching state, exact selection, generation and
adjacent target may call the existing QtCore `restoreHistory` guard. That Core
call restores its exact document, selection, assets and private composed capture
source-region proof. Only prepared source pixels and the private display are
published from the worker result. The capture-normalized worker document is
never adopted, preserving original source identity and fields in history.
Annotation-only history uses guarded Core admission without an image worker.

Source/display, viewport and status are complete before controls become ready.
Transition guards prevent recursive history/crop admission during cleanup and
refresh. Rejected or directly completed parent dialogs cancel and retire current
work, stop polling and close any crop child. Direct Accepted completion goes
through the same readiness/snapshot admission as Apply; busy work cannot bypass
it. Weak ownership protects busy entry, child cleanup and final control refresh.
No file or preference is modified by history preparation.

## Native evidence

`CaptureHistoryTest` is a separate private-bus/offscreen native target with the
existing 30-second timeout. Its 31 behavior cases cover:

- Both Undo and Redo with Cancel/Space, Escape, reject, window close and direct
  progress deletion. Editing and Apply recover under 500 ms while the worker's
  private gate remains blocked. Document bytes, state, generation, selection,
  assets, source region, capture defaults and sidecar intent remain unchanged.
- Retry, a new composed crop and a new annotation branch while old work remains
  blocked, followed by late success or exception. New state/status/eligibility
  survive and independent crop/history hooks exercise the production paths.
- Owner deletion at busy entry, progress cleanup and final refresh; parent reject
  and direct completion without joining; forced stale state, exact selection,
  native generation and cleanup-time selection changes; redacted failure/retry.
  Forced mutation fixtures briefly enable the canvas to inject changes that
  ordinary disabled controls refuse. Existing Core tests prove replaced Redo
  target identity even when current state and selection match.
- Two composed crops and exact Undo/Redo preserve Display P3, alpha, original
  source identity, unknown document/object fields, assets, selection and private
  source lineage. Annotation-only history starts no decoding worker.
- Actual capture pipeline Apply after successful Undo or cancelled history,
  deliberate encoder failure, retained receipt and explicit retry. Real temporary
  PNG/.xann files reopen with the expected original or cropped source pixels.
  Global cancellation and pipeline destruction while history is blocked publish
  no files. Capture pixels and private preference revision remain unchanged.

The existing capture annotation cases now inject their history seam independently
of crop preparation; their prior expectations remain intact. The first build
corrected two test field names to the actual document API. The first focused run
passed 30/31 behavior cases: its cleanup-selection observer used the capture as
its receiver and was intentionally disconnected with the dialog's own observers.
Using the test's independent guarded observer exercises the intended boundary.
No expected result or timeout was relaxed.

Synthetic progress QA shows restoring-history text, a progress bar and native
Cancel. Native name and button role are asserted. This is candidate offscreen
evidence, not physical keyboard/screen-reader/theme/helper acceptance.
Verification totals are indexed in
[CAPTURE-HISTORY-EVIDENCE.json](CAPTURE-HISTORY-EVIDENCE.json). The checkpoint
report verifies its exact pushed Ubuntu 24.04/Qt 6.4.2 CI revision.

## Remaining work

Next own Drawing Defaults and Appearance/Toolbar configuration dialogs and their
nested colour-picker/parent lifetimes, preserving revision checks, atomic writes
and publication to open editors. Large-image memory/performance, application
shutdown and physical progress/helper qualification remain Required.

All application/onboarding/activation/localization/notifications, capture topology,
recording/audio, destinations/uploads/credentials/plugins, workflows/settings,
tools/effects/canvas/output/utilities/history/automation/compatibility, packaging,
migration/updates/rollback and the supported Linux matrix remain in scope.
Security/privacy/accessibility/parity/human/release gates remain Required.
Ellipse, clipboard-assets, output-format, legacy-companion and shortcut decisions
remain pending. No completion percentage or production-readiness claim is made.
