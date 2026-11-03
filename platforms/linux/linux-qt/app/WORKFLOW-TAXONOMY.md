# Workflow execution taxonomy and surfaces — candidate evidence

Resume verification: 2026-11-03, baseline a3b6b4814a58d41a54545570ccc3d5b8bf8b1e19.
All seven affected WORKFLOW-CATALOG-001 rows remain **partial / Required**.
No contract, dependency or product-owner approval is changed. This is Linux
candidate implementation evidence; it establishes no cross-platform parity.

## Behavior and per-row change ownership

Paths below are relative to `platforms/linux/linux-qt`. Shared files and tests
span several rows; do not split their dependent changes into an unbuildable commit.

| Row | User-visible behavior | Main changed files |
| --- | --- | --- |
| WFCAT-002 | Full-screen, monitor and active-window captures use the existing admitted execution path. Recording now admits to the same private store before acquisition, with a UUID, trusted origin, full frozen request snapshot, ordered control results and durable terminal lookup. Storage refusal prevents start. | `src/workflows/WorkflowExecution.{h,cpp}`, `src/recording/Recording.{h,cpp}`, `app/RecordingController.{h,cpp}`, `app/RecordingActions.{h,cpp}`, `app/features/RecordingFeature.cpp`, `tests/PortalScreenshotTest.cpp`, `tests/RecordingWorkflowTest.cpp`, `tests/WorkflowExecutionTest.cpp` |
| WFCAT-004 | Failure classes distinguish dependency, transient, permanent, permission and validation failures; partial, skipped and cancellation remain distinct. Journal v3 reads v1/v2 and execution v2 reads v1. Save/upload cannot complete before publication/response. | `src/actions/PostCapturePipeline.{h,cpp}`, `src/actions/PipelineResultJson.{h,cpp}`, `src/actions/PipelineJournal.cpp`, `app/CaptureActions.{h,cpp}`, `tests/PipelineJournalTest.cpp`, `tests/CaptureUploadTest.cpp`, `tests/PostCapturePipelineTest.cpp`, `tests/CapturePairedSaveTest.cpp` |
| WFCAT-008 | Prepared image/file/path/URL/OCR text MIME offers validate before replacement and carry privacy classification. Unsupported input leaves the clipboard intact. Unconfirmed ownership remains interrupted, never invented success. | `app/CaptureFileOutput.{h,cpp}`, `app/CaptureActions.{h,cpp}`, `tests/ClipboardWorkflowTest.cpp`, `tests/CaptureRecognitionTest.cpp`, `tests/CapturePipelineTest.cpp` |
| WFCAT-012 | Recording controls target a session UUID; stale IDs and invalid transitions fail. Repeated stop returns the same output without repeating effects. Encoder failure retains nonempty owner-only partial media. Abort discards it. | `src/recording/Recording.{h,cpp}`, `app/RecordingController.{h,cpp}`, `tests/RecordingTest.cpp`, `tests/RecordingWorkflowTest.cpp` |
| WFCAT-013 | Busy/modal control admission, guarded shortcut disabling and editor shutdown preserve user authority. Every registered dirty editor, including a hidden editor or modal capture task, uses its existing Save/Discard/Cancel policy. Nested dialogs/reentry block shutdown. Headless view/editor requests return actionable presentation-required. | `app/ApplicationControl.{h,cpp}`, `app/EditorShutdown.{h,cpp}`, `app/WorkflowShortcutActions.cpp`, `app/features/{Application,Preferences,Workflow,WorkflowExecutionHistory}Feature.cpp`, `tests/EditorWindowTest.cpp`, `tests/NativeInstanceTest.cpp`, `tests/WorkflowShortcutsTest.cpp` |
| WFCAT-016 | Saved capture profiles and an initial default exist; no separate workflow-preset import/export UI exists. Unknown actions return unsupported-action before admission. Compatible unknown fields round-trip. Legacy migration is explicit and only Save rewrites bytes. | `src/workflows/CaptureWorkflows.{h,cpp}`, `app/CaptureWorkflows.cpp`, `app/SAVED-WORKFLOWS.md`, `app/WORKFLOW-DISCOVERY.md`, `tests/CaptureWorkflowsTest.cpp`, `tests/NativeCaptureWorkflowsTest.cpp` |
| WFCAT-017 | One public observation supplies terminal state and opaque output references to capture log, progress, history and notification. Partial failure never claims completion. Private report details retain actual paths separately. | `src/actions/PipelineResultJson.{h,cpp}`, `src/actions/PipelineJournal.cpp`, `app/CapturePipeline.{h,cpp}`, `app/CaptureController.cpp`, `app/PipelineHistoryDialog.cpp`, `app/PipelineResultDialog.cpp`, `tests/PortalScreenshotTest.cpp`, `tests/PipelineJournalTest.cpp`, `tests/PipelineHistoryDialogTest.cpp` |

`CMakeLists.txt` registers recording-workflow and clipboard-workflow and links
production recording persistence and shared application-control admission.
`traceability/WORKFLOW-CATALOG-001.json` records named tests and remaining scope
for every row. These source relationships are **EXTRACTED** from the changed
production calls and tests; they are not inferred parity claims.

## Semantics and open scope

The brief explicitly resolves Abort as discard; recoverability applies to stop
and encoder failure without treating partial media as a finalized recording.
Unknown fields remain opaque data, never execution authority. No retry policy is
changed. Missing engines/destination are unavailable dependencies; network timeout
and 5xx are transient; 4xx and invalid destination configuration are permanent for
that attempt. Privacy and shutdown retain existing editor-copy and close-policy
behavior. No new product policy question was introduced.

