# Opt-in compositor screenshot capture

The Capture setting **Include the pointer in screenshots** defaults off. Off
uses the existing Screenshot portal. On freezes the setting and the Qt output
identity before acquiring one compositor frame on an `OwnedWork` worker.
`CaptureController` accepts the frame only when output name, logical origin,
integer scale, normal Wayland transform, physical dimensions and image extent
match that frozen single-output topology. It then uses the existing overlay,
physical crop and save pipeline. The cursor is already in the frame; no second
cursor draw or later pointer sample is made. A failed compositor attempt does
not fall through to the portal. Configuration transfer preserves the receiving
profile's local opt-in rather than importing it.

The backend prefers `ext-image-copy-capture-v1` with `paint_cursors`; its
fallback is `zwlr_screencopy` with `overlay_cursor`. Shared-memory ARGB pixels
retain premultiplied alpha, and XRGB is made opaque. Cancellation is checked
before connecting, during protocol waits and between copied image rows. A
cancelled, changed-topology or destroyed-owner result cannot publish into a
later capture. The worker owns its connection, buffers and result.

The implementation currently refuses multi-output, rotated, fractional-scale,
dmabuf-only and mismatched sources. `cursorPaintedIntoFrame` records the
protocol's paint request, not pixel-based cursor detection. Software-cursor
exclusion when the paint flag is off is unproven; the application uses this
backend only for the opted-in inclusion path. RC-013 qualification remains open.
The owner approval for this permission posture is recorded in
[CAPTURE-POLICY.md](../CAPTURE-POLICY.md).

## Build and verify

Install the Wayland client development package and pkg-config as listed in
[INSTALL.md](../../INSTALL.md). Qt remains dynamically linked. The mock-server
test also needs the Wayland server development library.

```sh
cmake -S platforms/linux/linux-qt -B platforms/linux/linux-qt/build -G Ninja
cmake --build platforms/linux/linux-qt/build --target xerahs compositor-frame-test
ctest --test-dir platforms/linux/linux-qt/build -R '^compositor-frame$' --output-on-failure
```

The mock server supplies controlled output pixels and exercises the real
Wayland protocol client. It proves frame inclusion, source refusal, format
handling, cancellation and timeout; it is not a live compositor qualification.

## Protocol sources

The MIT-licensed XML and generated C/client/server headers are kept together
in `protocol/`. They were generated with wayland-scanner 1.26 using
`--strict --include-core-only`. For each XML, generate `client-header`,
`server-header`, and `private-code` outputs. The capture-source XML deliberately
contains only the output-source factory: this backend does not use or link the
foreign-toplevel source interface. Preserve the copyright and licence notices.

## Controlled native proof — 2026-11-13

Qt 6.4.2 and private headless Sway 1.9 exercised the production last-region
controller with cursor inclusion enabled and no injected compositor reader.
The save receipt identified the reopened PNG: 76×12 pixels, exactly 84 cursor
pixels in one clipped 12×7 rectangle, zero pixel mismatches and no visible
selection overlay. The real wlr-screencopy client acquired the frame before
cropping. All compositor sockets and the virtual pointer were private; the
host desktop was not captured.

The synthetic [PNG](../../tests/fixtures/native-cursor-proof/cursor-proof.png)
and [observation with source hashes](../../tests/fixtures/native-cursor-proof/observation.json)
retain this result. The temporary harness and logs live in
`/tmp/xerahs-cursor-proof`; those temporary files are not a shipped test target.
A callback lifetime error in that harness was corrected before this run, and
the real save receipt accessor passed. This single-output proof does not
qualify other desktops, ext-image-copy-capture, hardware displays or cursor
exclusion on the default portal path. RC-013 remains partial.
