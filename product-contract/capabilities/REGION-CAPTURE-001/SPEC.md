# REGION-CAPTURE-001 Interactive region capture

Version: 0.2.0

Status: Approved by the human product owner on 2026-08-31 (0.1.0) and 2026-09-28 (0.2.0 implementation-readiness clarifications); capture-permission security review and conformance required before activation

## User intent

Users can select any visible rectangular part of their desktop precisely across
multiple monitors and obtain the pixels they confirmed, or cancel without
creating a capture or running post-capture actions.

## Coordinate model and result

- **Desktop physical space** uses integer physical pixels with the virtual
  desktop's top-left as origin; monitors MAY have negative coordinates before
  normalization.
- **Monitor logical space** is the native UI coordinate space for one display.
- **Selection rectangle** is half-open `[left, top, right, bottom)` in desktop
  physical space. Width is `right-left`; height is `bottom-top`.
- **Capture result** contains the normalized physical rectangle, output pixel
  size, optional cursor metadata, optional annotation layer, selected monitor
  IDs, and the display topology snapshot used for mapping.

The lifecycle is normative in `STATE_MACHINE.md`.

## Requirements

- **RC-001:** Before showing an overlay, the implementation MUST determine
  whether screen capture is authorized, denied, restricted, or requires a
  native prompt. It MUST NOT show a misleading selectable desktop when pixels
  cannot be acquired.
- **RC-002:** A first authorization request MUST use the operating system's
  native consent flow. Denial MUST return diagnostic `capture-permission-denied`
  with a native route to settings where available; it MUST NOT loop prompts.
- **RC-003:** The implementation MUST snapshot monitor bounds, scale, rotation,
  stable display identity, color characteristics, and capture availability for
  the session. Topology change during selection MUST cancel with diagnostic
  `display-topology-changed` or restart only after explicit user confirmation.
- **RC-004:** Logical-to-physical mapping MUST be defined per monitor edge and
  MUST produce the same half-open physical rectangle as the vectors. The
  implementation MUST NOT apply one monitor's scale to another monitor.
- **RC-005:** The overlay MUST cover every capturable display without appearing
  in the captured result. It MUST preserve the user's ability to see the source
  desktop, either as a frozen snapshot or through a live transparent surface.
- **RC-006:** Press-drag-release MUST create a rectangle from any drag direction.
  The normalized rectangle MUST have positive width and height before it can be
  confirmed.
- **RC-007:** With quick confirmation enabled, pointer release over a positive
  selection MUST confirm it. With quick confirmation disabled, release MUST
  enter `selected` and Enter or an accessible confirm action MUST confirm it.
- **RC-008:** Escape or the native cancel action MUST cancel from every
  non-terminal state. Cancellation MUST return no image, MUST NOT update the
  last-region setting, and MUST NOT start post-capture actions.
- **RC-009:** Enter MUST confirm only a positive current selection. A click with
  no drag MAY select a detected window or control when snapping is enabled;
  otherwise it MUST NOT capture an accidental one-pixel region.
- **RC-010:** Arrow-key movement and resize, resize handles, and optional size
  presets MUST remain clamped to the capturable desktop. Shift or the native
  precision modifier MAY change step size but MUST be documented in the UI.
- **RC-011:** Window/control snapping MAY use native discovery and native hover
  mechanics. The resulting physical bounds MUST be visible before confirmation
  and users MUST be able to override snapping by dragging freely.
- **RC-012:** The output bitmap dimensions MUST equal the confirmed rectangle's
  physical width and height. Cross-monitor capture MUST composite source pixels
  in desktop physical order without gaps, duplication, or scale resampling.
- **RC-013:** Cursor inclusion is an explicit setting. When enabled, the cursor
  MUST be drawn once at its physical position and hotspot relative to the
  selection. Selection UI, magnifier, handles, toolbars, and crosshairs MUST NOT
  appear in output.
- **RC-014:** Frozen capture MUST represent a single session snapshot. Live
  capture MAY reflect later source changes, but the UI MUST identify live mode
  before confirmation.
- **RC-015:** HDR and wide-gamut input MUST use a documented conversion to the
  workflow output color space. Silent clipping, double tone mapping, or
  per-monitor color inconsistency is prohibited.
- **RC-016:** A completed selection MUST publish exactly one result. Repeated
  pointer, keyboard, window-close, or monitor events after terminal transition
  MUST be ignored.
- **RC-017:** The last-region value MUST update atomically only after capture
  pixels are acquired successfully. Its persisted representation MUST include
  enough display identity and normalized geometry to detect an invalid later
  topology.
- **RC-018:** Permission, portal, capture-source, and mapping failures MUST have
  stable diagnostic codes and MUST retain no screen pixels beyond the configured
  privacy/diagnostic policy.