Remaining scope includes complete action/entry-point families, separate preset
interchange, recording/other-family terminal-surface convergence, physical portal,
encoder and clipboard qualification, and human security/privacy review. In
particular the four-surface assertion and real network-status/durability cases
must be rerun outside the socket-restricted sandbox before approval.

## Verification

Built with the required Debug CMake/Ninja commands and `-j4`; dynamically linked
Qt 6.12.0 on this host. Minimum Qt 6.4.2 was not available for this run.
Focused CTest: **100% tests passed out of 9** (post-capture-pipeline,
capture-workflows, workflow-execution, recording, recording-workflow,
recording-integration, clipboard-workflow, editor-exit, editor-task).

Additional executables were run directly with offscreen Qt, an empty platform
theme and an explicitly nonexistent D-Bus address, without the private-bus launcher:

- PipelineJournalTest: **33 passed, 0 failed**.
- NativeCaptureWorkflowsTest: **29 passed, 0 failed**.
- CapturePipelineTest: **19 passed, 0 failed**.
- CaptureRecognitionTest: **9 passed, 0 failed, 1 skipped** (both recognition engines
  are installed, so missingEngineHasAnExplicitFailure skips).
- EditorWindowTest::applicationQuitUsesDirtyEditorClosePolicyAndBlocksModalWork:
  **3 passed, 0 failed**, including init/cleanup.

These direct runs supplement CTest; they do not make a private-bus CTest pass.
Unverified slice-specific cases include:

- PortalScreenshotTest::wholeSourceWorkflowSavesWithoutAnOverlay (full-screen, monitor).
- PortalScreenshotTest::activeWindowCropsToTheWindowOrFails.
- PortalScreenshotTest::partialFailureAllFourTerminalSurfaces.
- CaptureUploadTest::transfer (success, HTTP failures and timeout).
- CaptureUploadTest::uploadCannotSucceedBeforeRemoteResponseCompletes.
- WorkflowShortcutsTest::sharedNativeActionsEnableDisableInvalidateAndRespectBusy.

The sandbox prohibits Unix/TCP socket binding. The original full-suite run exposed
an editor-task shutdown regression; it was corrected and the editor-task target
passes. The original direct capture-pipeline run exposed an unsafe test access to
an intentionally empty interrupted result; the test now asserts uncertainty and
all 19 cases pass. Final full-suite results and exact blocked CTest target names
follow below.

## Git handoff

The supplied checkout is detached, as directed by the implementer brief; no
branch was created or switched and no push was attempted. Git staging fails:
`Unable to create '/home/mike/Work/BriarForge/XerahS/.git/worktrees/codex/index.lock': Read-only file system`.
This sandbox cannot stage or commit into the linked worktree metadata. Changes
remain reviewable in the working tree; Aoife must stage and commit outside this
sandbox, using the row ownership table above and retaining dependent shared edits.

```text
69% tests passed, 44 tests failed out of 141

Total Test time (real) =  13.50 sec

The following tests FAILED:
	  2 - editor-print (Failed)
	  3 - editor-copy (Failed)
	  4 - editor-save (Failed)
	  6 - pipeline-journal (Failed)
	  7 - capture-history-window (Failed)
	  8 - pipeline-history-dialog (Failed)
	  9 - capture-pipeline (Failed)
	 10 - action-selection (Failed)
	 11 - capture-file-output (Failed)
	 12 - capture-save-as (Failed)
	 13 - capture-annotation (Failed)
	 14 - capture-history (Failed)
	 15 - capture-paired-save (Failed)
	 22 - native-instance (Failed)
	 28 - portal-screenshot (Failed)
	 29 - native-desktop-appearance (Failed)
	 31 - native-editor-preferences (Failed)
	 43 - native-window-discovery (Failed)
	 52 - annotation-object-navigator (Failed)
	 58 - editor-window-1 (Failed)
	 59 - editor-window-2 (Failed)
	 60 - editor-window-3 (Failed)
	 61 - editor-pin (Failed)
	 62 - editor-wallpaper (Failed)
	 63 - editor-upload (Failed)
	 64 - native-preference-dialogs (Failed)
	 66 - url-download (Failed)
	 67 - editor-url-open (Failed)
	 68 - editor-open (Failed)
	 70 - native-capture-workflows (Failed)
	 71 - workflow-shortcuts (Failed)
	 80 - secret-service-credential-store (Failed)
	 86 - upload-executor (Failed)
	 87 - capture-upload (Failed)
	 88 - capture-pin-print (Failed)
	 89 - portal-color-picker (Failed)
	 92 - file-upload (Failed)
	 96 - watch-folder-service (Failed)
	122 - effects-dialog (Failed)
	124 - onboarding (Failed)
	127 - scroll-capture (Failed)
	134 - capture-recognition (Failed)
	135 - capture-before-upload (Failed)
	137 - capture-delete (Failed)
```

Conformance, contract lint and generated-progress check pass. The final suite
reports 97 passing targets and 44 socket/D-Bus-dependent failures; it is not a
full-suite pass. Tests named editor-window-* were also attempted alone and failed
at private-bus startup before test execution.

## Graph refresh limitation

Foreground `graphify update .` refreshed the code graph (14,287 nodes, 36,280
edges). Its parser reports partial extraction for 100 files and no symbols for
8 code files; source and tests remain the correctness evidence. The final
four-document semantic extraction was attempted with full line coverage, but
Graphify's preservation guard rejected the merge because prior semantic IDs
would disappear. The guard was kept enabled and the last valid code graph was
retained. Semantic refresh of CMakeLists.txt, SAVED-WORKFLOWS.md,
WORKFLOW-DISCOVERY.md and this file remains pending; `.needs_update` records it.
