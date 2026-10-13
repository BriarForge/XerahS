# Native capture annotation handoff

Candidate evidence for PCA-003/008/010/011/012/014/016, ES-002/009/011/012/020,
ESET-006 and CORE-004/005/007. Full annotation tools, capture persistence,
workflow configuration and native/human qualification remain Required.

## Actual operation

The shared per-run capture chooser now enables **Annotate image**. The same
Core engine orders it before Save, Save As and clipboard outputs. Default
capture remains Save then Copy. A worker prepares an owned untitled source,
including its bounded embedded PNG and private display copy, without reading
any earlier saved file. The GUI opens an asynchronous application-modal
annotation window after the controller releases its capture overlays and
restores the command window. No nested GUI loop is used by this handoff.

The window reuses the production `EditorCanvas`, `EditorSession`, viewport and
rectangle tools. Pointer creation/selection and keyboard move/resize/rotation/
ordering/deletion, Undo/Redo and zoom controls operate on that private session.
Native drawing preferences supply border/fill/thickness for future pipelines;
each pipeline copies its style before execution. Later preference changes do
not alter the active annotation snapshot. This is rectangle coverage, not
completion of all 20 annotation tools or the full preferences/workflow ledger.

Apply snapshots the re-editable document only after the active gesture ends.
The worker validates the document and exact embedded source binding, then
calls the shared authoritative Core renderer through `renderEditorImage()`.
Successful rendering replaces working media for every later action. Failure
leaves the previous media available. PNG saves and image clipboard therefore
receive the actual annotated pixels, rather than an unrelated open editor.
Stable diagnostics contain no document bytes, pixel payload or exception text.

Cancel/Escape/window close skips annotation only; later independent actions
receive the original media. Escape during a canvas gesture first cancels that
gesture. Cancel Capture closes the window, requests worker cancellation and
prevents later actions. Preparation/rendering check the token at safe boundaries;
owner destruction releases the worker without joining or allowing a late GUI
result to authorize output. The transient parent and completion state are
guarded through activation, dialog destruction and duplicate completion.

## Retention, compatible persistence and retry

The immutable native receipt retains the successful annotation document and
its unannotated embedded source separately from rendered working pixels. These
private bytes are not serialized into reports or journals. The existing latest
receipt lifetime applies: next completed capture, explicit discard or app close.
Restart pixel/document retention, opt-out and retention policy remain Required.

**Edit retained capture…** routes an annotated receipt to the real editor's
document opener. It validates and decodes the embedded source on a worker,
preserves annotation objects, clears durable path/hash bindings and opens an
unsaved document. It does not reread a saved PNG, flatten annotations into the
source, consume the receipt or change the original saved file. When the native
document opener is absent, this command stays unavailable rather than opening
only flattened pixels and silently losing editability.

The annotation window explicitly offers this recovery/persistence route. The
real editor's explicit Save chooses a destination and uses the existing
compatible raster/`.xann` service: atomic raster replacement, embedded original
source, true raster hash, annotations and sidecar acknowledgement. A failed
artifact keeps the editor dirty and visibly incomplete under ES-011/012.
Capture Save/Save As still publish PNG only. Automatic paired capture persistence
and companion artifact outcomes are the next integration slice, not claimed by
this explicit editable-copy offer.

Retry of failed eligible outputs reuses annotated pixels and the retained
document without opening annotation again. The Core planner admits acknowledged
annotation media; it rejects missing native documents and unsupported failed
transformations. The Core engine and write-ahead recorder inherit the actual
working-media ID without inventing another annotation execution or timestamp.
Journals retain successful annotation metadata before unknown pending save;
restart review never reconstructs pixels/documents or repeats the transformation.

## Verification and remaining work

- `CaptureAnnotationTest`: actual canvas input, incomplete-gesture rejection,
  frozen style, exact rendered PNG/clipboard/source pixels, explicit compatible
  persistence, picker/global cancellation, owner destruction, invalid source
  binding, failed-save retry and preserved document/media identity.
- `PortalScreenshotTest`: private-bus acquisition/composition, actual per-run
  annotation choice and canvas, transient ownership/overlay release, failed
  save and shared recovery routing to editable document without another capture
  or flattened fallback; borrowed image bytes and receipt remain unchanged.
- `EditorSourceTest` and `EditorWindowTest`: validated memory-only reopen,
  cancellation, source/dimension checks, unsaved real editor and actual explicit
  PNG/`.xann` persistence without compounding annotations. Dialog preferences
  and all output/preferences stores in new fixtures are temporary.
- `PipelineJournalTest`: kill a real annotated run before publication and recover
  its acknowledged transformation plus unknown save, with no invented retained
  pixels. Core tests cover inherited media identity without another result.

All 38 native/contract CTest targets include the unchanged 114 published vectors.
Affected native paths also run against isolated Qt 6.4.2 libraries; exact pushed
revision qualification uses Ubuntu 24.04/Qt 6.4.2 CI. Live Avalonia configuration
and real capture/upload commands remain untouched.

Next integrate explicit compatible sidecar intent into capture Save/Save As,
with separate true artifact facts and incomplete outcomes under ES-011/012/014/
030, preserving successful rasters and re-editable data on failure. Complete
annotation/effect/canvas/output tools, workflow/settings persistence, catalogue
retry, restart media authority, shared schema/vectors, colour/performance and
supported distro/compositor/physical accessibility/security/privacy/parity/
release reviews remain in the full Linux audit. No gate is self-approved.