- **RC-019:** The overlay MUST expose screen-reader instructions, visible focus,
  keyboard-only selection/confirmation/cancellation, high-contrast boundaries,
  and non-color-only state cues.
- **RC-020:** Pointer feedback SHOULD remain at display refresh rate and SHOULD
  show selection changes within 50 ms at the 95th percentile. Confirmation to
  pixel result SHOULD complete within 250 ms for a 1920x1080 region, excluding
  an operating-system portal chooser.
- **RC-021:** Implementations MUST pass the mapping and state vectors in
  `test-vectors.json`.
- **RC-022:** A logical point on display D MUST map to desktop physical space as
  `D.physical.left + (x - D.logical.left) * D.scale` and
  `D.physical.top + (y - D.logical.top) * D.scale`, computed in IEEE 754 double
  precision and then rounded with `floor(value + 0.000001)`. A pointer position
  MUST be mapped on the display that reports it. The normalized selection is
  `left = min(x0, x1)`, `right = max(x0, x1)`, `top = min(y0, y1)`, and
  `bottom = max(y0, y1)` of the two mapped points, so the end point is the
  exclusive edge.
- **RC-023:** Mapped points MUST be clamped to the reporting display's physical
  bounds, where the right and bottom edges are valid exclusive rectangle edges.
  Keyboard and handle adjustments MUST keep the rectangle inside the bounding
  box of all capturable displays. Output pixels inside the rectangle that no
  capturable display covers MUST be transparent black (all channels zero).
- **RC-024:** A keyboard move or resize step MUST be 1 physical pixel, and 10
  physical pixels with the native precision modifier. A move that would cross
  the bounding box MUST stop at the edge and preserve size. A resize MUST keep
  width and height at least 1 physical pixel. Each step is one selection
  change; the resulting bounds MUST be announced to assistive technology.
- **RC-025:** The session state machine MUST process these events: `permission`
  (`authorized`, `denied`, `restricted`), `ready`, `pointer-down`,
  `pointer-move`, `pointer-up`, `key-enter`, `key-escape`, `confirm`,
  `cancel`, `topology-changed`, `capture-succeeded`, and `capture-failed`. An
  event with no transition in `STATE_MACHINE.md` MUST be ignored. Denied or
  restricted permission MUST enter `failed` with `capture-permission-denied`
  without showing an overlay. `capture-failed` MUST enter `failed` with
  `capture-source-failed`.
- **RC-026:** With snapping disabled, a pointer-down and pointer-up at the same
  physical point MUST leave the session `idle`. With snapping enabled and a
  detected window or control under the pointer, the same click MUST select
  that element's clamped physical bounds and then follow RC-007.
- **RC-027:** The persisted last region MUST record format version 1, the
  normalized physical rectangle, and the stable ID and physical bounds of every
  display the rectangle intersects. A later last-region capture MUST fail with
  `last-region-invalid`, without capturing, when any recorded display is
  missing or its physical bounds changed.

## Settings and defaults

Window snapping, magnifier, information display, keyboard nudge, control
detection, and quick confirmation default to enabled. The frozen overlay is the
default. Cursor inclusion defaults to disabled. Magnifier size, zoom, dimming,
colors, snap sizes, and snap distance MUST be validated before the session and
MUST fall back to documented safe defaults when imported legacy values are out
of range.

## Native adaptations

- Windows MAY use DXGI/WinRT and Win32 hit testing.
- macOS MAY use ScreenCaptureKit and AppKit overlay panels.
- Wayland Linux MAY require a portal source chooser before XerahS can display
  its own region surface; X11 MAY use direct capture.

These mechanics MAY differ, but permission truth, physical rectangle, output
dimensions, confirmation, cancellation, and diagnostics MUST satisfy the shared
requirements. A compositor that exposes only a portal-selected region is an
Equivalent disposition only when the user can achieve the same selected pixels;
Degraded or Unavailable requires product-owner approval.

## Compatibility and baseline disposition

The contract preserves rectangle, transparent, custom, and last-region workflow
outcomes and the baseline's magnifier/snapping/keyboard interactions. It makes
mixed-scale mapping, topology invalidation, permission diagnosis, HDR behavior,
and terminal-event idempotence explicit rather than copying implementation
accidents.

Baseline evidence: `src/desktop/app/XerahS.RegionCapture/Services/SelectionStateMachine.cs`,
`CoordinateTranslationService.cs`, `OverlayManager.cs`, `UI/RegionCaptureControl.cs`,
and platform capture services at commit
`5c7e36dea77ab131fe0f5e2101e5d578ccde0306`. Ledger link:
`CAPTURE-REGION-001` plus rectangle/custom/last-region workflow rows.
