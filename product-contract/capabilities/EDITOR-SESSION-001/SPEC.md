# EDITOR-SESSION-001 Native editor session

Version: 0.2.0

Status: Approved by the human product owner on 2026-08-31 (0.1.0) and 2026-09-28 (0.2.0 implementation-readiness clarifications); conformance required before activation

## User intent

Users can open a captured or saved image, add and revise a rectangle annotation,
undo and redo edits predictably, export the rendered result, and later reopen
the re-editable annotation document without losing the source or edit state.

This qualification contract covers the session foundation and rectangle tool.
The complete approved editor surface is expanded by `EDITOR-ANNOTATIONS-001`,
`EDITOR-EFFECTS-001`, `EDITOR-CANVAS-001`, `EDITOR-OUTPUT-ACTIONS-001`,
`EDITOR-SETTINGS-001`, and `EDITOR-UTILITIES-001`.

## Data model

- **Source image**: immutable decoded pixels and their color-space metadata.
- **Document**: source identity, canvas size, ordered annotation objects, embedded
  assets, version, timestamps, and compatibility metadata.
- **Rectangle annotation**: stable UUID, half-open floating-point canvas bounds,
  stroke/fill style, stroke width, rotation, opacity, and z-order.
- **Rendered image**: source pixels composited with visible annotations in
  document order.
- **Dirty state**: whether document state differs from its last successful
  persistence checkpoint.

## Requirements

- **ES-001:** Loading MUST decode the source without altering the original file.
  Unsupported, corrupt, empty, or resource-exhausting input MUST fail with a
  typed diagnostic and MUST NOT create a partially usable session.
- **ES-002:** The source image's pixel width, height, orientation, alpha, and
  color interpretation MUST be normalized once at load and retained as document
  metadata. Canvas coordinates use source-image pixels with origin at top-left.
- **ES-003:** Creating a rectangle MUST normalize drag direction into positive
  bounds and MUST reject zero-area rectangles. Its initial style MUST be a
  snapshot of current tool settings.
- **ES-004:** Selection MUST visibly identify exactly the selected objects and
  expose native pointer and keyboard move, resize, rotate, delete, and ordering
  operations. Clicking empty canvas MUST clear selection unless a native
  multi-select modifier is active.
- **ES-005:** Every user-visible document mutation MUST be one atomic history
  operation. Pointer gestures MAY emit previews but MUST commit one operation on
  completion.
- **ES-006:** Undo MUST restore the complete state before the most recent
  committed operation, including selection where meaningful. Redo MUST restore
  the undone state. A new mutation after undo MUST clear the redo branch.
- **ES-007:** Undo and redo MUST NOT dispose or mutate assets still referenced by
  another reachable history state. History truncation MAY release unreachable
  assets under a documented memory policy.
- **ES-008:** Cancelling an in-progress gesture or editor close prompt MUST
  restore the last committed state. Closing a dirty session MUST offer Save,
  Discard, and Cancel through native accessible UI.
- **ES-009:** Export MUST composite the immutable source and all visible
  annotations in z-order into an image with the requested format and color
  profile. For identical normalized document, renderer version, format, and
  options, decoded output pixels MUST be identical on all supported platforms,
  except as ES-025 permits for rotations that are not multiples of 90 degrees.
- **ES-010:** Rectangle rendering MUST clip to canvas bounds, apply opacity once,
  use the specified stroke width centered on the rectangle path, and render fill
  before stroke. Native preview MAY be accelerated but export is authoritative.
- **ES-011:** A successful raster save MUST replace the destination atomically
  where the platform supports it and MUST mark raster state clean only after the
  raster and required annotation sidecar both reach their persistence policy.
- **ES-012:** If raster save succeeds but sidecar save fails, the session MUST
  remain dirty, retain the editable document, and report which artifact failed.
  It MUST NOT claim a fully successful save.
- **ES-013:** `.xann` version 1 MUST be a gzip stream containing UTF-8 JSON that
  validates against `annotation-document.schema.json`. It MUST contain the
  source PNG, source SHA-256 when a source file exists, canvas dimensions,
  timestamps, ordered annotations, and embedded annotation images.
- **ES-014:** Saving a document with no annotations MUST remove an existing
  default sidecar only after explicit sidecar-save intent and MUST return no
  sidecar path. It MUST NOT delete the raster image.
- **ES-015:** Loading a newer unsupported major document version MUST fail
  without rewriting it. Unknown additive properties and unknown annotation
  types in a compatible version MUST be retained for round-trip and MAY be
  rendered as unsupported placeholders.
- **ES-016:** When the current raster's SHA-256 differs from the sidecar source
  hash, the user MUST be warned. The implementation MUST preserve annotations
  and offer the current raster or embedded source explicitly; it MUST NOT
  silently bind annotations to mismatched pixels.
- **ES-017:** Embedded source and annotation images MUST be size-limited before
  decode. Parsers MUST reject decompression bombs, invalid base64, non-finite
  coordinates, duplicate annotation IDs, and unreasonable canvas dimensions.
- **ES-018:** Autosave or crash recovery MAY write a separate recovery document.
  It MUST NOT overwrite the user's last explicit save and MUST disclose recovery
  age and source on restart.
- **ES-019:** Editor controls MUST support keyboard navigation, visible focus,
  screen-reader names and state, non-color-only selection, high contrast, and
  zoom-independent hit targets.
