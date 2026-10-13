# Linux implementation plan

Implement the approved English Product Contract in the `linux-qt` reference
edition. Keep shared expected results independent of Linux, and record partial
requirements honestly until native behavior and evidence cover their full scope.

The next editor work follows these dependencies:

1. Complete arbitrary-angle rectangle export under ES-025 using exact polygon
   coverage. Expose native selection rotation, preserve atomic session history,
   and make selection and hit testing follow the rotated object. Verify known
   analytical coverage and real PNG/sidecar persistence.
2. Implement viewport zoom, fit, reset, gestures, and pan under EC-005/EC-006.
   Keep viewport state outside the document and history; convert all pointer
   input into document pixels and keep hit targets usable at every zoom. Verify
   pointer anchoring, finite scale limits, history isolation, and native input.
3. Atomic crop, auto crop, basic pixel resize, canvas resize, custom rotation,
   quarter turns and flips now share production geometry, PNG preparation and
   history with all three canvas
   vectors passing. Auto crop has explicit border/alpha/colour rules; custom
   rotation has a cancellable native preview. Continue with general affine
   annotations, then insert and background composition. Native image comparison
   now accepts two raster files or a rendered editor snapshot with a file,
   exposes accessible divider controls, and keeps source/session state immutable.
   Flatten of represented annotations and distinct clear commands preserve complete
   history; flattening separate background layers follows their implementation.
   New Image now validates dimensions, alpha/colour, selected profile and optional
   file/clipboard initial pixels on a cancellable worker, confirms dirty session
   replacement, and opens an untitled source that stays unsaved until persistence.
   Native Open now validates raster and sidecar snapshots on a cancellable
   worker before the same dirty replacement confirmation. Saving over the
   candidate reloads the explicit save before adoption; failed or cancelled
   opening preserves the current session. Recent-file selection now offers
   persisted opt-in, bounded platform-aware paths, removal/clearing and
   atomic Apply/Cancel controls in the editor and tray. A versioned private
   store atomically retains current and privacy-pruned previous settings,
   preserves compatible unknown fields and publishes only successful writes.
   Native tests cover restart, cancellation, stale drafts, write failure and
   successful/incomplete image saves. Border/fill colours and thickness now
   persist canonically with alpha, apply only to future creations across native
   editor windows, and survive session replacement and restart. Native drawing
   controls support atomic Apply/Cancel, reset, invalid/stale draft rejection and
   failure reporting; explicit toolbar edits retain their document history if
   preference saving fails. Window size/maximized state now restore through a
   logical-pixel display model, recover on native screen/work-area/DPI changes,
   and save only on accepted close. A shared memory toggle atomically excludes
   window state from both versions and stops automatic persistence; native
   tests cover cancellation, failure, minimized/maximized state, injected display
   changes, actual editor frame recovery and preserved document/redo state.
   Theme/system-theme and accent/system-accent now resolve over a read-only
   desktop Settings observer and native Qt palette. Atomic native Apply/Cancel,
   reset and alpha-aware colour picking update all editor windows while preserving
   image state and history. Desktop contrast and reduced motion are independent
   of preferences; private-bus tests cover wire types, live changes, stale
   snapshots, missing services and owner restart. Physical style/focus and
   assistive-technology qualification remain Required.
   Native toolbar customization now reorders, shows/hides and groups all 37
   current commands, validates additional hotkeys and applies or cancels one
   atomic draft. Fixed menus and protected shortcuts remain reachable with an
   empty toolbar. Native input tests cover text/modal/busy precedence, disabled
   commands, live cell editing, multiple windows, restart and preserved image
   files/checkpoints/redo. Complete command coverage, shared metadata and
   physical keyboard/accessibility qualification remain Required.
   Continue complete settings metadata and
   downstream preferences, independent whole-file recovery and review gates
   under EDITOR-SETTINGS-001, then general affine annotations, inserted images
   and background composition.
4. Expand annotation tools, effects, output actions, settings and utilities,
   alongside the remaining capture and post-capture integrations.

