# EDITOR-SESSION-001 Native editor session

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

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
  options, decoded output pixels MUST be identical on all supported platforms.
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
