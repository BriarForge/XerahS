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
prepares private sRGB display copies on a cancellable worker. `RasterSource`
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
