# RC-003/004/012/022/027 and CORE-014: the physical topology authority

Decision: `RC-TOPOLOGY-AUTHORITY-2026-11-20`, resolved by the loop under the
maintainer's delegated owner-block authority on 2026-11-20 (agent opencode,
GLM 5.3). Contract: 0.4.5 (no requirement text changes; every RC requirement
still reads exactly as approved). Requirements: RC-003, RC-004, RC-012,
RC-022, RC-027, CORE-014.

## Problem

The native candidate builds its pre-acquisition topology from QScreen and
synthesizes the desktop's physical arrangement by packing logical columns
and rows (the reference XerahS arrangement). The shared loop state held the
question: "approve authoritative pre-acquisition physical topology source
(portal/Wayland output metadata vs QScreen)", because nothing proved that
the packed physical arrangement equals the compositor's screenshot layout -
so RC-004/RC-012's cross-monitor correctness, RC-022's physical mapping and
RC-027's persisted-bounds semantics were all candidate rather than decided.

## Choice

1. **The selection space is QScreen logical geometry.** The overlay, the
   pointer and the confirmed rectangle live there, and on Wayland Qt's
   QScreen geometry is itself derived from zxdg_output_v1 logical_position
   and logical_size - so "QScreen" and "Wayland output metadata" are the
   same logical authority; there is no separate portal logical space to
   choose between.
2. **Per-display physical bounds are each display's own logical extent
   scaled by its own scale** (RC-022's formula, per-display, mode-verified).
   No other display's scale participates (RC-004).
3. **No Wayland protocol exposes a compositor's global physical pixel
   arrangement.** wl_output carries per-output mode (pixel size), transform
   and scale but no global placement; zxdg_output_v1 carries the logical
   placement; the leading tools compose per-output buffers in logical space
   (grim) or crop the compositor's own all-screens image (Spectacle).
   Therefore a client-synthesized global physical arrangement is a
   **documented degraded disposition**, not a compositor fact.
4. **The degraded packed arrangement is endorsed only when the compositor's
   own facts prove a strict grid tiling**: every output carries complete
   facts (wl_output geometry, current mode, scale, done, name - and the
   zxdg_output logical position and size), is unrotated and unflipped, each
   logical size times its own scale equals its mode, and the logical
   rectangles tile the plane with no interior gaps and no overlaps (exact on
   integers: no pairwise interior overlap, and the per-output areas sum to
   the bounding box's area). For such tilings the packed columns/rows are
   provably the compositor's logical arrangement up to translation, so
   RC-004/RC-012's packed mapping and RC-027's packed persisted bounds are
   decided rather than guessed. A single output is the trivial tiling.
5. **Anything else fails closed with the existing diagnostic
   `compositor-layout-unproven`**: staggered, overlapping, rotated or flipped
   outputs, incomplete facts, or a compositor without zxdg_output_v1.
   Cross-monitor captures in such layouts are refused rather than captured
   misaligned; the portal path remains the fallback. A requested output that
   no longer exists by name is `compositor-source-mismatch`
   (captureTopologyMismatch for the caller's frozen session).
6. **Per-display compositor-native frames** are captured by the output's own
   compositor identity (`CompositorFrameRequest.outputName`), inside a
   proven layout. The single-output path is unchanged and does not require
   zxdg_output (wl_output facts alone remain sufficient for one output).

## Sources (read 2026-11-20)

- grim, the canonical wlroots screenshot tool, `output-layout.c` and its
  documented geometry flow (github.com/emersion/grim): per-output frames
  from wlr-screencopy composed at xdg_output logical positions, each output
  scaled by its own logical scale; the wl_output-only fallback
  (`guess_output_logical_geometry`) documents that grid assumptions "may be
  incorrect for complex multi-monitor setups".
- KDE Spectacle, bug 487997 and the maintainer's design description
  (bugs.kde.org): a region screenshot is a crop of an all-screens composite
  produced by the compositor - with the recorded scaled-display resolution
  mismatches - so the compositor's own image, not a client-arranged global
  physical layout, is the pixel authority.
- The zxdg_output_v1 protocol (wayland-protocols unstable/xdg-output):
  logical_position/logical_size are the compositor's authoritative logical
  arrangement; wl_output carries no global placement.
- The app's own existing honest machinery: `proveSingleOutputLayout` and
  `compositor-layout-unproven` already refused unprovable layouts; this
  decision extends the same standard to multiple outputs instead of
  weakening it.

## Impact

The bounded first build: CompositorFrame now binds zxdg_output_v1 (vendored
protocol XML through wayland-scanner like the existing vendored
protocols), collects the logical position and size per output, and
`proveOutputLayout(outputs, requestedName)` decides the whole layout by the
rules above. A named output can be captured inside a proven grid (mixed
scales included), with the frame carrying the named output's mode-verified
bounds; a missing name is a source mismatch; a gapped, overlapping,
staggered, rotated or silent-xdg arrangement refuses with
`compositor-layout-unproven`, proven red by disabling each rule. The
controller's cursor-inclusion path now requests the frozen display by its
compositor name instead of "the only output". Tests drive the real client
binding against a fake libwayland server (26/26), including an end-to-end
two-output named capture. What remains of the backlog item: RC-012's
cross-monitor composite from per-output frames on proven grids (the
controller currently still requires one display for the compositor-native
path), RC-003's colour/HDR characteristics (explicitly unavailable), live
desktop qualification on real Sway/mixed-scale/rotated hardware, and
CORE-014's cursor/delay polish. Expected values come from the decision rules
and the compositor facts, not from the implementation.
