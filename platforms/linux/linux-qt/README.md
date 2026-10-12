# XerahS for Linux (`linux-qt`)

Reference Linux edition under D-LIN-001: Qt 6, C++17, Wayland first.

## Status

| Capability | State | Evidence |
|---|---|---|
| `CORE-PLATFORM-001` | Native lifetime instance ownership, authenticated bounded activation and a keyboard-accessible command window; complete application foundation and release gates pending | Real-process/actual-application forwarding, failure/recovery, core dispatch and native action tests; no CORE vectors published yet; [traceability](traceability/CORE-PLATFORM-001.json) |
| `FILENAME-GENERATION-001` | Core logic and conformance seam; no UI yet | 51 of 51 vectors pass; [traceability](traceability/FILENAME-GENERATION-001.json) |
| `POST-CAPTURE-ACTIONS-001` | Production Core engine with native worker PNG publication, UI-thread clipboard dispatch, cancellation/activity and retained receipts; save/clipboard write-ahead journal and native restart result history; complete catalogue, retry, media recovery and retention pending | 17 of 17 vectors pass; [traceability](traceability/POST-CAPTURE-ACTIONS-001.json) |
| `REGION-CAPTURE-001` | Core mapping/compositing/session model and native frozen portal/overlay flow; cancellable, owner-fenced screenshot requests and responsive confirmed pixel composition; per-monitor source mapping, capture settings and full qualification pending | 21 of 21 vectors plus private native transport/controller/cancellation tests; [traceability](traceability/REGION-CAPTURE-001.json) |
| `EDITOR-CANVAS-001` | Native New/Open and opt-in persisted recent files with confirmed dirty replacement; viewport, crop, auto crop, resize, rotation, flips, annotation flatten, clear and image comparison; remaining image operations pending | All 3 published vectors plus native geometry, history, window, creation, opening, recent-file, comparison and persistence tests; [traceability](traceability/EDITOR-CANVAS-001.json) |
| `EDITOR-SESSION-001` | Document, `.xann` reader and writer, history, and export renderer; editor window with rectangle tool, selection, save, and sidecar reopen; resize and ordering by keyboard, arbitrary-angle rotation and export | 19 of 19 vectors pass; [traceability](traceability/EDITOR-SESSION-001.json) |
| `EDITOR-SETTINGS-001` | Persisted recents, drawing/appearance defaults, toolbar placements/hotkeys and normal/maximized window state over private atomic storage; live desktop appearance, creation-only styles, toolbar customization, display recovery and memory opt-out; remaining preferences, complete ledger and review gates pending | All 3 published vectors plus core/storage/recovery, native controls/dispatch, private desktop transport, window lifecycle/display ports, restart, history isolation and failure tests; [traceability](traceability/EDITOR-SETTINGS-001.json) |

The next implementation steps are tracked in [IMPLEMENTATION.md](IMPLEMENTATION.md).
The [full Linux scope audit](CONTRACT-AUDIT.md) records the remaining application,
capture, recording, upload, workflow, editor, integration and release work.

The native test suite requires `dbus-run-session` (the `dbus-daemon` package on
Ubuntu). Desktop appearance and screenshot transport tests use private buses
with no service activation directories; they cannot start or query the host's
portal services.

## Layout

- `src/` production libraries with conformance seams (Qt Core only)
- `src/image-editor/` native ImageEditor feature module ([README](src/image-editor/README.md))
- `conformance-adapter/` thin adapter for `conformance/runner`
- `tests/` native unit tests for paths the vectors cannot reach
- `traceability/` requirement to implementation and evidence maps

## The `xerahs` app

`build/xerahs` is a native command-window and tray app (`com.xerahs.app`, "XerahS").
Repeated desktop launch shows/restores the existing window. A lifetime kernel
lock prevents another primary after a busy, rejected or uncertain activation;
private bounded IPC authenticates peer users before sending paths. The
[activation policy](app/ACTIVATION-POLICY.md) records ownership, delivery and
review limits. With no tray host, the window provides the current commands.

- **Capture region** (tray menu, tray click, or `xerahs --capture-region`, which forwards to a running instance) takes a frozen snapshot through the xdg-desktop-portal Screenshot portal, shows a fullscreen selection overlay per screen, and runs the post-capture pipeline: save as PNG to `~/Pictures/XerahS` with the default filename pattern, then copy the image to the clipboard. The first capture shows the portal's consent prompt.
- Keys in the overlay: drag or Space to select, Enter to capture, Esc to cancel, arrows to move by 1 pixel (Shift for 10), Alt+arrows to resize.
- **Cancel capture** in the command window or tray cancels a pending native portal interaction, an open overlay or confirming pixel composition. Confirmed composition runs on a worker with visible accessible progress; Escape cancels without waiting for that worker. Cancellation before pixel completion creates no capture, save or clipboard action. After confirmation, PNG saving runs off the UI thread with visible activity: cancellation lets an already running save finish, records that fact and stops later actions. Portal errors produce stable diagnostics; returned images decode on a native worker with validated resource limits. The [capture policy](app/CAPTURE-POLICY.md) records request/file authority and remaining qualification.
- Hyprland binding example: `bind = , PRINT, exec, xerahs --capture-region`.
- `packaging/com.xerahs.app.desktop` gives desktop launches the `com.xerahs.app` identity, which portals use to attribute consent.

