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
libraries prevent capture and editor paths from diverging. [Paired capture persistence](app/PAIRED-SAVE.md)
now freezes explicit sidecar intent, publishes compatible companions and reports
each artifact separately. Partial save keeps the PNG/document for companion-only
repair, admits acknowledged PNG consumers without delete authority, and records
known PNG facts before unknown companion intent. Native/private-bus and real-process
fixtures verify both Save aliases, actual re-editing, unchanged bytes/creators,
cancellation, private-store failure and honest normal/restart review.
[Editor Save](app/EDITOR-SAVE.md) now freezes an owned snapshot on a cancellable
worker, publishes through descriptor-bound same-directory temporaries, acknowledges
the exported history state and keeps split saves dirty. Explicit default-Cancel
companion repair preserves the PNG and Redo; failed Save As keeps the current path
until full repair. Native tests cover actual cancellation, concurrent caller changes,
old-destination/future-companion preservation, bounded hashing, owner destruction,
completion exceptions and refused non-PNG destinations. Full path-race, filesystem,
shutdown, physical and human qualification remain Required. Explicit
PNG/JPEG/WebP alpha/quality/extension/overwrite/profile choices remain Required
under EO-004/005/006; resolve the format-policy clarification recorded in
EDITOR-SAVE.md while continuing decided independent work,
alongside complete annotation tools, configured workflows, remaining save variants
and catalogue. [Native editor Copy Image](app/EDITOR-COPY.md) now implements
independent EO-001/002/014/015 behavior while the format clarification remains
pending: owned worker composition, bounded alpha-preserving PNG plus native
image offers, GUI-only acknowledgement, explicit unknown delivery, unchanged
dirty/history/files and default-Cancel known-failure replay. Fixed Ctrl+C and
customized hotkeys share native text/modal/busy precedence. Save and Copy now
share one guarded busy/lifetime scope with [native Print Image](app/EDITOR-PRINT.md).
Print now adds immutable source/annotation/white-paper preparation, explicit
page/margins/scale/orientation/colour preview and native printer confirmation,
worker submission, truthful submitted/unknown outcomes and confirmed known-failure
replay under EO-001/008/014/015/016. Source/history/Redo/files/preferences/clipboard
remain unchanged. Physical printer/permission/queue/cancellation/colour/shutdown
and full supported-target/human qualification remain Required. The subsequent
[annotation transfer checkpoint](app/ANNOTATION-TRANSFER.md) adds
represented-rectangle duplication and native object Cut/Copy/Paste under
EA-003/016/017/018/019/020, bounded complete-batch validation, fresh identities,
exact history/selection and protected direct edits, with source/assets/files/defaults
kept and no Cut after unknown delivery. Shared transport/asset-reference/offset/lock
policy EA-016-LINUX-2026-10-14 remains pending; asset-bearing clipboard copies
are refused without a lossy fallback. [Native annotation modes](app/ANNOTATION-TOOLS.md)
now add checked Select/Rectangle/Crop controls, shared capture guidance, live-gesture
cancellation, finite complete-batch geometry refusal, rounded no-op protection and
actual pre-command selection recovery. The shared schema defines only Rectangle
fields and ES-025 only Rectangle pixels: Ellipse remains an inert preserved unknown
type pending EA-005-LINUX-2026-10-14. [Rectangle edits](app/RECTANGLE-EDIT.md) now
add eight native resize handles with fixed opposite points at rotation, copied
previews and one validated commit, plus shared keyboard-accessible numeric/color/alpha
properties. Creation defaults/source/assets/unknown fields remain intact through
actual editor save/reopen and capture publication; invalid/stale/cancel/owner
failures preserve this edit's state. Initial layout now fits the actual viewport
while later resizes preserve user zoom. [Keyboard Rectangle creation](app/RECTANGLE-CREATION.md)
now owns numeric bounds and frozen defaults/state/selection in editor and capture.
Pointer creation shares that guarded draft path. Actual Tab/Alt+R/F9/capture
Tab-Space, cancel/stale/lifetime and save/reopen/publication fixtures preserve the
active tool, defaults and source. [Native object selection](app/ANNOTATION-OBJECTS.md)
now provides a shared UUID-based keyboard list, non-color-only protection/selection
labels, pointer/history/source synchronization and guarded stale/busy/lifetime
refusal. [Frozen group movement](app/ANNOTATION-MOVE.md) now copies rectangle
geometry, exact selection, history state and document generation at press;
previews share Core translation/finite validation and release commits one guarded
group edit. Stale/cancel/no-op/protected/overflow failures retain committed state,
Redo and checkpoints. Native editor save/reopen and capture publication preserve
source, IDs, styles, assets and compatible unknown fields. [Guarded group rotation](app/ANNOTATION-ROTATION.md)
now owns a native per-object angle preview in the editor and capture, shares Core
finite/normalized rotation validation, and commits against frozen history,
selection and document generation. Cancel/invalid/stale/no-op/protected/owner
failure preserves committed state; actual editor/capture output and exact
history retain source, fields and defaults. [Guarded stroke/fill choices](app/ANNOTATION-COLOR.md)
now share an owned alpha picker, freeze document/selection/defaults/generation and
editor preference revision, preserve one-step history, and keep capture defaults
local. Preference-save failure retains the accepted undoable document edit with
an explicit diagnostic; actual save/reopen and independent alpha-composited capture
publication are verified. [Numeric Rectangle drafts](app/RECTANGLE-DRAFTS.md) now
guard document generation, including replacement documents with matching state
and IDs, and share the owned nested alpha picker with explicit draft-only scope.
Refusal and nested destruction preserve history/files; fresh editor save/reopen
and original capture publication recover safely. [Guarded thickness](app/ANNOTATION-THICKNESS.md)
now admits document/default changes together, fences native state/generation/
preferences and guards owner lifetime during persistence. Capture has local
keyboard controls and a visible numeric future-default label. Next expose the
four ordering commands through guarded accessible native editor/capture controls
with catalogue gestures and correct text/list/canvas precedence, while resolving
the shared Ellipse decision, alongside
owned native output service handoffs, remaining tools and the
format policy and all other contracted output/capture/application domains.
Physical clipboard/color/accessibility, shutdown and human gates remain
Required; this does not accept full output conformance or parity.

Each checkpoint updates requirement traceability, runs native tests and the
implemented shared vectors, and lands on `main`. Passing the small pilot vector
set does not establish full capability completion. Linux edition qualification,
shared goldens, accessibility and release reviews remain required as described
in `product-contract/READINESS.md`.
