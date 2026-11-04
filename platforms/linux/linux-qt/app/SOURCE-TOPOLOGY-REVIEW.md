# Qt snapshot authority and remaining qualification

Decision from Aoife/Mike, 2026-11-04: Qt's screen list is the authoritative
pre-acquisition Linux topology, following toolkit enumeration in Flameshot and
reference XerahS. `screenTopology()` samples QScreen name/manufacturer/model/
serialNumber, logical geometry, devicePixelRatio, orientation and screen depth
before the Screenshot request. `snapshotScreenTopology()` in the Qt Core
library derives a SHA-256 identity from a versioned, length-delimited tuple;
list order never supplies identity. Duplicate tuples fail closed. Depth is the
available colour characteristic; primaries, transfer function and HDR are
explicitly unavailable, so RC-003's full colour description remains partial.
All listed Qt screens are candidates; authorization and actual raster size
must succeed before an overlay is shown.

The brief directs unspecified layout semantics to reference XerahS. Read-only
behavioral reference (no source copied):
`/home/mike/Work/ShareX/XerahS/src/desktop/app/XerahS.RegionCapture/Services/WaylandMonitorLayoutNormalizer.cs`,
`NormalizeCumulative` / `BuildCumulativeOrigins`, inspected 2026-11-04. Physical
origins pack logical columns and rows cumulatively, using each column/row's
maximum physical extent, and normalize the top-left to zero. Each display's
width and height use only its own DPR, with the contract's binary64/floor
rounding. Logical coordinates remain unchanged for reporting-display mapping.
Published vectors continue to define their own independent physical topology.

A returned bitmap must match the snapshot physical bounding box exactly.
Disagreement fails `capture-topology-mismatch`, without guessing, resampling,
overlay, last-region write or post-capture actions. This is the explicitly
accepted degraded outcome when Qt and the compositor disagree. Screen add/
remove, geometry, orientation and DPI signals cancel selection with
`display-topology-changed`. Qt 6.4 lacks a QScreen DPR-change signal: a 25 ms
comparison plus checks at bitmap delivery, confirmation and worker publication
cover DPR changes and stale-source races. Once pixels complete, later monitor
events cannot alter the terminal session.

The compositor reads only the one decoded in-memory portal image. Desktop
physical coordinates are translated to bitmap offsets using the snapshot's
physical bounding-box origin. It copies per-display source pixels without
resampling and clears uncovered space to transparent black. Overlays paint
that frozen image and never enter the result. Live mode is not offered.

`Capture last region` and the saved `last-region` workflow source reload the
version-1 private geometry record and check display IDs/bounds against CURRENT
Qt topology before constructing/requesting a screenshot client. Missing or
changed displays fail `last-region-invalid` with no acquisition. Successful
recapture uses the same worker compositor and output pipeline without overlays.

Candidate evidence: `RegionCaptureTest` topology builder/geometry cases,
`RegionCaptureNativeTest` two fake display descriptions through real native
controller/overlays and exact saved pixels, `SnapshotCompositionTest` gaps and
negative origins, and `LastRegionStoreTest` atomic private persistence.
`PortalScreenshotTest::frozenConfirmationNeverReopensThePortalFile` covers file
removal after decode; that private-bus suite must run outside the restricted
socket sandbox. Fake displays share an offscreen QScreen surface; these tests
do not claim physical multi-monitor placement or compositor qualification.

## Cursor, colour and privacy dispositions still required

The [Screenshot portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html)
(checked 2026-11-04, version 3 documentation) returns an image URI. It exposes
modal/interactive controls and, in version 3, target choices; it provides no
cursor inclusion/hotspot metadata or HDR/tone-mapping negotiation. The current
client uses handle_token, interactive=false and modal=false. It preserves the
decoded image's colour metadata without asserting a qualified conversion.
RC-013 and RC-015 remain partial pending explicit platform dispositions; no
cursor or HDR feature is added here. RC-018 remains partial because the final
configured retention/privacy policy and portal-owned file lifecycle remain
unresolved. Physical GNOME/Plasma/wlroots, Wayland/X11, fractional-scale,
rotation/hotplug, Orca and human permission/privacy/security qualification are
separate Required gates, not accepted by candidate implementation status.
