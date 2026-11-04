# MiniMax continuous slice queue (XerahS Linux, linux-qt)

You are MiniMax Code working on BriarForge/XerahS, the native Linux edition in `platforms/linux/linux-qt/`.
You run unattended for many hours. Work through the QUEUE below one slice at a time, in order.
Aoife (Claude Opus, lead) reviews and pushes your commits later. You never push.

## Where you work
- Workspace: `/home/mike/Work/BriarForge/XerahS/worktrees/minimax` (a detached-HEAD git worktree). Work ONLY here.
- Paths below are relative to `platforms/linux/linux-qt/` unless they start with `/`.
- Log file (create if missing): `/home/mike/Work/BriarForge/XerahS/worktrees/.minimax-state/queue-log.md`.

## Start of every run
1. Read the log. Find the first slice in the QUEUE whose ID is not listed in the log as `done` or `skipped`.
2. If `git status --short` shows uncommitted changes (ignore `platforms/linux/graphify-out/`), a previous run was
   interrupted mid-slice: read the log's last entry, finish that slice if the changes belong to it, otherwise discard
   them with `git checkout -- <file>` for each changed file (never `git stash`, never `git reset --hard`).
3. Do that slice completely (steps below), then continue with the next one. Keep going until you run out of time.

## Doing one slice
1. Read every file the slice names before editing. Read the existing test that the slice names as a model.
2. Make ONLY the change the slice describes, touching ONLY the files it allows. Match the surrounding code style.
3. Build and test from `platforms/linux/linux-qt`:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j4
   ctest --test-dir build -R '<the slice's tests>' --output-on-failure
   ```
   Then run the FULL suite once: `ctest --test-dir build -j4 --output-on-failure`.
   Tests named `editor-window-1/2/3`, `capture-annotation` and `upload-executor` can fail under parallel load: if one
   fails, rerun it alone (`ctest --test-dir build -R '^<name>$'`); a pass alone counts as a pass.
4. If everything passes: run `git checkout -- platforms/linux/graphify-out 2>/dev/null` (never commit graph files),
   check `git status --short` shows only files the slice allows, then commit with
   `git-nadia commit -m "<message>"` after `git add` of exactly those files. Message format:
   `[Linux] <one-line imperative summary>` + blank line + 1-3 lines saying what changed and which requirement
   (e.g. CORE-009, IFCAT-003, IFCAT-005) + blank line + `Written by MiniMax Code from the queue in tools/agent-orchestration/contracts/minimax-queue.md (slice <ID>).`
5. Append to the log:
   ```
   ## <slice ID> — done — <commit SHA> — <date time>
   - What changed (1-3 lines). Tests run and the result line.
   - Findings: <any product problem you noticed but did not fix, with file:line>, or "none".
   ```
6. If the slice cannot pass after two honest attempts, or it would need files outside its allowed list, or an existing
   test fails because of a real product problem: discard your changes for that slice (`git checkout -- <files>`, and
   delete any new files you created), log it as `## <slice ID> — skipped — <reason>` with the exact failing test and
   assertion, and move on to the next slice.

## Hard rules (never break these)
- Never push. Never run `git stash`, `git reset --hard`, `git rebase`, `git checkout <branch>`, `git branch`, or anything
  that changes HEAD other than `git-nadia commit`. Never commit with plain `git commit`.
- Never edit: `product-contract/`, `traceability/` JSON, `PROGRESS.md` files, `CONTRACT-AUDIT.md`, `README.md` files,
  `tools/`, other worktrees, or any file Codex is still working on:
  `app/AppContext.*`, `app/CaptureActions.*`, `app/EditorUpload.*`, `app/EditorWindow.*`, `app/FileUploadController.*`,
  `app/FileUploadResultDialog.*`, `app/ScheduledCapture.*`, `app/UploadDestinationsDialog.*`, `app/features/*`,
  `app/main.cpp`, `app/NativeNotifications.*`.
- Never weaken or delete an existing test assertion to make something pass. If an existing test must change because a
  slice changed visible text on purpose, update ONLY that assertion and say so in the log.
- No new dependencies, no new files except where a slice says so, no reformatting of unrelated lines, no graphify runs.
- If a slice's instructions don't match the code (a name differs, a constructor takes other arguments), adapt by
  content in the smallest way and write what you adapted in the log.

