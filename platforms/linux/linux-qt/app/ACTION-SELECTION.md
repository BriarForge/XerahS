# Native per-run capture action selection

Candidate implementation evidence for PCA-009/014/023 and CORE-004/005/007.
The full action catalogue, configured workflows, shared/native parity and human
security/privacy/accessibility/release qualification remain Required.

## Actual operation

**Capture region and choose actions…** shares one QAction in the tray, main
window and Capture menu. It uses the existing busy/modal admission checks and
the same portal, region selection, frozen-pixel composition and worker pipeline
as the default capture. Its action snapshot includes `after-capture-window`
before Save and Copy. No new activation wire command or persistent preference
is introduced. Default capture and tray click still use Save then Copy.

After composition, capture overlays close, the XerahS command window restores
behind the chooser, and an application-modal Qt chooser opens on the GUI thread.
The command window preserves maximized state and stays available for native
focus and clipboard ownership when its transient chooser closes. The chooser
shows image dimensions, per-run scope, retained-pixel behavior, keyboard
cancellation and all 19 remaining catalogue choices.
Annotation, Save image, Save image as new PNG, image/file/path clipboard and Show saved file are available; the other choices are
disabled with visible and accessible unavailability explanations. These are
unfinished Required actions, not accepted Degraded/Unavailable dispositions.

The user explicitly chooses available outputs, or no outputs. File consumers
require a successful Save or Save As; they do not implicitly enable either prerequisite. Empty selection keeps
the captured pixels in memory without saving or changing the clipboard.
Cancel, Escape, default Enter, window close, cancellation of the operation or
dialog destruction cancels the selector and later actions before mutation.
The normal retained-result controls can edit or discard the resulting pixels.
Their existing latest-receipt lifetime applies; no pixel persistence is implied.

No output directory, journal store or file is created while choosing. A
cancelled selector creates no durable metadata. Clipboard-only and empty runs
also create no journal unless another durable artifact already requires it.
The chosen actions use the existing PNG/clipboard authority, cancellation and
truthful-result paths. This window changes no workflow defaults.

## Product logic and native boundary

`src/actions/ActionSelection` uses the normative catalogue to enumerate and
validate drafts against executor-supplied availability. Empty selections are
valid. Duplicates normalize to catalogue order; entries at/before the selector
are ignored under PCA-023. Unknown or unavailable remaining IDs reject the
whole draft, with a value-free diagnostic, rather than silently omitting work.
The native dialog keeps an invalid draft open without executing actions.
The new validation diagnostic names remain native candidate codes pending
shared diagnostic/schema qualification.

`CapturePipeline` provides a cancellable condition-variable bridge. The worker
waits for a typed selection outcome without touching widgets or running a GUI
event loop. Its owning QObject creates and closes the asynchronous dialog.
Cancellation and owner destruction release the worker and fence unclaimed or
late selections; a closing dialog cannot later authorize Save or Copy.

The Core `runPipeline()` replaces its remaining plan for this run only. Its
selection hook gives the complete effective plan to the journal recorder before
recording the selector result or starting a chosen action. The subsequent save
intent therefore contains the successful selector and actual chosen actions.
Recovery never reconstructs removed default actions as failed or not started.
The persisted format and canonical result fields are unchanged.

Explicit native retry preserves a successful selector as provenance and retries
only eligible failed outputs. It opens no new chooser, reacquires no capture,
and changes neither the original result nor the captured settings.

## Verification and remaining work

- `PostCapturePipelineTest`: catalogue availability, empty/unknown/unavailable
  draft validation, normalization and effective-plan observation before later
  execution; published PCA vectors still use this production engine.
- `ActionSelectionTest`: actual modal Qt keyboard input, accessible disabled
  catalogue, Save/Copy/both/empty PNG and clipboard behavior, private journal
  effective-plan round trip, responsive cancellation, close/destruction,
  admission cancellation, transient-parent destruction, retained pixels and
  linked retry without repetition.
- `PortalScreenshotTest`: private-bus production acquisition and composition,
  overlay focus release, selector cancellation, actual chosen save, unchanged
  next default capture, restored owner focus/maximized state, busy admission
  and controller destruction.
- `PipelineJournalTest`: kill a real selected run before publication, reopen
  its private journal and observe the successful selector plus unknown save;
  the deselected clipboard action is absent from the durable plan.
- `ApplicationWindowTest`: actual shared QAction, menu/button keyboard input
  and busy availability.

Fixtures use temporary files/stores, private buses and offscreen Qt. They do
not change live Avalonia configuration or invoke real capture/upload commands.
The remaining catalogue and editor handoffs, prerequisite/artifact
retry and recovery, supported distro/compositor/physical focus and assistive
technology qualification, and all human gates remain Required. Native
[Save As](SAVE-AS.md) now uses the same chooser, engine, multiple-file result,
linked retry and journal. Native [annotation handoff](ANNOTATION-HANDOFF.md)
now returns accepted edited pixels before save and retains the document for
explicit compatible editor persistence. Capture-side paired persistence and
the complete annotation/action catalogue remain Required.

[Native file/path clipboard and reveal](FILE-OUTPUTS.md) now extend the chooser
and linked retry with original-save prerequisites and live file validation.
Uncertain reveal delivery interrupts the engine and retains pending journal
intent; it cannot authorize retry. Native identity is deliberately excluded
from persisted metadata. The linked evidence records verification and remaining
catalogue/shared/native/human scope. Save and Save As now retain separate
artifacts with Save-first consumer precedence; the linked Save As evidence
records exact native verification and remaining save variants.


## Native quick-task selection

Saved capture workflows offer Show quick task menu. It opens the same owned,
asynchronous action chooser before transformations or output. Choices apply only
to the current capture. Cancel, Escape, close and owner destruction cancel the
pipeline before output and retain the captured pixels. No workflow is saved by
this chooser.

The worker carries the actual selector ID into the native dialog. Catalogue
position validation excludes the current and earlier selectors, preventing
loops. Quick-task selection can include the later after-capture chooser; that
chooser starts with the effective choices accepted in the quick-task window.
Both selectors use the shared engine, canonical action results, journal plan
replacement and linked retry. A successful selector is preserved during output
retry without prompting again.

ActionSelectionTest exercises Save, Copy, both and no output for each selector,
keyboard cancellation, close, cancellation at admission, parent/dialog/controller
destruction, unavailable actions, retry and the two-selector chain. The chain
verifies that a deselected clipboard action stays deselected in the second
window and is absent from the durable plan. NativeCaptureWorkflowsTest creates,
saves, reloads and runs a quick-task workflow through the real workflow editor,
then verifies PNG pixels, journal results and unchanged persisted defaults.
CaptureWorkflowsTest verifies discovery and definition availability.

This is native candidate evidence for PCA-009/014/023. The complete quick-action
inventory, shared catalogue coverage, supported desktops, physical accessibility
and human acceptance remain Required.

Verification on 2026-11-02 from parent revision `73e1c57` plus this checkpoint:

```sh
cmake -S platforms/linux/linux-qt -B platforms/linux/linux-qt/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build platforms/linux/linux-qt/build --parallel 8
ctest --test-dir platforms/linux/linux-qt/build --output-on-failure --parallel 6
```

Result: `100% tests passed out of 124`. The focused action-selection,
capture-workflows and native-capture-workflows targets also passed separately.
Contract lint passed on a temporary snapshot of all tracked working-tree files;
the live checkout's nested local worktrees otherwise produce duplicate-rule
findings. Backlog and generated progress checks passed. Tests used the local
host toolchain and native offscreen/private-bus fixtures; these results do not
qualify other desktop targets or replace human acceptance.
