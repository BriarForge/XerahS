# POST-CAPTURE-ACTIONS-001 Post-capture actions

Version: 0.1.0

Status: Draft; product-owner approval required

## User intent

After a capture, users can run a chosen sequence of transformations,
interactive steps, persistence, clipboard, upload, recognition, presentation,
and navigation actions while retaining the capture and an honest result when an
individual action fails.

## Definitions and model

- **Working media**: the current capture payload passed between actions.
- **Artifact**: a saved file, annotation sidecar, clipboard representation,
  upload result, recognized text, QR result, pin, or other action output.
- **Action result**: action ID, lifecycle state, timestamps, artifacts,
  diagnostic code, and user-safe message.
- **Pipeline result**: ordered action results plus the final working media and
  overall state.

The normative lifecycle is in `STATE_MACHINE.md`. The selected action set and
its settings are snapshotted before execution; later settings edits affect only
future runs.

## Requirements

- **PCA-001:** The engine MUST record one action result for every selected
  action, in normative order, with state `succeeded`, `failed`, `cancelled`, or
  `skipped`.
- **PCA-002:** Actions MUST execute in this order: selection window; image
  effects; annotation; save; image clipboard; upload; OCR; OCR clipboard; QR
  scan; pin; print; custom actions; file clipboard; path clipboard; reveal;
  analyze; before-upload window; deletion; completion/history. An action not
  selected MUST NOT run.
- **PCA-003:** A transformation that succeeds MUST replace working media for all
  later actions. A failed transformation MUST leave the last valid working
  media available and MUST NOT dispose or corrupt it.
- **PCA-004:** Failure of one action MUST NOT prevent a later independent action
  from running. A dependent action MUST be `skipped` with diagnostic
  `dependency-unavailable`, naming the missing artifact.
- **PCA-005:** Clipboard, notification, reveal, pin, print, analyzer, QR, OCR,
  and history failures MUST NOT discard working media or a successfully saved
  file.
- **PCA-006:** Upload MUST use the saved file when available. When no saved file
  exists, the engine MAY create a managed temporary encoded file; it MUST record
  that artifact and apply the workflow's cleanup policy.
- **PCA-007:** Save success MUST record the absolute native path, media type,
  byte length, and filename-generation context. Save failure MUST leave working
  media available for clipboard and permitted upload fallbacks.
- **PCA-008:** Annotation MUST complete or cancel before save. A successful
  editor result MUST become working media; re-editable annotations MUST be
  offered to compatible persistence without making raster save failure
  invisible.
- **PCA-009:** The selection window MAY change the action set for the current
  run. Cancelling it MUST cancel the pipeline before mutations. Permanently
  changing a workflow option requires an explicit user choice.
- **PCA-010:** Cancellation requested between actions MUST prevent new actions
  from starting and mark remaining selected actions `cancelled`. An action that
  cannot be interrupted safely MAY finish, but its result MUST state that
  cancellation arrived while it was running.
- **PCA-011:** Retrying MUST target failed or dependency-skipped actions and
  their prerequisites without repeating successful non-idempotent actions by
  default. The retry result MUST link to the original pipeline result.
- **PCA-012:** The pipeline result MUST survive process restart once a durable
  artifact or network action exists. Recovery MUST distinguish `interrupted`
  from `failed` and MUST NOT claim an upload failed when its remote outcome is
  unknown.
- **PCA-013:** Secrets, clipboard contents, recognized text, and uploader
  credentials MUST NOT appear in ordinary diagnostics. User-safe errors MUST
  carry stable diagnostic codes.
- **PCA-014:** Quick actions MUST use the same action engine and result model as
  configured actions. UI shortcuts MUST NOT create an untracked alternate path.
- **PCA-015:** Concurrent pipelines MUST isolate working media, settings
  snapshots, cancellation, counters, temporary files, and results by task ID.
- **PCA-016:** Implementations MUST emit the canonical result shape illustrated
  by `test-vectors.json`. Platform-specific metadata MAY be added under a
  namespaced extension object.

## Supported action inventory

This contract governs the ordering and result semantics of all baseline
`AfterCaptureTasks` values: quick-task menu, after-capture window, image effects,
annotation, image/file/path/OCR clipboard actions, pin, print, save variants,
thumbnail save, custom actions, reveal, analyze, QR, OCR, before-upload window,
upload, and delete. Detailed behavior of each action belongs to its own
capability contract; until then its ledger row remains inventoried.

## Native adaptation, accessibility, privacy, and performance

Native selection, clipboard, print, reveal, and notification UI MAY differ.
The selected actions, order, artifacts, and result states MUST remain equivalent.
Interactive surfaces MUST support keyboard cancellation, screen readers, and
clear focus. Permission denial MUST be an action failure or dependency skip,
never a silent omission. Non-interactive dispatch overhead SHOULD remain under
20 ms excluding the action's own work.

## Compatibility and baseline disposition

Imported `AfterCaptureTasks` bit flags MUST map to the same user outcomes,
including obsolete `AnnotateImage` as an alias of `AnnotateMedia`. This draft
preserves the baseline action inventory and principal order while correcting
silent failure/skip recording, incomplete cancellation, and ambiguous recovery.

Baseline evidence: `src/platform/XerahS.Platform.Abstractions/TaskEnums.cs` and
`src/desktop/core/XerahS.Core/Tasks/Processors/CaptureJobProcessor.cs` at commit
`5c7e36dea77ab131fe0f5e2101e5d578ccde0306`. Ledger links are
`WORKFLOW-ACTIONS-001` and all `WF-AFTER-CAPTURE-*` rows.