## Shared recipe T — make one window's text translatable (CORE-009)
Used by the T slices. Model: the existing slot `toolDialogsTextIsTranslatable()` in `tests/LocalizationExpansionTest.cpp`
and the translated code in `app/WatchFoldersDialog.cpp`.
1. If the class has `Q_OBJECT`, use `tr("...")` inside its member functions and lambdas that capture `this`. Everywhere else
   in the file (free functions, helpers, lambdas without `this`, classes without `Q_OBJECT`) use
   `QCoreApplication::translate("<ClassName>", "...")` and add `#include <QCoreApplication>` if missing.
2. Wrap every user-visible string literal: widget and button texts, labels, window titles, tooltips, accessible names and
   descriptions, placeholders, combo items, menu and action texts, message-box titles and texts, status and error
   sentences, file-dialog captions. Keep the exact source text (including `&` accelerators and `%1` placeholders); for
   `.arg(...)` wrap only the literal format string.
3. Do NOT wrap: diagnostic codes, object names, settings and JSON keys, file patterns like `(*.png)`, identifiers, unit
   suffixes like `" px"`, numbers, or text built from runtime data.
4. Test: add ONE private slot `<camelCaseWindowName>TextIsTranslatable()` to `tests/LocalizationExpansionTest.cpp`. Install
   `PseudoLocale`, construct the window the way an existing test constructs it (search `tests/` for the class name and copy
   the smallest setup), and assert every visible `QAbstractButton` and `QLabel` with non-empty text contains
   `QChar(0x2022)` (skip runtime-data widgets by objectName and comment why), with at least 3 checked, and the window
   title contains the marker if it has one. Remove the translator at the end. If the test needs libraries, add them to the
   `localization-expansion-test` lines in `CMakeLists.txt` (copy from the target of the test you copied the setup from).
5. Tests to run: `localization-expansion`, the window's own test target(s), `modal-dialogs`, `interface-states`,
   `native-surface-accessibility`.
Allowed files: the window's `.cpp` (and `.h` only if you must add an include), `tests/LocalizationExpansionTest.cpp`, the
`localization-expansion-test` lines in `CMakeLists.txt`.

## Shared recipe M — add a dialog to the modal focus/Escape test (IFCAT-005)
Model: `tests/ModalDialogsTest.cpp::modalDialogsTrapFocusOnlyWhileActiveIdentifyPurposeEscapeAndRestoreFocus` (rows 0..13).
Add one new row per dialog named in the slice, numbered after the current last row, constructed the way an existing
test constructs that dialog (helpers created before the `QDialog *dialog = ...` line so they outlive it). Keep existing rows
unchanged. If linking needs it, add the library or the dialog's `.cpp/.h` to the `modal-dialogs-test` lines in
`CMakeLists.txt`. If a new row fails because the DIALOG behaves wrongly (no Cancel/Close, destructive default button,
Escape does not close, focus not restored, empty title), that is a finding: remove that row, log it as a finding with
file:line, and keep the passing rows. Tests to run: `modal-dialogs`. Allowed files: `tests/ModalDialogsTest.cpp`,
the `modal-dialogs-test` lines in `CMakeLists.txt`.

## Shared recipe S — add a dialog to the visible-state test (IFCAT-003)
Model: `tests/InterfaceStatesTest.cpp` (`qrDialogStates()`, `hashCheckerStates()`, `watchFoldersDialogStates()`).
Add one private slot `<dialogName>States()` per dialog named in the slice. Prove each state the dialog really has
(initial/empty, loading, populated, validation, error, cancelled, denied, unavailable) as visible text, one line per
state with a trailing `// <state>` comment; for states it cannot have, add `// <state>: n/a because <reason>`. Use only
existing public API and `findChild<...>("objectName")`. Never trigger real network, portal or file-chooser calls. If a
dialog shows NO visible text for a state where it clearly should, that is a finding: assert only what is visible, and log
the finding with file:line. Tests to run: `interface-states`. Allowed files: `tests/InterfaceStatesTest.cpp`, the
`interface-states-test` lines in `CMakeLists.txt`.

## QUEUE (do in order)