- **ES-020:** Source pixels and documents are private user data. They MUST NOT be
  uploaded or included in support bundles without explicit user action. Temp and
  recovery files MUST follow configured retention and secure-delete limits.
- **ES-021:** A 3840x2160 source with one rectangle SHOULD open to interactive
  state within 500 ms and maintain 60 Hz manipulation previews on supported
  hardware; export MAY take longer but MUST expose progress and cancellation.
- **ES-022:** Implementations MUST pass the structural and history vectors in
  `test-vectors.json` and shared golden-image vectors once those fixtures are
  approved.
- **ES-023:** Colors in `.xann` MUST be `#AARRGGBB` hexadecimal with
  non-premultiplied 8-bit sRGB channels. Writers MUST emit uppercase digits;
  readers MUST accept either case. The qualification defaults are stroke
  `#FFFF0000`, fill `#00000000`, stroke width 4, rotation 0, and opacity 1.
- **ES-024:** A rectangle's bounds are the normalized `start` and `end` points
  in canvas coordinates. Rotation is in degrees, clockwise on screen, about the
  bounds centre. A new rectangle MUST become the only selected object and take
  the highest z-order.
- **ES-025:** For rotations that are multiples of 90 degrees, export MUST render
  each visible annotation in z-order as follows, using IEEE 754 binary64 in the
  order written and without fused multiply-add. The fill region is the bounds;
  the stroke region is the bounds grown by half the stroke width minus the
  bounds shrunk by half the stroke width (empty when either shrunk dimension is
  not positive). For each pixel square `[i, i+1) x [j, j+1)`, coverage is the
  exact area of its intersection with a region. With channels scaled to 0..1,
  fill alpha `af = coverage_fill * Af` and stroke alpha
  `as = coverage_stroke * As`; the layer alpha is `as + af * (1 - as)` and the
  layer colour is `(Cs * as + Cf * af * (1 - as)) / layer_alpha` (0 when the
  layer alpha is 0). The layer alpha is then multiplied by opacity once, giving
  `A`, and composited source-over onto the current result (colour `Cd`, alpha
  `Ad`) in non-premultiplied sRGB encoded values as `Ao = A + Ad * (1 - A)` and
  `Co = (C * A + Cd * Ad * (1 - A)) / Ao`, or 0 when `Ao` is 0. Each channel of the result MUST be quantized after every
  annotation as `floor(value * 255 + 0.5)`. Other rotations use the same
  model with exact polygon coverage and MAY differ from the reference by at
  most 1 in any 8-bit channel.
- **ES-026:** Selection changes alone are not document mutations and MUST NOT
  create history operations. Style changes apply to every selected object as
  one operation and also update the tool default; with nothing selected they
  update only the tool default. `undo_count` and `redo_count` are the numbers of
  operations currently available to undo and redo.
- **ES-027:** The session is dirty exactly when its history position differs
  from the last successful persistence checkpoint. Undoing or redoing back to
  the checkpoint MUST make it clean. When a new mutation truncates the redo
  branch that contained the checkpoint, the session MUST stay dirty until the
  next successful save.
- **ES-028:** Readers MUST enforce these limits before allocating pixels: each
  canvas or image dimension from 1 to 100000 and at most 268435456 pixels, and
  at most 1073741824 bytes of decompressed `.xann` JSON. Every coordinate,
  width, rotation, and opacity MUST be finite.
- **ES-029:** Load, parse, and save failures MUST use these diagnostics:
  `source-unsupported`, `source-corrupt`, `source-empty`, `source-too-large`,
  `document-version-unsupported`, `document-invalid`, `document-too-large`,
  `raster-save-failed`, and `sidecar-save-failed`. When several document
  failures apply, the first in this order MUST be reported:
  `document-version-unsupported`, `document-too-large`, `document-invalid`.
- **ES-030:** The default sidecar path MUST be the raster file's full name with
  `.xann` appended (for example `shot.png.xann`). When that file is absent,
  readers MUST also look for the raster's name with its last extension
  replaced by `.xann` so that baseline sidecars remain discoverable; the
  baseline convention MUST be confirmed by runtime observation before
  activation.

## Native adaptation

Windows, macOS, and Linux MAY use native commands, menus, pickers, shortcuts,
GPU APIs, and accessibility patterns. Tool placement and gesture mechanics MAY
differ. Document coordinates, history boundaries, pixel export, dirty state,
error outcomes, and `.xann` compatibility MUST remain equivalent.

## Defaults, compatibility, and migration

The qualification rectangle defaults are an opaque red stroke, transparent
fill, 4-pixel stroke width, zero rotation, full opacity, and topmost z-order.
Imported KovaForge `.xann` version 1 files MUST remain readable. The native
document writer MUST preserve compatible unknown fields needed for a lossless
round-trip. Full legacy `.sxie` effect compatibility is owned by its separate
compatibility row.

## Baseline traceability and disposition

Evidence: `ShareX.ImageEditor/src/ShareX.ImageEditor/Core/Editor/EditorCore.cs`,
`Core/Annotations/RectangleAnnotation.cs`,
`Core/Persistence/XannProjectFileService.cs`, and KovaForge editor tests at
baseline commits recorded in `kova-0.29.0.yaml`. Ledger links:
`EDITOR-SESSION-001` and `COMPAT-XANN-001`.

Disposition: preserve the session, rectangle, history, export, and re-editable
sidecar outcomes; correct non-atomic save/dirty ambiguity and make mismatched
source handling explicit.
