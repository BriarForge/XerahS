# Linux ImageEditor

Native ImageEditor feature module of the `linux-qt` reference edition. Implements
the approved Product Contract using Qt Core libraries shared by the app and the
conformance adapter.

`AnnotationDocument` reads and writes `.xann` v1 documents and sidecars.
`EditorSession` owns rectangle annotations, selection, atomic history, and dirty
state. `EditorViewport` owns finite zoom, pointer anchoring and bounded pan
independently of the session. `AnnotationRenderer` is the authoritative export path: quarter-turn
rectangles use exact box coverage, and arbitrary rotations use exact polygon
coverage under ES-025.

The native editor in `app/EditorWindow.cpp` and `app/EditorCanvas.cpp` loads sources, draws and manipulates
rectangles, exposes undo/redo and save, and reopens editable sidecars. See the
edition [implementation plan](../../IMPLEMENTATION.md) and
[traceability](../../traceability/) for remaining contract requirements.
