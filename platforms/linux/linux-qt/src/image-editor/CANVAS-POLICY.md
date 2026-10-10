# Native canvas implementation policy

Implements the existing `EDITOR-CANVAS-001` requirements; this document does not
approve new product behavior or close the full capability's parity review.

Crop Image and the temporary Crop tool call the same production operation.
The retained rectangle normalizes drag direction, floors left/top, ceils
right/bottom, clips to the source canvas, and rejects empty/non-finite input.
Source pixels are copied without resampling. Rectangle objects wholly outside
the retained canvas are removed; intersection uses the actual rotated,
stroke-grown geometry. Partially intersecting objects keep their full editable
bounds, translated to the new origin, and export clips them to the canvas.
Stable IDs and object order survive. Unsupported annotation geometry causes an
atomic failure rather than loss of its data.

Auto Crop scans the canvas for the smallest half-open box containing foreground
pixels, which trims only complete background rows/columns from the four edges.
By default its bounds include the authoritative composite of source and visible
annotations; the dialog also offers source-only bounds. This never flattens
annotations into the source. The resulting box uses the same crop operation and
intersection policy. Border matching is explicit: transparent pixels only,
the top-left pixel of the chosen bounds image, or a selected ARGB colour. Alpha
at or below the chosen threshold (0..255) is always background. Colour matching
uses the maximum absolute difference across the four 8-bit RGBA channels,
with inclusive tolerance (0..255); hidden RGB in transparent pixels is ignored
by the alpha rule. The scan is global, not flood fill: internal matching pixels
do not remove foreground islands. No foreground or full-canvas bounds produce
a no-op, with no source encoding, history entry, or change to dirty state.
Compositing, scanning and source copying support cancellation and progress.

Resize Canvas uses nine anchors. Added pixels use the explicitly selected
non-premultiplied ARGB colour, transparent by default. Each origin offset is
`floor((new_dimension - old_dimension) * anchor_fraction)`, where the fraction
is 0, 0.5, or 1. Centre expansion puts an odd extra pixel on the right/bottom;
centre shrinking removes the odd extra pixel on the left/top. Retained pixels
are copied exactly; annotation translation and removal use the crop policy.

Quarter-turn rotations and flips permute pixels without interpolation. An
annotation uses half-open boundary coordinates; a pixel index refers to its
centre. Clockwise rotation therefore maps pixel `(x,y)` to `(H-1-y,x)` while
mapping the boundary point to `(H-y,x)`. Rectangle centres and orientations
transform together. Unknown directional assets require their tool-specific
implementation before they can be transformed safely.

Custom rotation uses the canvas centre as its pivot; positive angles rotate
clockwise. Angles are normalized modulo 360. Expansion uses
`ceil(abs(W*cos(angle)) + abs(H*sin(angle)))` for width and the corresponding
swapped expression for height, placing the original centre at the new canvas
centre. Without expansion the dimensions stay fixed and the rotated image is
cropped there. Exact expanded quarter turns use the lossless pixel permutation;
zero/full turns are no-ops. Other angles inverse-map output pixel centres into
the source, using nearest neighbour or alpha-aware bilinear interpolation.
Samples outside the source use the chosen ARGB background; transparency inside
the source is preserved. Bilinear border samples participate in the same
premultiplied-alpha weighting as source samples. Rotation retains rectangle
size/stroke width, transforms centres and adds the angle to each orientation.
Without expansion, wholly outside annotations are removed under the crop
intersection policy; partial objects retain their editable bounds.

The native Rotate Image dialog shows the authoritative source/annotation
composite of the actual full-resolution prepared edit, fitted to the preview
area. Angle, interpolation, expansion/cropping and uncovered-pixel colour are
explicit controls. Control changes cancel obsolete work and invalidate its
acceptance; only a ready preview of the latest controls can be accepted. Cancel
waits for the live worker cooperatively without changing the session. Accept
commits the prepared source and geometry already shown, guarded by the original
state ID, as one history entry. Neither preview nor cancellation changes pixels,
annotations, selection, dirty checkpoints, history, or the main viewport.

Resize Image exposes pixel dimensions, an aspect lock, and nearest-neighbour
or bilinear interpolation. The aspect lock derives height from width rounded
to the nearest integer. Sampling aligns pixel centres and clamps at image
edges. Bilinear interpolation weights premultiplied colour and alpha before
returning non-premultiplied bytes, so transparent RGB does not create halos.
Uniform resizing transforms rectangle bounds and stroke width. Non-uniform
resizing that would shear a rotated rectangle or require different stroke
widths on its axes currently reports an unsupported transform; affine object
support remains required. This also covers aspect-locked sizes whose integer
rounding makes the two scale factors unequal.

Preparation runs on a worker with row-level cancellation/progress. The native
PNG codec preserves the source colour profile. Encoding or cancellation failure
never changes the session. Only a fully prepared, validated PNG/document pair
enters history, guarded by the original session state ID. Undo and redo restore
the source PNG, dimensions, annotations and selection; viewport state stays
independent. Allocation is reserved before changing the redo branch.

Flatten uses the authoritative renderer and records which annotation layers
had positive visible coverage on the canvas. Those objects alone are removed
after their composite is encoded as a new immutable source. Hidden, fully
transparent, zero-opacity and wholly off-canvas objects remain editable, in
their original relative order. A visible unsupported annotation causes an
atomic failure; hidden unsupported objects and compatible fields are preserved.
Embedded assets are retained in the document and all reachable history states;
this edition does not infer that an asset is unreachable from unknown fields.
No represented objects means no-op, with no new history entry. Separate
background layers still depend on the upcoming background implementation.

Clear Annotations removes all objects, including unsupported placeholders,
as one undoable operation and retains source PNG bytes and colour metadata
exactly. Clear Image and Annotations is a separate, explicitly labelled command
that resets every source pixel to transparent black, removes objects, and
retains canvas dimensions/profile. Its native confirmation defaults to Cancel;
dirty sessions offer Save before Clear, Clear without Saving and Cancel. A
failed or cancelled save prevents clearing. Clear commits only after preparation
succeeds, and Undo restores pixels, objects, selection and the applicable clean
checkpoint. Already empty objects or an already cleared canvas add no history.
Neither command alters disk files until an explicit save. That save follows the
existing explicit no-annotation sidecar removal policy.

General affine annotations, inserted images,
background composition and comparison remain subsequent work.
Shared goldens and the remaining qualification gates in
`product-contract/READINESS.md` still apply.
