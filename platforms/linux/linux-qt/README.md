# XerahS for Linux (`linux-qt`)

Reference Linux edition under D-LIN-001: Qt 6, C++17, Wayland first.

## Status

| Capability | State | Evidence |
|---|---|---|
| `FILENAME-GENERATION-001` | Core logic and conformance seam; no UI yet | 51 of 51 vectors pass; [traceability](traceability/FILENAME-GENERATION-001.json) |
| `POST-CAPTURE-ACTIONS-001` | Pipeline engine and conformance seam; native actions, retry, and recovery not yet | 17 of 17 vectors pass; [traceability](traceability/POST-CAPTURE-ACTIONS-001.json) |
| `REGION-CAPTURE-001` | Mapping, keyboard, compositing, last-region, and session state machine; no overlay or capture backend yet | 21 of 21 vectors pass; [traceability](traceability/REGION-CAPTURE-001.json) |
| `EDITOR-CANVAS-001` | Viewport controls; native crop, auto crop, resize, rotation, flips, annotation flatten, clear and image comparison; remaining image operations pending | All 3 published vectors plus native geometry, history, window, comparison and persistence tests; [traceability](traceability/EDITOR-CANVAS-001.json) |
| `EDITOR-SESSION-001` | Document, `.xann` reader and writer, history, and export renderer; editor window with rectangle tool, selection, save, and sidecar reopen; resize and ordering by keyboard, arbitrary-angle rotation and export | 19 of 19 vectors pass; [traceability](traceability/EDITOR-SESSION-001.json) |

The next implementation steps are tracked in [IMPLEMENTATION.md](IMPLEMENTATION.md).

## Layout

- `src/` production libraries with conformance seams (Qt Core only)
- `src/image-editor/` native ImageEditor feature module ([README](src/image-editor/README.md))
- `conformance-adapter/` thin adapter for `conformance/runner`
- `tests/` native unit tests for paths the vectors cannot reach
- `traceability/` requirement to implementation and evidence maps

## The `xerahs` app

`build/xerahs` is a single-instance tray app (`com.xerahs.app`, "XerahS").

- **Capture region** (tray menu, tray click, or `xerahs --capture-region`, which forwards to a running instance) takes a frozen snapshot through the xdg-desktop-portal Screenshot portal, shows a fullscreen selection overlay per screen, and runs the post-capture pipeline: save as PNG to `~/Pictures/XerahS` with the default filename pattern, then copy the image to the clipboard. The first capture shows the portal's consent prompt.
- Keys in the overlay: drag or Space to select, Enter to capture, Esc to cancel, arrows to move by 1 pixel (Shift for 10), Alt+arrows to resize.
- Hyprland binding example: `bind = , PRINT, exec, xerahs --capture-region`.
- `packaging/com.xerahs.app.desktop` gives desktop launches the `com.xerahs.app` identity, which portals use to attribute consent.

- **Open image** (tray menu or `xerahs --edit FILE`) opens the editor: drag to draw rectangles, click to select, drag or arrow keys to move, Delete to remove, Alt+arrows to resize, R for quarter turns or **Rotate selection…** for any angle, `[`/`]` to reorder, Ctrl+Z/Ctrl+Shift+Z for history, Ctrl+S to save the flattened PNG with its `.xann` sidecar.

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

No settings are persisted yet, and the reference Avalonia app's configuration is never read or written.

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