- **Edit retained capture…** (tray or main window) prepares the latest retained pixels on a cancellable worker and opens a new unsaved editor. It works after failed save/clipboard actions, preserves the original receipt, and chooses a durable path only through explicit Save. **Review retained capture result…** shows read-only action outcomes and optional report details, including actual file metadata; private paths/context stay in this user-requested report. It repeats no actions and changes no files or clipboard. The application and adapter share the [production canonical serializer](app/PIPELINE-RESULTS.md). **Discard retained capture…** confirms removal of the current in-memory result; saved files, clipboard contents and existing editors are kept. A stale confirmation cannot clear a newer result. Only the latest pixel receipt is retained until another pipeline completes, discard, or application close. **Capture result history…** (main window/tray) reviews multiple durable result records across restart. Interrupted pending actions have unknown outcomes; recovery never repeats them. **Forget record…** confirms removal of unchanged metadata while keeping files and retained pixels. Corrupt, unsafe and newer records stay preserved and unavailable. The [journal recovery evidence](app/JOURNAL-RECOVERY.md) records native write-ahead authority, private storage and remaining media recovery/retention/retry/human gates.

- **Open image** (tray menu or `xerahs --edit FILE`) opens the editor. Editor **File → Open Image…**, Ctrl+O, replaces the current session after loading succeeds and a dirty document's Save/Discard/Cancel confirmation. Loading exposes progress and cancellation; failed or cancelled opening preserves the current session. Saving to the same candidate path reloads the newly saved raster and sidecar before replacement.

- **Recent Images → Manage Recent Images…** (editor File menu or tray) offers explicit opt-in, a maximum of 1..100 entries (default 10), Remove Selected and Clear All. Apply atomically saves the private list and preferences; Cancel leaves active and stored values unchanged. Only successful openings and complete raster/sidecar saves after opt-in add paths. Selecting an entry uses the same validated Open and dirty confirmation flow; missing entries remain removable. Preferences survive restart. Remove, Clear, trim and opt-out remove forgotten paths from the current and retained previous settings together. A preference-save failure preserves the old list and visibly reports the problem while leaving a successful image operation successful.

- **Preferences → Drawing Defaults…** changes stroke colour, fill colour and thickness for new annotations in every open editor and after restart. Colours accept `#RRGGBB` or `#AARRGGBB` and retain alpha; thickness is 1..10000 pixels. Apply saves all fields together before publishing; Cancel and Reset followed by Cancel leave preferences unchanged. Existing annotations, selection, dirty state and undo/redo stay intact. Invalid input, a stale draft or a write failure keeps the dialog open with an error. The stroke/fill and thickness toolbar controls explicitly edit selected objects and save the resulting creation defaults. A preference-save failure leaves the document edit undoable and shows a persistent status message.

- **Preferences → Appearance…** (or tray **Editor appearance…**) selects the desktop theme/accent or manual Dark/Light and accent colour. Apply persists the whole draft atomically before updating all editor windows; Cancel and Reset followed by Cancel leave settings and colours unchanged. The saved accent retains alpha, while displayed colours can adjust for contrast. Desktop theme/accent/contrast changes update editors without changing preferences or documents. The read-only Settings portal supplies appearance hints, with a native Qt palette fallback when unavailable; reduced motion disables Qt menu, combo, tooltip and toolbox effects. Native widget accessibility and focus surfaces remain in use. Physical desktop/style and assistive-technology qualification remain Required.

- **Preferences → Remember Window Size and Maximized State** controls window memory across editors. Startup restores the normal client size and maximized flag in logical pixels, constrained to an available work area. Display, work-area, scale and screen changes recover visible normal bounds without saving or editing the document. Accepted close saves normal size and the maximized state; Cancel or an incomplete image Save leaves window preferences unchanged. Turning memory off atomically removes the saved state from current and previous records and stops automatic writes. A failed toggle rolls back the checkmark; a failed close-time save reports the error and leaves stored preferences unchanged.

- **Preferences → Toolbar…** reorders the 37 current editor commands and changes visibility, group boundaries and additional hotkeys. Apply saves the entire draft atomically and updates every editor; Cancel and Reset followed by Cancel leave the layout unchanged. Invalid, conflicting, stale or failed writes keep the dialog open with an error. Hidden commands retain their fixed menus and protected shortcuts, including **View → Show Toolbar** (Ctrl+H) and **File → Close Editor**. Text entry, modal dialogs and active editor operations take precedence over command hotkeys. Image files, annotations and undo/redo remain unchanged by customization. Compatible unavailable commands stay preserved and inactive. Complete command coverage and shared metadata, physical keyboard and accessibility reviews remain Required.

