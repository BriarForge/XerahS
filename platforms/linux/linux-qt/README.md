# XerahS for Linux (`linux-qt`)

Reference Linux edition under D-LIN-001: Qt 6, C++17, Wayland first.

## Status

| Capability | State | Evidence |
|---|---|---|
| `FILENAME-GENERATION-001` | Core logic and conformance seam; no UI yet | 51 of 51 vectors pass; [traceability](traceability/FILENAME-GENERATION-001.json) |

## Layout

- `src/` production libraries with conformance seams (Qt Core only)
- `src/image-editor/` native ImageEditor feature module ([README](src/image-editor/README.md)); not started
- `conformance-adapter/` thin adapter for `conformance/runner`
- `tests/` native unit tests for paths the vectors cannot reach
- `traceability/` requirement to implementation and evidence maps

## Build and test

See [`../AGENTS.md`](../AGENTS.md) for the toolchain. On Ubuntu 24.04:

```bash
sudo apt install cmake ninja-build g++ qt6-base-dev python3
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```
