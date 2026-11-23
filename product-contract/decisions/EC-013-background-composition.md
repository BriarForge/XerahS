# EC-013 background composition (decided 2026-11-23)

Decision ID: EC-013-BACKGROUND-COMPOSITION-2026-11-23
Requirement: EDITOR-CANVAS-001 EC-013 (background composition: transparent,
solid, gradient and image choices plus validated margin, padding, smart
padding, corner radius and shadow; background assets follow the inserted-image
safety and durability rules).
Decided by: the build loop under the maintainer's delegated owner-block
authority (see AGENTS.md Stewardship). No SPEC text changes; the requirement's
MUSTs stand as written and this record settles the open semantics the row
named ("deterministic smart-padding/background ... remain explicit unanswered
product decisions").

## Question

EC-013 names the knobs but not their meaning. Four questions were open:

1. **Model.** Is the background a non-destructive view-time state composed at
   export (the reference's approach), or a committed operation?
2. **Smart padding.** What exactly is detected, with what tolerance, and what
   happens to the detected border?
3. **Geometry.** What do margin, padding, corner radius and shadow mean
   relative to each other, and what are the defaults?
4. **Scope of kinds.** Which background kinds does this edition own?

## Research

The behavioral reference is the cross-platform ShareX.ImageEditor (the edition
this product replaces), read directly from the upstream checkout and confirmed
online at
https://github.com/ShareX/ShareX/tree/cross-platform/ShareX.ImageEditor/Integration
(same tree, 2026-11-23):

- `Integration/ImageEditorOptions.cs` - the defaults: `BackgroundMargin = 80`,
  `BackgroundPadding = 40`, `BackgroundSmartPadding = true`,
  `BackgroundRoundedCorner = 20`, `BackgroundShadowRadius = 30`,
  `BackgroundType = "Transparent"`, solid default `#FF222222`, gradient
  defaults `#FFDD2476` to `#FFFF512F`, `BackgroundImagePath = ""`. The kind
  list is Gradient, Color, Transparent, Image, Wallpaper.
- `Presentation/ViewModels/MainViewModel.CanvasState.cs` - smart padding
  detection: target colour `skBitmap.GetPixel(0, 0)` (the source's top-left
  pixel), constant `tolerance = 30` per RGBA channel (absolute, content is
  any pixel differing by more than the tolerance), insets are the uniform
  border distances per edge, and the feature disables itself when no border
  is detected.
- `Presentation/Views/EditorView.axaml` + `MainViewModel.cs` - the layer
  geometry: the margin band (chosen kind) is square and outermost; inside it
  the card (a border with `CornerRadius = BackgroundRoundedCorner` and the
  canvas shadow) wraps the padding band (`SmartPaddingThickness =
  BackgroundPadding`, `SmartPaddingColor` = the source's top-left pixel
  sampled live), which wraps the image viewport; the viewport is clipped by
  the detected insets only when `IsSmartPaddingActive` (the user toggle AND
  effects active AND insets detected).
- `Presentation/Helpers/ShadowEffectHelper.cs` - the canvas shadow:
  `Blur = BackgroundShadowRadius`, `OffsetX = 0`, `OffsetY = 10`, black at
  alpha 80 of 255; there is no shadow on/off switch - radius 0 removes it.
- `Presentation/Views/BackgroundSettingsPanel.axaml` - the control set:
  margin, padding and shadow-radius sliders, a smart-padding toggle, mode
  picker and gradient presets; no separate shadow toggle.

The classic WinForms ShareX editor (the same product's earlier edition) has
no background composition at all - only canvas resize with a fill colour - so
this feature's semantics come from the cross-platform edition the contract's
behaviour tracks.

## Decision

**Model: one committed, fully undoable canvas operation.** The reference
composes the background view-time and bakes it at export; this edition
already owns an immutable-source-plus-committed-canvas-operations architecture
(CanvasOperations with one deterministic history entry per edit), so the
background lands as `CanvasAction::Background` in that seam: Apply composes
the new source, records exactly one history entry, and Undo restores the
previous source. This is the smallest reversible model that makes the saved
PNG carry the composed background, and it keeps every canvas edit on one
audited path. The reference's view-time state remains a possible later
refinement, not a promise.

**Geometry (inner to outer), with the reference defaults:**

- Content: the source image. With smart padding ON and a uniform border
  detected, the border is clipped (the compose bakes the same visible pixels
  the reference's clipped viewport shows).
- Padding band: width = padding (default 40), colour = the source's exact
  top-left pixel. Painted whenever a background is applied, exactly like the
  reference's `SmartPaddingColor`/`SmartPaddingThickness` band.
- Card: the padding band plus content, outer corners rounded by
  cornerRadius (default 20).
- Shadow: composed whenever shadowRadius > 0 (default 30, 0 means none) as a
  gaussian-blurred silhouette of the rounded card (sigma = radius / 3, the
  house `gaussian_blur` convention), offset (0, +10), black at alpha 80/255 -
  the reference's `CreateCanvasShadow` facts. It composes over the margin
  band and is bounded by the composed canvas.
- Margin band: width = margin (default 80), square outer edge, painted with
  the chosen kind: transparent (nothing), solid (default `#FF222222`) or a
  vertical linear gradient (default `#FFDD2476` to `#FFFF512F`, top to
  bottom). With the default margin (80) the shadow's reach (radius 30 +
  offset 10 = 40) fits inside the band; a smaller user margin clips the
  shadow at the canvas edge, which the operation reports as the honest
  outcome rather than growing the user's margin.

**Smart padding rule:** the reference's own algorithm, reusing the existing
AutoCrop TopLeftColor machinery semantics: target = the source's top-left
pixel; a pixel is background when every RGBA channel differs from the target
by at most tolerance 30 (inclusive); insets are the per-edge uniform border;
the clip applies only when the user toggle is ON and the border is non-empty.
Annotations are not part of detection (the reference scans the source bitmap
only); they translate with the composed content and stay editable.

**Validation:** margin, padding and cornerRadius are finite and >= 0;
shadowRadius finite in 0..100; tolerance 0..255; the composed canvas must
pass the seam's existing dimension/pixel budget (kMaxDimension/kMaxPixels).
An invalid request refuses with the existing typed diagnostic and no partial
output.

**Scope of this build half:** transparent, solid and gradient kinds with the
full geometry above, committed through the editor's Image > Background dialog.
Named follow-ups: the image kind (a file-picker background asset behind the
same decode/safety rules as inserted images), the reference's Wallpaper kind
(not one of the SPEC's four), gradient presets (furniture), and EC-014's
flatten background clause which this unblocks.

## The image kind (decided and built 2026-11-23, same slice continuation)

- **Paint rule:** the reference's Image mode is an ImageBrush at the Fill
  layout (`Stretch.UniformToFill`, BackgroundSettingsPanel picks the file,
  `ApplyImageBackground` -> `TryCreateImageBrushFromPath(filePath,
  DesktopWallpaperLayout.Fill, ...)`), so the chosen image paints the whole
  margin band cover-scaled: uniform scale to fill the canvas, centred,
  overflow cropped at the canvas bound, sampled with the house bilinear
  (deterministic, edge-clamped).
- **Safety rules:** "Background assets MUST follow the same safety and
  durability rules as inserted images" means the app prepare seam resolves the
  chosen path through `loadRaster` - the validated native decode shared by
  editor loading and comparison (encoded-byte ceiling 1 GiB, orientation
  applied once, bounded dimensions, the typed stable diagnostics) - before
  the pure core compose runs; the core seam never does file I/O and refuses
  an Image kind without a resolved asset with the existing typed error. A
  corrupt or missing file fails the whole edit with `source-unsupported` and
  no partial output.
- **EC-014 reconciliation:** with the committed model an applied background
  is already in the source, so flatten's existing composite satisfies the
  background clause: source + applied background + visible annotations
  composite into the new immutable source, one history entry, annotations
  cleared (backgroundThenFlattenCompositesTheAppliedBackground).
- The dialog's Choose button opens the native file picker; tests inject the
  picker through the same seam pattern as BackgroundRemoverDialog
  (setBackgroundFilePicker + chooseBackgroundImage).

## Sources

- ShareX.ImageEditor `Integration/ImageEditorOptions.cs` (defaults; read from
  the upstream checkout; the same file exists online at the URL above).
- ShareX.ImageEditor `Presentation/ViewModels/MainViewModel.CanvasState.cs`
  (smart padding detection: top-left target, tolerance 30, insets,
  self-disable without a border).
- ShareX.ImageEditor `Presentation/Views/EditorView.axaml` and
  `MainViewModel.cs` (layer geometry, band colour and thickness, the
  `IsSmartPaddingActive` gate).
- ShareX.ImageEditor `Presentation/Helpers/ShadowEffectHelper.cs` (canvas
  shadow blur/offset/alpha facts).
- ShareX.ImageEditor `Presentation/Views/BackgroundSettingsPanel.axaml`
  (control set; shadow has no toggle, only radius).
- Classic ShareX (same checkout) has no background composition, confirming
  the feature's semantics are owned by the cross-platform edition.
