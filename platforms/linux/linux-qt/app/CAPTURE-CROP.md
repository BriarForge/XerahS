# Native capture Crop and verified source handoff

This is partial native candidate evidence for EC-007/017, EA-003/013/017/018/
019/020, session source/history and post-capture annotation/persistence. All full
requirements, supported targets and human/release gates remain Required. Shared
schemas, vectors, expected results, dispositions and approvals are unchanged.

## Production behavior

Capture annotation offers a focusable Crop tool and Crop Image button. Reverse
pointer drags and numeric bounds enter the same owned native preview. Left, Top,
Width and Height use labelled native spin controls and keyboard mnemonics; a
rendered preview, named progress, Apply Crop and Cancel expose the retained area.
Changing bounds cancels obsolete preparation. Apply admits only the current
completed preview; cancellation or worker failure keeps the committed canvas.

The QtCore crop-region helper factors the existing canvas rules: reject nonfinite
or zero-area input, normalize reverse drags, clip to the source, floor lower and
ceil upper pixel edges. Existing canvas geometry/rendering is unchanged. Known
annotations translate with the retained rectangle; wholly outside objects are
removed and intersecting objects retain geometry, clipped at export under the
existing rotated-intersection policy. Unknown geometry refuses the complete
crop rather than guessing a transform. Full tool/field/protection parity remains
Required; this does not introduce a new unknown-object model or golden.

Native admission freezes history state, exact selection, document generation and
owned source/document before pending notifications. Busy controls cannot apply
capture, repeat Crop, edit or navigate history during preview. Matching-state
replacement cannot retarget acceptance. A successful crop commits one complete
history operation, retains source color/alpha, UUID/order/unknown fields, embedded
assets and local creation defaults, and filters only removed selection IDs.
Full-canvas no-op and Cancel preserve history and Redo.

QtCore history stores a private retained-source region alongside each state.
Ordinary annotation edits retain it; crop composes it; Undo/Redo restores it.
An unrelated source transformation invalidates that lineage. It is never written
into shared document JSON, companions, preferences or public pipeline reports.
The native capture choice carries the declared region internally. On its worker,
capture independently reproduces that region from the original normalized source
through the same Core/codec crop path, then compares the complete source PNG and
dimensions before rendering any returned annotations. Missing proof retains the
old strict unchanged-source/dimensions checks. Invalid, overflowing, wrong-location,
tampered or size-mismatched proof returns document-invalid and leaves working
media unchanged. A declared region alone cannot authorize arbitrary source pixels.

Source-changing Undo/Redo prepares a validated source and private sRGB display
on an owned cancellable worker before changing history. Failure, cancellation,
stale state/selection/generation or owner destruction keeps current history/source.
Accepted restoration updates dimensions, viewport and native action/list state.
Preview/history workers capture immutable values and shared cancellation state,
with no GUI pointers. Closing/destroying their owner cancels and releases the UI
without waiting for a deliberately blocked fixture worker. Full memory/performance,
codec/shutdown and physical qualification remain Required.

The outer capture Apply publishes only a completed accepted document. Capture
Cancel or global cancellation/destruction retains the original capture or stops
later actions as appropriate. Actual PNG/.xann saves reopen the cropped editable
source and retained objects/assets. Empty-annotation saves keep existing ES-014
semantics; ES-014/030 legacy-companion reconciliation remains pending. Failed-save
retry reuses acknowledged cropped pixels/document without another annotation or
crop. Companion disclosure names the current editable source and embedded assets;
earlier canvas states remain in Undo while the dialog is open. No claim is made
that cropping removes pixels embedded in independent retained assets.

## Indexed verification

- `CanvasOperationsTest::cropRegionsNormalizeAndRejectUnsafeBounds` checks
  reverse/fractional/clipped bounds, nonfinite/zero/outside input and overflow.
  `croppedSourceLineageFollowsExactHistoryAndRejectsStaleAdmission` checks two
  composed crops, intervening annotation edit/undo, exact history/source/selection,
  stale/invalid admission, Redo truncation and unrelated-source invalidation.
  Existing crop/intersection, resource, codec/color and canvas vectors remain.