The [full contract scope audit](CONTRACT-AUDIT.md) also tracks capture,
recording, uploads, workflow/catalogue integration, native application surfaces,
platform services, compatibility and release qualification. Native screenshot
transport now binds replies to the requested owner/handle, distinguishes
cancel from permission denial, closes pending/late legacy requests and decodes
borrowed images on a bounded worker. A native Cancel Capture command reaches
the pending portal, overlay and confirming-composition phases; private-bus production tests cover
failure/cancellation and no output/clipboard side effects. Human permission
and privacy review remain Required, as do per-monitor sources, capture settings,
last-region persistence and complete overlay behavior. Native save/clipboard
pipelines now execute the Core engine on a worker, publish private PNG files
without replacing existing destinations, expose activity and cancellation,
retain true results/artifacts/media and dispatch the clipboard on the GUI.
Shared tray/main-window recovery controls now open retained pixels as an owned
unsaved editor source, explicitly discard only the confirmed current receipt,
and review a read-only action/result snapshot. Canonical result serialization now
lives in the production Qt Core library shared by the adapter and native receipt;
actual file facts, task identity and execution times use a namespaced extension.
The real save/image-clipboard path now adds a Qt Core write-ahead recorder,
private atomic journal and native multi-record restart review/confirmed forget
surface. Real-process kill/reopen and native/storage fixtures distinguish
unknown outcomes without repeating actions, preserve true successful files and
leave unsaved pixels unpersisted. Explicit native retry now confirms one attempt at known failed save/clipboard
operations, preserves earlier successes and original settings/pixels, and
records new original/previous task links and actual file creators. Core/native
UI/I/O and real-process kill/reopen tests cover cancellation, stale approval,
owner destruction, storage failure and unknown outcomes. Continue full
catalogue/prerequisite retry and artifact/media recovery, configured
retention/pagination, crash-temporary authority,
shared persisted compatibility, the remaining catalogue and native/human
qualification under app/JOURNAL-RECOVERY.md. Continue those native capture requirements alongside the full
application domains recorded in the audit. Native per-run action selection now
shares the capture/controller/worker engine, with an asynchronous cancellable
Qt chooser, explicit Save/Copy/empty choices and visible unavailable catalogue
actions. Default workflows remain unchanged; cancellation creates no outputs
or journal, and the effective selected plan precedes save intent in recovery.
Native/private-bus keyboard and real-process kill/reopen fixtures are indexed
in [action selection evidence](app/ACTION-SELECTION.md). Native saved-file clipboard/path/reveal now uses
live in-memory publication binding, worker/GUI validation, asynchronous bounded
ShowItems, truthful interruption on unknown delivery and linked original-save
prerequisite retry. [File-output evidence](app/FILE-OUTPUTS.md) records actual
verification and path-race/native/human limits. Native Save As now opens an
asynchronous Qt new-PNG picker, publishes on the worker without replacement,
and retains both file aliases with Save-first precedence. Picker cancellation
skips only that action; global cancellation stops later actions. Explicit retry
keeps known successes/creator facts and asks again only for failed Save As;
write-ahead recovery preserves earlier files and unknown pending publication.
[Save As evidence](app/SAVE-AS.md) indexes actual picker/private-bus/process-kill
tests and concrete remaining replacement/format/sidecar/shared-schema decisions.
Native [annotation handoff](app/ANNOTATION-HANDOFF.md) now runs an asynchronous
rectangle canvas before save, renders accepted edits on the worker and retains
the source document for explicit compatible persistence through the real editor.
Cancellation preserves original pixels; output retry retains annotated media
and its identity without repeating annotation. Actual native/private-bus and
process-kill fixtures cover this partial PCA-008 surface. Shared canvas/source
libraries prevent capture and editor paths from diverging. Next integrate
explicit sidecar intent and truthful companion-artifact outcomes into capture
Save/Save As under ES-011/012/014/030, alongside complete annotation tools,
configured workflows, remaining save variants and catalogue.

Each checkpoint updates requirement traceability, runs native tests and the
implemented shared vectors, and lands on `main`. Passing the small pilot vector
set does not establish full capability completion. Linux edition qualification,
shared goldens, accessibility and release reviews remain required as described
in `product-contract/READINESS.md`.
