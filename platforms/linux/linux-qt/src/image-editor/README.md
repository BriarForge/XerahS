# Linux ImageEditor

Native ImageEditor feature module of the `linux-qt` reference edition. Implements
the approved Product Contract using Qt Core libraries shared by the app and the
conformance adapter.

`AnnotationDocument` reads and writes `.xann` v1 documents and sidecars.
`SourceBinding` validates source identity and requires an explicit choice before
annotations can be attached to a raster whose hash differs from the sidecar.
`EditorSession` owns rectangle annotations, selection, atomic history, and dirty
state. `EditorViewport` owns finite zoom, pointer anchoring and bounded pan
independently of the session. `AnnotationRenderer` is the authoritative export path: quarter-turn
rectangles use exact box coverage, and arbitrary rotations use exact polygon
coverage under ES-025.

`CanvasOperations` transforms source pixels and annotation geometry atomically.
Native PNG preparation stays at the image-codec I/O boundary, and
`EditorSession::commitCanvas` admits only validated results for the current
state. [CANVAS-POLICY.md](CANVAS-POLICY.md) documents the retained-rectangle,
anchor, sampling and history policies and remaining work.

`ImageComparison` validates native-size layouts, computes divider columns and
composes immutable pixel buffers. The native `ImageComparisonDialog` shares
that layout/split, provides accessible keyboard and pointer controls, and
prepares private sRGB display copies on owned immutable workers. Cancel/Close/
owner teardown never joins decoding; job identity isolates late results, complete
publication precedes control re-enabling, and the owned picker/main-editor busy
boundary preserve history and files. See [owned comparison loading](../../app/COMPARISON-WORKERS.md).
`RasterSource`
is the shared Qt image-decoder boundary used by editor opening and comparison;
editing sidecar resolution remains exclusively in `EditorSource`.

`NewImage` validates new canvas limits and composes native-size initial pixels
at the top left. `NewImageSource` privately converts background/initial colours
to the selected profile and prepares a validated immutable PNG/document plus
a private sRGB display copy. `NewImageDialog` provides native dimensions,
alpha/colour, profile, file and clipboard controls with progress/cancellation.
`EditorWindow` confirms dirty replacement and starts an untitled session with
no persistence checkpoint; saving or cancelling never confuses its initial
state with a previously saved document.

`RecentImages` owns opt-in state, validated limits and platform-aware path
ordering independently of the document. `RecentImageMenu` hosts the native
editor/tray selection and Apply/Cancel removal controls. `NativeEditorPreferences`
connects that model to versioned private storage after the app claims its
instance socket. [RECENT-PATHS-POLICY.md](RECENT-PATHS-POLICY.md) records boundaries,
privacy and the subsequent settings work.

`EditorPreferences` validates known fields independently, preserves compatible
unknown fields and rejects invalid changes atomically. `EditorPreferenceStore`
uses a bounded private native storage boundary, writes current and privacy-pruned
previous settings together, and publishes only after replacement succeeds.
The production conformance seams share this code. Native opt-in and list edits
survive restart; failed preference writes retain the old list without undoing
successful image work. `DrawingDefaultsDialog` provides atomic Apply/Cancel,
draft reset and alpha-aware native colour pickers for border/fill/thickness.
Successful saves update creation styles across open editors, without selection
or history edits; newly opened/replaced sessions use the same persisted defaults.
Explicit toolbar selection edits retain history even if preference persistence
fails, with a visible status error. `EditorWindowPlacement` resolves normal
client/frame bounds against logical work areas. `NativeEditorWindowState` reads
Qt screens and frame margins, observes work-area/DPI/screen/state changes and
restores visible geometry without writing. Accepted close stores normal size
and maximized state; the shared memory toggle excludes retained state and stops
automatic writes. Native port and actual editor tests cover display recovery,
minimization, failure, cancellation, restart and preserved redo/files.
`EditorAppearance` resolves the theme and accent over translated desktop hints
and a native palette with alpha-preserving saved colours and readable display
roles. Native controls apply/reset/cancel atomically; editor palettes follow
desktop changes without altering application-native palette authority,
documents or history. The Settings observer uses asynchronous read-only D-Bus
with owner/stale-reply recovery, and a private-bus fixture verifies transport.
`ToolbarPreferences` validates persistent placements and protected hotkey
collisions in Qt Core. Native customization drafts reorder/show/hide/group
commands and reset without persistence until Apply. `NativeEditorToolbar`
reuses actual editor actions, keeps fixed menus and protected shortcuts
reachable with an empty toolbar, and gives text/modal/busy input precedence.
Native tests cover atomic failure/cancellation, live editing and dispatch,
multiple windows, restart and preserved image/checkpoint/history state.
Full field coverage and review gates remain required;
[PREFERENCES-POLICY.md](PREFERENCES-POLICY.md)
records implemented metadata, compatibility/recovery limits and review gates.

The native editor in `app/EditorWindow.cpp` and `app/EditorCanvas.cpp` loads sources, draws and manipulates
rectangles, exposes undo/redo and save, and reopens editable sidecars. See the
edition [implementation plan](../../IMPLEMENTATION.md) and
[traceability](../../traceability/) for remaining contract requirements.

`AnnotationTransfer` provides bounded selected-object projection/private decoding
and shares field validation/serialization with the document. `EditorSession`
atomically inserts fresh identities or removes an acknowledged unchanged Cut
selection, preserving previous selection/checkpoint/Redo on failure. Native
clipboard/menu/input and pending shared tool/asset policy are indexed in
[ANNOTATION-TRANSFER.md](../../app/ANNOTATION-TRANSFER.md).

[Native annotation tool evidence](../../app/ANNOTATION-TOOLS.md) indexes explicit Select/Rectangle/Crop modes, cancellation, complete-batch finite geometry validation, rounded no-op history and actual pre-command selection recovery. Ellipse persistence and pixels await shared decision EA-005-LINUX-2026-10-14; unknown ellipse data remains inert and round-trippable. All remaining tool/field/target/human qualification stays Required.

`RectangleGeometry` shares fixed-opposite-point pointer resize and supported-field/derived-export-bound validation with immutable previews and session commits. [Rectangle edit evidence](../../app/RECTANGLE-EDIT.md) indexes native numeric/color/alpha controls, creation-default isolation, exact persistence and remaining full-field/native/human qualification.

The guarded owned Rectangle creation draft snapshots style/state/selection for numeric native creation and pointer drags. [Keyboard creation evidence](../../app/RECTANGLE-CREATION.md) records real keyboard input, exact history and actual save/reopen/capture output; full tools/fields/human qualification remains Required.

Guarded Core selection filters requested UUIDs with a set, rejects stale displayed history, and preserves document/history/checkpoint state. The [native object-list evidence](../../app/ANNOTATION-OBJECTS.md) indexes keyboard selection, source generations, owned labels and physical/human limits.

[Owned native New/Open preparation](../../app/NEW-OPEN-WORKERS.md) now owns
worker inputs and result publication, releases cancellation/owner teardown without
joining, guards nested dialogs and freezes session replacement admission. Borrowed
QImage buffers are bounded/copied at native ingress; decoding, colour conversion,
composition and PNG preparation remain on workers. Core policies remain unchanged.
