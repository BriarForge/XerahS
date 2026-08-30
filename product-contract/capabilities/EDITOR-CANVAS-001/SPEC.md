# EDITOR-CANVAS-001 Canvas and image operations

Version: 0.1.0

Status: Draft; product-owner approval required

## Requirements

- **EC-001:** New Image MUST create a document from validated dimensions,
  background/alpha choice, color profile, and optional initial pixels. Cancel or
  invalid input MUST NOT replace the current document.
- **EC-002:** Open MUST support native file selection and explicit recent-file
  selection, validate before replacing the session, apply orientation once,
  preserve alpha and color metadata, and report unsupported or corrupt input.
- **EC-003:** Opening a URL MAY be offered, but MUST require explicit user
  action, HTTPS by default, download and decode limits, redirect limits, content
  validation, cancellation, and a clear local-copy/privacy outcome.
- **EC-004:** Insert Image MUST accept a validated file, clipboard image, or
  optional screen capture, create one editable image annotation, preserve alpha
  and aspect ratio, and commit one history operation. Dialog bypass preferences
  MUST NOT bypass validation.
- **EC-005:** Zoom In, Zoom Out, Reset Zoom, Zoom to Fit, wheel/pinch zoom, and
  pan MUST modify only viewport state. They MUST NOT change document pixels,
  annotation coordinates, history, or dirty state.
- **EC-006:** Viewport transforms MUST keep the pointer's logical image point
  stable where native convention permits, expose the zoom value, prevent
  non-finite scales, and retain usable scroll/pan access at minimum and maximum.
- **EC-007:** Crop Image and the Crop annotation tool MUST share one normalized
  retained rectangle. Commit MUST update dimensions and transform or remove
  annotations under a documented intersection policy in one history operation.
- **EC-008:** Auto Crop MUST derive deterministic bounds from explicit border,
  alpha, color-distance, and tolerance rules. An unchanged image MUST produce a
  no-op and MUST NOT add history.
- **EC-009:** Resize Image MUST distinguish pixel dimensions from display scale,
  preserve aspect ratio when locked, validate interpolation and limits, and
  transform annotation geometry consistently when annotations are retained.
- **EC-010:** Resize Canvas MUST define anchor, added-area fill or alpha, crop
  behavior, and annotation translation independently of pixel resampling.
- **EC-011:** Rotate 90 degrees clockwise/counter-clockwise, Rotate 180, custom
  rotation, Flip Horizontal, and Flip Vertical MUST transform pixels, canvas,
  annotations, directional assets, and coordinates as one atomic operation.
- **EC-012:** Custom rotation previews MUST be cancellable and MUST expose angle,
  interpolation, canvas expansion/cropping, and background rules. Commit MUST
  produce one deterministic history entry.
- **EC-013:** Background composition MUST support transparent, solid, gradient,
  and image choices plus validated margin, padding, smart padding, corner radius,
  and shadow. Background assets MUST follow the same safety and durability rules
  as inserted images.
- **EC-014:** Flatten MUST composite source, background, and visible annotations
  into a new immutable source, clear only the objects represented in that
  composite, preserve color/alpha policy, and be fully undoable until history is
  intentionally discarded.
- **EC-015:** Clear Annotations and Clear Image/Document MUST be distinct,
  explicitly labelled operations. Destructive clear of pixels or replacement of
  a dirty document MUST use the session confirmation and recovery rules.
- **EC-016:** Image comparison MUST accept two validated images, align them using
  a declared size policy, provide an accessible slider or equivalent comparison,
  and MUST NOT mutate either source.
- **EC-017:** All canvas operations MUST enforce finite dimensions and resource
  limits, expose progress/cancellation when costly, preserve session atomicity,
  and pass the vectors in `test-vectors.json` on Windows, macOS, and Linux.

## Native adaptation

Native viewport gestures, dialogs, menus, interpolation APIs, and accelerators
MAY differ. Coordinate transforms, pixels, object dispositions, atomicity, and
failure outcomes MUST remain equivalent.

## Baseline traceability and disposition

The pinned candidate exposes ten browser-routed operations plus New, Open,
Insert, zoom/pan, background, compare, clear, and flatten commands. The draft
preserves those outcomes while correcting ambiguous annotation transforms,
unsafe URL/asset handling, and operation atomicity.
