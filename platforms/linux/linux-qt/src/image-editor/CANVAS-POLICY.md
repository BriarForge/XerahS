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

Custom image rotation, auto crop, general affine annotations, inserted images,
background composition, flatten, clear and comparison remain subsequent work.
Shared goldens and the remaining qualification gates in
`product-contract/READINESS.md` still apply.
