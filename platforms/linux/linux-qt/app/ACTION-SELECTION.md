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
Save image, image/file/path clipboard and Show saved file are available; the other choices are
disabled with visible and accessible unavailability explanations. These are
unfinished Required actions, not accepted Degraded/Unavailable dispositions.

The user explicitly chooses available outputs, or no outputs. File consumers
require a successful Save; they do not implicitly enable that prerequisite. Empty selection keeps
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
Complete quick-task and configured selection, explicit persistent workflow
editing, the remaining catalogue and editor handoffs, prerequisite/artifact
retry and recovery, supported distro/compositor/physical focus and assistive
technology qualification, and all human gates remain Required. Next independent
catalogue work is native Save As with multiple-artifact tracking and matching
retry/journal coverage.

[Native file/path clipboard and reveal](FILE-OUTPUTS.md) now extend the chooser
and linked retry with original-save prerequisites and live file validation.
Uncertain reveal delivery interrupts the engine and retains pending journal
intent; it cannot authorize retry. Native identity is deliberately excluded
from persisted metadata. The linked evidence records verification and remaining
multiple-artifact/catalogue/shared/native/human scope.