### Translation (recipe T)
- T01 `app/AppearanceDialog.cpp` (AppearanceDialog)
- T02 `app/DrawingDefaultsDialog.cpp` (DrawingDefaultsDialog) — large; keep preference keys literal
- T03 `app/RectanglePropertiesDialog.cpp` (RectanglePropertiesDialog)
- T04 `app/TextContentDialog.cpp` (TextContentDialog)
- T05 `app/CanvasCropDialog.cpp`, `app/CanvasRotationDialog.cpp`, `app/SelectionRotationDialog.cpp` (three small dialogs; one test slot each)
- T06 `app/ConveniencePreferencesDialog.cpp` (ConveniencePreferencesDialog)
- T07 `app/AutoCaptureDialog.cpp` (AutoCaptureDialog)
- T08 `app/ActionSelectionDialog.cpp` (ActionSelectionDialog)
- T09 `app/CaptureSaveAsDialog.cpp`, `app/EditorSaveDialog.cpp`, `app/CapturePermissionHelp.cpp` (one test slot each)
- T10 `app/PipelineHistoryDialog.cpp`, `app/PipelineResultDialog.cpp` (one test slot each)
- T11 `app/WorkflowExecutionHistoryDialog.cpp` (WorkflowExecutionHistoryDialog)
- T12 `app/CaptureHistoryWindow.cpp` (CaptureHistoryWindow)
- T13 `app/OnboardingWizard.cpp`, `app/OnboardingProgress.cpp` (large; one slot for the wizard's first page and one for progress text)
- T14 `app/AnnotationColorDialog.cpp`, `app/AnnotationObjectNavigator.cpp` (one test slot each)
- T15 `app/PinWindow.cpp`, `app/RegionOverlay.cpp` (overlay instructions and pin tooltips; one slot each; the overlay may need the
  construction used in tests/PortalScreenshotTest.cpp — if that needs D-Bus, test only PinWindow and log it)
- T16 `app/CaptureAnnotationDialog.cpp` (CaptureAnnotationDialog) — large; keep task/diagnostic codes literal
- T17 `app/ApplicationWindow.cpp` (ApplicationWindow)

### Modal checks (recipe M)
- M01 AppearanceDialog, DrawingDefaultsDialog
- M02 RectanglePropertiesDialog, TextContentDialog
- M03 CanvasCropDialog, CanvasRotationDialog, SelectionRotationDialog
- M04 AutoCaptureDialog, ActionSelectionDialog, ConveniencePreferencesDialog
- M05 CaptureSaveAsDialog, EditorSaveDialog, PipelineResultDialog
- M06 WorkflowExecutionHistoryDialog, PipelineHistoryDialog

### Visible states (recipe S)
- S01 AppearanceDialog, DrawingDefaultsDialog
- S02 CaptureHistoryWindow
- S03 PipelineHistoryDialog, WorkflowExecutionHistoryDialog
- S04 AutoCaptureDialog, ActionSelectionDialog
- S05 OnboardingWizard (first page, finish-later, decline-everything states)
- S06 RectanglePropertiesDialog, TextContentDialog (invalid input keeps the dialog open with visible text)

When the whole queue is done, write `## QUEUE COMPLETE — <date time>` to the log and stop.

## Running this queue continuously (for Mike)
Run from a terminal; each pass works for up to 60 minutes, then the loop starts a fresh pass that resumes from the log:
```bash
cd /home/mike/Work/BriarForge/XerahS/worktrees/minimax
until grep -q 'QUEUE COMPLETE' ../.minimax-state/queue-log.md 2>/dev/null; do
  mcode exec --cwd "$PWD" --prompt-mode coding --permission smart --timeout 60m \
    -o ../.minimax-state/last.md --input - < ../claude/tools/agent-orchestration/contracts/minimax-queue.md \
    >> ../.minimax-state/run.log 2>&1
  sleep 60
done
```
Stop it any time with Ctrl+C; the next run resumes from the log. Before starting, make sure the worktree is clean and at
the current main: `git -C /home/mike/Work/BriarForge/XerahS/worktrees/minimax status --short` should be empty, then
`git -C /home/mike/Work/BriarForge/XerahS/worktrees/minimax fetch https://github.com/BriarForge/XerahS.git main && git -C /home/mike/Work/BriarForge/XerahS/worktrees/minimax checkout --detach FETCH_HEAD`.
