# POST-CAPTURE-ACTIONS-001 Post-capture actions

Version: 0.2.0

Status: Approved by the human product owner on 2026-08-31 (0.1.0) and 2026-09-28 (0.2.0 implementation-readiness clarifications); conformance required before activation

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
- **PCA-002:** Actions MUST execute in this order: selection windows; image
  effects; annotation; save variants; image clipboard; before-upload window;
  upload; OCR; OCR clipboard; QR scan; pin; print; custom actions; file
  clipboard; path clipboard; reveal; analyze; deletion; completion/history, as
  itemized in `actions.json`. An action not selected MUST NOT run. Completion
  and history recording is implicit and is not a selectable action.
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
  exists, the engine creates a managed temporary encoded file under PCA-019; it
  MUST record that artifact and apply the workflow's cleanup policy.
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
- **PCA-017:** `actions.json` is the normative action catalogue. It defines each
  stable action ID, the baseline `AfterCaptureTasks` flags it imports from, its
  kind, the artifacts it requires and produces, and whether it can be
  interrupted safely. Its array order is the execution order and refines
  PCA-002: both selection windows run first, save variants and the image
  clipboard follow the transformations, and the before-upload window runs
  immediately before upload. Selected actions MUST run in catalogue order
  regardless of the order in which they were selected.
- **PCA-018:** An action MUST start only when every artifact it requires is
  available. `saved-file` resolves to `save.file` when available and otherwise
  to `save-as.file`. When a required artifact is unavailable the action MUST be
  `skipped` with diagnostic `dependency-unavailable` and `missing_artifact` set
  to the canonical name of the first unavailable requirement in catalogue
  order (`save.file` for an unresolved `saved-file`). The result of an action
  that consumes `saved-file` or `temporary.file` MUST name the resolved artifact
  in `input_artifact`.
- **PCA-019:** Upload MUST use `saved-file` when it is available. Otherwise, when
  the workflow permits temporary upload files (the default), the engine MUST
  encode the working media to a managed `temporary.file`, record it as the
  upload's input artifact, and delete it under the workflow's cleanup policy.
  When temporary files are not permitted, upload MUST be skipped with
  `dependency-unavailable` and `missing_artifact` `save.file`.
- **PCA-020:** When the before-upload window is declined or cancelled, its
  result MUST be `cancelled`, upload MUST be `skipped` with diagnostic
  `upload-declined`, and every other action MUST continue. Declining upload is
  not pipeline cancellation.
- **PCA-021:** Delete MUST remove only a file this pipeline created, and only
  after every earlier selected action that requires `saved-file` succeeded.
  Otherwise delete MUST be `skipped` with diagnostic `delete-guard` so a failed
  upload or custom action never loses the user's only copy.
- **PCA-022:** The pipeline state MUST be `cancelled` when cancellation
  prevented at least one selected action from starting, `interrupted` while a
  restarted pipeline awaits recovery, and otherwise `completed`. The result
  MUST report `final_working_media` as the ID of the last transformation that
  succeeded, or `capture` when none did.
- **PCA-023:** A selection window that succeeds MAY return a replacement action
  set for the current run. Replacement entries at or before the selection
  window's catalogue position MUST be ignored and MUST NOT run, the remaining
  replacement set MUST run in catalogue order, and the recorded results MUST
  contain the selection window followed by those remaining actions only. Legacy flag sets MUST map to action IDs
  through `legacy_flags`, and the obsolete `AnnotateImage` flag MUST map to
  `annotation`.

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
including obsolete `AnnotateImage` as an alias of `AnnotateMedia`. This package
preserves the baseline action inventory and principal order while correcting
silent failure/skip recording, incomplete cancellation, and ambiguous recovery.

Baseline evidence: `src/platform/XerahS.Platform.Abstractions/TaskEnums.cs` and
`src/desktop/core/XerahS.Core/Tasks/Processors/CaptureJobProcessor.cs` at commit
`5c7e36dea77ab131fe0f5e2101e5d578ccde0306`. Ledger links are
`WORKFLOW-ACTIONS-001` and all `WF-AFTER-CAPTURE-*` rows.
