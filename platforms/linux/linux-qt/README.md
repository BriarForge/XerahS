# XerahS for Linux (`linux-qt`)

Reference Linux edition under D-LIN-001: Qt 6, C++17, Wayland first.

## Status

| Capability | State | Evidence |
|---|---|---|
| `FILENAME-GENERATION-001` | Core logic and conformance seam; no UI yet | 51 of 51 vectors pass; [traceability](traceability/FILENAME-GENERATION-001.json) |
| `POST-CAPTURE-ACTIONS-001` | Pipeline engine and conformance seam; native actions, retry, and recovery not yet | 17 of 17 vectors pass; [traceability](traceability/POST-CAPTURE-ACTIONS-001.json) |
| `REGION-CAPTURE-001` | Mapping, keyboard, compositing, last-region, and session state machine; no overlay or capture backend yet | 21 of 21 vectors pass; [traceability](traceability/REGION-CAPTURE-001.json) |
| `EDITOR-SESSION-001` | Document, `.xann` reader and writer, history, and export renderer; no editor UI yet | 19 of 19 vectors pass; [traceability](traceability/EDITOR-SESSION-001.json) |

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

No settings are persisted yet, and the reference Avalonia app's configuration is never read or written.

## Build and test

See [`../AGENTS.md`](../AGENTS.md) for the toolchain. On Ubuntu 24.04:

```bash
sudo apt install cmake ninja-build g++ qt6-base-dev zlib1g-dev python3
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```
