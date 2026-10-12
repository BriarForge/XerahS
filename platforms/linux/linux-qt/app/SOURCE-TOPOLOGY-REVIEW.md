# Native capture source authority still to qualify

RC-003/004/012/014/015/017/022/027 remain Required. The existing Screenshot
controller infers a single desktop scale from one raster and Qt logical
bounds. That does not establish per-monitor native physical mapping, stable
identity, rotation, colour or complete mixed-scale capture. Published mapping
vectors prove the domain transform for supplied topology, not this native
topology inference. No contract, expected result or supported target is changed
by this implementation note.

## Evidence that prevents guessing

The [ScreenCast API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
describes optional stream position/size in compositor coordinates, which can
differ from pixel coordinates and the negotiated stream size. Its optional
stream ID is session-local and remains across restored sessions, not a general
stable display identifier. Pixel dimensions alone therefore cannot establish
the required desktop physical origins or persisted display identity.

[Qt's QScreen API](https://doc.qt.io/qt-6/qscreen.html) warns that screen names
are not unique native identities. Its screen pixel ratio can differ from a
window's ratio on Wayland with fractional scaling. [Qt's high-DPI guidance](https://doc.qt.io/qt-6/highdpi.html)
also distinguishes device-independent UI geometry from device pixel buffers.
Applying one raster-width ratio to every Qt screen does not resolve these
authority differences.

## Required next source evidence

1. Acquire authorized per-display native buffers and their actual pixel
   dimensions without resampling; bind each to an authoritative physical
   origin, logical reporting surface, stable display identity, rotation,
   colour description and capture availability. Prove those bindings through
   native protocol/API evidence; do not infer an accepted identity from a
   display name or session-only token.
2. If a backend provides only compositor coordinates, establish how its
   native topology satisfies the contract's desktop physical coordinate
   model. Missing deciding semantics require ROOT-ESCALATE-001 clarification
   through the product/conformance owners, rather than inventing expected
   mixed-scale results or accepting a guessed mosaic.
3. Define a frozen multi-stream snapshot consistently with RC-014. Preserve
   observed source timing and avoid unreviewed synchronization tolerances or
   presenting frames sampled at different stages as an atomic native snapshot.
4. Qualify cursor authority, HDR/wide-gamut conversion, source revocation and
   cleanup, selected-source availability, topology changes, screen locking,
   failed or partial acquisitions, permission rejection and cancellation.
   Permission/privacy reviews remain human gates.
5. Run all published mapping/state vectors plus independent native pixel
   fixtures and physical tests on the supported GNOME/Plasma/wlroots,
   Wayland/X11 and distribution matrix. Include different scales and heights,
   fractional scales, negative origins, rotation, gaps and hotplug. Core vector
   success or the new cancellable compositor does not substitute for this.

The current worker composition improvement is independent of these unresolved
source bindings: it uses the existing frozen input, preserves exact source
pixels and colour metadata, and keeps cancellation responsive. It does not
claim that the uniform-scale acquisition path now meets mixed-display scope.
