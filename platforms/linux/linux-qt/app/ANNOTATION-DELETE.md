# Guarded native annotation deletion

This is partial native evidence for EA-003/017/018/019/020, ES-004/005/006/007/
008/014/019/026/027 and ESET-011/016. Full Product Contract/native/human gates
remain Required. Shared schema, expected vectors, requirement scope/dispositions and
approvals have not changed. ES-014 is labelled partial because the baseline-alias
policy remains unresolved. The shortcut catalogue still awaits product-owner
approval. The command registry remains 52 commands with fourteen visible defaults.

## Production behavior

Editor Delete now freezes history state, exact UUID selection and document
generation before busy notifications. The canvas routes ordinary Delete and
Backspace through the same guarded Core operation with native pending/generation
and weak-owner checks. A focusable Delete button uses this path in capture
annotation. Main toolbar/menu/custom hotkeys retain the stable delete-selection
ID and native text/modal/busy precedence. Auto-repeat cannot add extra deletion
steps. Canvas deletion requires unmodified keys; the editor's distinct Shift+
Delete Clear Annotations command retains its existing route. Complete capture
Clear/shortcut surfaces and physical qualification remain Required.

The QtCore overload delegates admitted requests to the existing atomic deletion
algorithm. Single, multiple and compatible opaque objects can be deliberately
removed without interpreting their payload. Hidden or locked members protect the
whole group. Accepted deletion makes one history operation and clears selection;
Undo restores the complete prior document and exact selection, and Redo restores
the deletion. Source, remaining UUID/order/fields, embedded assets and creation
defaults stay unchanged. Reachable history keeps assets even after deleting all
objects. Empty/protected controls are unavailable, while stale state/selection/
generation requests report refusal and keep committed state, checkpoints and
Redo. Legacy Core/adapter deletion calls remain unchanged.

Native refreshes now check their weak owner after action, model, source, title
and status notifications. Capture history shares one guarded refresh sequence,
including regained Delete availability after Undo. A teardown regression found
Qt's status-bar hide/show traversal continuing after a messageChanged observer
deleted its owning editor. The coredump placed the failure in QStatusBar::hideOrShow
from EditorWindow::refresh; the deliberately destructive fixture identifies the
trigger. Native refresh now completes Qt's traversal with signals blocked and
publishes the changed message once afterward, before checking its weak owner.
This fixes the tested path on Qt 6.11.2 and 6.4.2 without changing system config.
Only temporary fixture data was involved; no real user image/settings were lost.
No extracted core copy was created. Full native lifecycle qualification remains
Required.

Actual editor/capture output uses the accepted deletion state. A concrete
[ES-014/030 clarification](EMPTY-SIDECAR-CLARIFICATION.md) records a legacy alias
that reintroduced deleted annotations after a formerly successful empty save.
Known aliases now cause truthful companion refusal with raster/document recovery,
preserving both files; archive/reconciliation policy remains pending. With remaining
annotations, real PNG/.xann reopen preserves source, fields and assets. With no
annotations, explicit save follows ES-014: remove the stale default companion,
keep the raster, and return no companion path. The live session/receipt still
retains its owned document/assets; this does not invent a garbage-collection or
empty-document interchange policy. Capture destruction before Apply publishes
no edited document, and subsequent Save retains original captured pixels.

## Indexed verification

- EditorSessionTest checks four independently declared single/mixed/opaque/all
  deletions, complete remaining payload/assets, exact one-step Undo/Redo and
  dirty/checkpoint/Redo behavior. Seven empty/protected/opaque/stale cases refuse
  without mutation. Four Core explicit-intent/default-present/absent cases preserve baseline files
  while refusing misleading completion. The published delete-and-undo vector
  remains unchanged.
- EditorCanvasTest checks actual Delete/Backspace input, repeat suppression and
  exact group history. Nine cases cover default independence, stale state/
  selection, matching-state replacement, disabled/reentrant dispatch and
  destruction during pending/rejection/completion notifications.
- EditorWindowTest checks five actual menu/default-key/custom-key/all cases,
  native list synchronization, empty-selection availability, exact history and
  real PNG/.xann save/reopen, including stale-companion removal with all objects
  deleted. Thirteen refusal/lifetime cases include protected/opaque groups,
  stale state/selection, matching-state replacement, busy teardown and owner
  destruction from Delete/Cut/model/title/status refresh notifications. Current
  files and private preference bytes remain unchanged on refusal/teardown. Three
  native empty-save alias cases (including late publication) report incomplete
  persistence and retain companion bytes and the zero-annotation document.
- CaptureAnnotationTest checks twelve native button/key/all/empty/protected/
  teardown cases. Space activates the visible focusable native Button with its
  accessible name. Exact Undo/Redo and regained availability keep local defaults.
  A fixture-populated native dialog uses the real annotation port and normal
  worker validation/publication. Independent red/green/blue pixels, editable
  reopen and original source/assets prove only accepted state reaches output.
  Pending/completion/Undo-control destruction retains original capture output.
  An opaque baseline collision preserves the alias, raster and zero-annotation
  receipt with failed companion/retry evidence.
  Private preference bytes/values/revision remain unchanged.
- NativeEditorToolbarTest checks actual text Delete/Backspace editing with zero
  command activation, protected/custom key routing, modal and busy precedence,
  recovery and hidden toolbar reachability.

Fixtures use temporary files/preferences/QSettings and private buses. No real
capture/upload/print or live Avalonia settings are touched. The inspected capture
frame is reproducible with XERAHS_DELETE_QA=/tmp/xerahs-delete.png on the capture
Delete button row under the existing offscreen/private-bus setup. Delete is
visible and unavailable after cleared selection; the remaining green object,
object list, numeric local default, companion disclosure and Apply/Cancel are
visible. Synthetic role/name/layout evidence does not replace physical AT,
high-contrast or desktop qualification. ANNOTATION-DELETE-EVIDENCE.json indexes
verification.

## Next work and full scope

[Guarded native keyboard transforms](ANNOTATION-KEYBOARD.md) now supply frozen
state/selection/generation admission, repeat/history and editor/capture
persistence/lifetime evidence. [Native capture Crop](CAPTURE-CROP.md) now supplies that owned preview/history/
source handoff and actual publication. Next migrate main-editor numeric/pointer
Crop to the owned preview with frozen admission and worker/close/owner guards;
continue remaining tools/fields and the full contract sequence.

All tools/fields, 232 effects, canvas/output, capture mapping/topology,
recording/audio, uploads, 436 workflows, 922 settings, application/history/
automation/compatibility, packaging/migration/updates/rollback, supported Linux
matrices, resource/performance, privacy/security/accessibility/parity and human/
release gates remain in scope and Required.