- In the editor, drag to draw rectangles, click to select, drag or arrow keys to move, Delete to remove, Alt+arrows to resize, R for quarter turns or **Rotate selection…** for any angle, `[`/`]` to reorder, Ctrl+Z/Ctrl+Shift+Z for history, Ctrl+S to save the flattened PNG with its `.xann` sidecar.

- **New image…** (tray menu or editor **File → New Image…**, Ctrl+N) offers
  validated dimensions, transparent or ARGB background, sRGB/linear sRGB/Display
  P3/Adobe RGB, and optional initial pixels from a file or clipboard. Initial
  pixels retain native size at the top left; chosen dimensions crop or pad them.
  Preparation has progress and cancellation. Replacing a dirty editor offers
  Save, Discard and Cancel after preparation succeeds. An untitled image stays
  unsaved until Ctrl+S successfully writes its chosen PNG destination.

An unreadable, corrupt or newer sidecar stops opening and leaves both files untouched. If the raster changed since its annotations were saved, a native warning offers the validated current raster or embedded source with their dimensions; Cancel opens no session. Annotation coordinates and assets are preserved whichever source is chosen.

**Viewport:** Zoom In/Out, 100% and Fit actions expose keyboard shortcuts (Ctrl+plus/minus, Ctrl+0, Ctrl+9). Ctrl+wheel or pinch zooms around the pointer. Scrollbars, wheel, middle-button drag, Space+drag, and arrows with no selection pan. Zoom and pan never alter pixels, annotations, history, or dirty state.

**Image menu:** Crop Image and the Crop tool share a retained rectangle. Auto Crop offers transparent or colour matching with explicit alpha and RGBA tolerance, includes visible annotations by default, and adds no history when bounds are unchanged. Resize Image changes pixel dimensions with aspect lock and nearest/bilinear interpolation; Resize Canvas uses nine anchors and a chosen alpha/colour fill. Image rotations and flips change the source and annotations together. Every successful edit is undoable and saves its edited, unannotated source into the sidecar. Costly work exposes progress and cancellation. The [canvas policy](src/image-editor/CANVAS-POLICY.md) records intersection, sampling and remaining transform limitations.

**Rotate Image…** previews the edited source and visible annotations with controls for clockwise angle, nearest/bilinear interpolation, canvas expansion or cropping, and uncovered-pixel colour/alpha. Changing controls cancels obsolete previews. Cancel preserves the document; OK applies the displayed edit as one undo step. Exact expanded quarter turns preserve pixels without interpolation.

**Flatten** makes rendered visible annotations part of a new immutable source, removing only objects represented in that composite. Hidden or off-canvas objects remain; visible unsupported types prevent flattening. **Clear Annotations** keeps source pixels intact. **Clear Image and Annotations…** resets the canvas to transparent pixels after confirmation, with Save before Clear, Clear without Saving and Cancel for dirty documents. These operations preserve full Undo/Redo recovery and colour metadata. Unsupported annotation types appear as preserved placeholders in a native dock.

**Compare Images…** accepts two raster files or the current rendered editor
image plus a file. Top-left and centered alignment retain native pixel sizes
and show unmatched areas as transparent. Private display copies use sRGB,
with untagged images treated as sRGB. Move the divider by dragging, slider,
arrows, or Home/End. Loading and current-image rendering expose progress and
cancellation; failed or cancelled replacement keeps the prior pair. Comparison
leaves editor history, dirty state, selection, viewport and files unchanged.
File comparisons use the raster itself and do not load its editing sidecar.

The canvas adapter also exposes `compare-images` over injected `first` and
`second` objects containing `size` and either row-major `pixels` (ARGB hex
strings) or a uniform `fill`, with `alignment` (`top-left` or `center`) and
`reveal` (0..1000). This calls the production layout/compositor and returns
canvas size, offsets, split column, pixels and a diagnostic. Published shared
comparison vectors and full capability qualification remain pending.

Its `create-image` seam accepts `size`, an optional `background` (#AARRGGBB,
sRGB, transparent by default), `profile` (`srgb`, `linear-srgb`, `display-p3`,
`adobe-rgb`) and optional `initial` pixels with their `size` and `profile`.
It calls production preparation and returns source-profile pixels, dimensions,
profile, initial dirty/history state and a diagnostic. Published shared New
Image vectors and full capability qualification remain pending.

Native editor preferences use the XDG config directory plus
`com.xerahs.app/linux-qt/editor-settings.json`, with private file/directory
permissions, bounded reads/writes, compatible unknown retention and atomic
replacement. Missing settings cause no startup write. Newer or unreadable
settings stay untouched; native startup warnings report field recovery or
read-only fallback. The reference Avalonia configuration is never read or
written. The [preference policy](src/image-editor/PREFERENCES-POLICY.md)
records metadata, recovery limitations, remaining field work and review gates.

## Build and test

See [`../AGENTS.md`](../AGENTS.md) for the toolchain. On Ubuntu 24.04:

```bash
sudo apt install cmake ninja-build g++ qt6-base-dev zlib1g-dev python3
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The Product Contract workflow also builds this edition in Ubuntu 24.04 and
runs the native tests and implemented conformance capabilities on every push
and pull request.