- `CaptureAnnotationTest::nativeCropSharesPointerNumericHistoryAndVerifiedCapturePublication`
  drives the actual production coordinator and native dialog across numeric,
  reverse pointer, repeated crop, Undo, inner Cancel, outer Cancel, matching-state
  replacement and no-op. Real text entry/Tab/Space, native Button/SpinBox roles
  and names, busy Apply refusal, exact document/selection/viewport history,
  alpha/source pixels, local defaults and actual PNG/.xann reopen are checked.
- `nativeCropRetainsTranslatedObjectsAssetsAndTruthfulUnknownRefusal` declares
  independent retained/removed/partially intersecting objects, complete fields
  and assets. Independent red/green/source pixel samples, all-outside empty save,
  unknown refusal and Cancel use the actual rendering/publication worker.
- `captureWorkerIndependentlyVerifiesDeclaredCropSource` verifies valid, repeated
  and identity source proofs, and missing/wrong-location/tampered/outside/overflow/
  size-mismatch refusal with original-media fallback and value-free diagnostics.
- `cropAndSourceHistoryWorkersRespectCancellationReplacementAndOwner` checks
  preview and history cancellation/failure, matching-source replacement,
  pending/completion owner destruction and preview-only destruction. Deliberately
  blocked worker fixtures verify UI release without joining the worker.
  `globalCancelAndPipelineDestructionReleasePendingCropWorkers` checks the actual
  coordinator with no later file publication; `croppedCaptureRetryUsesRetainedPixelsWithoutAnotherAnnotation`
  proves failed-save recovery with one annotation execution and actual reopen.

The new preview-destruction case initially left the canvas disabled because Qt
cleared its guarded pointer at a different destruction boundary. Matching either
the known child identity or cleared pointer restores controls. The coordinator
destruction case then exposed SIGSEGV in QWidget::setEnabled: a child recovery
callback ran after the parent had explicitly deleted its canvas. Parent teardown
now marks closing and disconnects recovery before deleting the canvas; closing
operations cannot restore or re-accept. Coredump/QtTest stacks identify that path,
including the deliberately blocked owned worker. Memory was available; no OOM
mechanism is inferred. No extracted core copy or system changes were made.

The initial suite timeout included those recovery failures and a fixture sending
Redo before native focus returned from asynchronous history progress. Explicit
active-window/focus checks retain actual-key assertions. The replacement fixture
now matches the capture's 3 px creation defaults, so it tests generation refusal
without introducing a separate default change. No shared expected results were
changed. The adjacent candidate evidence records final verification and these
findings, without accepting full native/human gates.

Fixtures use synthetic pixels, temporary files/preferences/QSettings and private
buses. No real capture/upload/print or live Avalonia configuration is touched.
The final capture frame is reproducible with `XERAHS_CROP_QA=/tmp/xerahs-crop.png`
on the native crop `numeric` row; `XERAHS_CROP_PREVIEW_QA=/tmp/xerahs-crop-preview.png`
captures its owned preview before Apply. Synthetic visual inspection does not replace
physical keyboard, assistive technology, high-contrast or target qualification.

## Next work and full scope

Move the main editor's numeric and pointer Crop routes to the owned preview,
freezing state/selection/generation and containing worker/close/owner lifetime
while preserving the existing Core crop policy and real Save/reopen behavior.
The older main-editor numeric dialog and general canvas worker remain separate.

Ellipse, shared clipboard/assets, output-format, legacy-companion and shortcut
decisions remain pending. All tools/fields, 232 effects, canvas/output, capture
mapping/topology, recording/audio, uploads, 436 workflows, 922 settings,
application/history/automation/compatibility, packaging/migration/updates/rollback,
supported Linux matrices, resource/performance, privacy/security/accessibility/
parity and human/release gates remain in scope.
