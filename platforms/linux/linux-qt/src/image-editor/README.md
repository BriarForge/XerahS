# Linux ImageEditor

Native ImageEditor feature module of the `linux-qt` reference edition. Implements
the approved Product Contract using Qt Core libraries shared by the app and the
conformance adapter.

`AnnotationDocument` reads and writes `.xann` v1 documents and sidecars.
`SourceBinding` validates source identity and requires an explicit choice before
annotations can be attached to a raster whose hash differs from the sidecar.
`EditorSession` owns rectangle annotations, selection, atomic history, and dirty
state. `EditorViewport` owns finite zoom, pointer anchoring and bounded pan
independently of the session. `AnnotationRenderer` is the authoritative export path: quarter-turn
rectangles use exact box coverage, and arbitrary rotations use exact polygon
coverage under ES-025.

`CanvasOperations` transforms source pixels and annotation geometry atomically.
Native PNG preparation stays at the image-codec I/O boundary, and
`EditorSession::commitCanvas` admits only validated results for the current
state. [CANVAS-POLICY.md](CANVAS-POLICY.md) documents the retained-rectangle,
anchor, sampling and history policies and remaining work.

`ImageComparison` validates native-size layouts, computes divider columns and
composes immutable pixel buffers. The native `ImageComparisonDialog` shares
that layout/split, provides accessible keyboard and pointer controls, and
prepares private sRGB display copies on a cancellable worker. `RasterSource`
is the shared Qt image-decoder boundary used by editor opening and comparison;
editing sidecar resolution remains exclusively in `EditorSource`.

The native editor in `app/EditorWindow.cpp` and `app/EditorCanvas.cpp` loads sources, draws and manipulates
rectangles, exposes undo/redo and save, and reopens editable sidecars. See the
edition [implementation plan](../../IMPLEMENTATION.md) and
[traceability](../../traceability/) for remaining contract requirements.
