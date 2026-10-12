# Native screenshot transport and remaining capture qualification

This is candidate Linux implementation evidence for RC-001/002/008/016/018
and CORE-004/005. It does not approve capture-permission or privacy behavior,
accept a platform disposition or establish complete region capture.

## Native interaction and terminal outcomes

`PortalScreenshot` requests one non-interactive frozen screenshot through the
desktop's native Screenshot portal. It resolves the service owner, attempts
normal service activation once if absent, and binds its subscription and call
to that unique owner. The owner must remain authoritative until the result.
No automatic retry repeats consent. Native error messages and result URIs
never enter diagnostics or notifications.

The subscription precedes the screenshot method. At most eight early response
records, each retaining only a bounded URI value, await the returned handle.
Only a response from that owner at the verified Request path can provide
pixels. Early legacy handles are supported; unrelated sender/path signals and
all events after the one terminal result are ignored.

The method reply has a 25-second transport deadline. This deadline stops when
the handle arrives: the user's consent interaction has no automatic timeout.
Cancellation calls Request.Close and returns `Cancelled` without pixels or an
error notification. Response code 1 also means user cancellation; other
non-success response codes mean source failure. Explicit NotAllowed or
AccessDenied errors mean permission denial. Failures use
`capture-source-failed`; denials use `capture-permission-denied`. A generic
failure is not guessed to be a denial.

The native tray offers Cancel Capture while busy, including before an overlay
exists. The capture controller exposes availability before acquisition and
supports synchronous cancellation from that first notification. Cancelled
clients retain only the bounded pending method cleanup needed to close an
unexpected late handle, independently of the controller's lifetime. No old
request can finish a newer controller capture. Overlay Escape continues to
use the production region-session cancellation path.

## Image and file authority

The response must contain a typed, bounded, absolute local file URI without
remote authority, credentials, query, fragment or NUL. Native worker decoding
uses the shared production `loadRaster` boundary, including encoded-file,
header/dimension and pixel limits. Cancellation is checked before, during file
reading and after decode; the GUI never waits on the worker to cancel. Worker
state owns its data independently and discards cancelled output. Decoding
does not resample or apply a capture colour conversion implicitly.

URI access is a grant to read an image, not proof that XerahS owns the supplied
pathname. The client therefore does not delete or overwrite portal-returned
files. It creates no screenshot file/cache of its own before user confirmation;
temporary encoded bytes remain worker-local and only a successful immutable
image reaches the controller. The portal's file/document lifecycle is external
authority and needs privacy-policy qualification. Removing a file merely
because its URI was supplied would risk unrelated user data.

## Confirmed pixel composition

Confirmed bounds stay in the production session's `confirming` state while
a native worker invokes the Qt Core compositor. The worker owns an immutable
frozen source, topology and cancellation/progress state; it never accesses the
controller, overlays, clipboard, files or widgets. Source copying, pixel-format
preparation and final image conversion happen there. Overlays share the frozen
image and paint their source subrectangles, avoiding per-display pixel copies
on the UI thread.

The compositor checks cancellation before allocation, during transparent-row
initialization, around each source read, during each copied row and before
publication. Cancellation discards all partial output and source-result
metadata. Output extent checks precede signed subtraction and byte-size
arithmetic. The current native raster ceiling (100,000 pixels per dimension,
268,435,456 pixels total) is also applied to composition; excess output fails
explicitly without resampling. Complete resource/performance qualification
and the final configured privacy policy remain Required.

Known row work supplies 0..100 progress to the overlay and command window.
Native format/copy operations cannot be interrupted internally; cancellation
is checked around them and the GUI never waits for their worker. Escape or
Cancel Capture cancels while confirming without a capture result, last-region
update, file or clipboard action. Late cancelled results cannot publish into
a new session; controller destruction cancels and releases the UI without
joining the worker. Existing colour metadata survives preparation; this is
not a qualified HDR/wide-gamut workflow conversion.

Native PNG saving encodes once into a private same-filesystem temporary file
(mode 0600), flushes its bytes, and publishes with Linux `RENAME_NOREPLACE`.
Existing destinations, including dangling symlinks and racing writers, are
preserved. Random name collisions retry without re-encoding; failures remove
only the owned temporary file on ordinary failure/exit. A process crash can
leave an owner-private encoded temporary file; durable journal-based recovery
and policy-governed cleanup remain Required. Unsupported atomic-publication mechanisms fail
explicitly. The native save artifact records its absolute path, media type,
actual byte length and immutable naming context. These values are artifact data,
not ordinary diagnostics. This does not provide the durable pipeline journal
and restart/recovery behavior required by PCA-012. The filesystem/native target
matrix and human privacy/security review remain Required.

## Native post-capture execution

Pixel completion is a selection terminal state, not action completion. The
controller stays busy while the same production Core pipeline runs on a native
worker. It owns immutable confirmed media, selected actions, naming settings,
unique task identity and cancellation state. PNG encoding/publication and the
Core action engine do not block the GUI. Selection events no longer change
acquired bounds or publish another image after pixel completion; Escape in
the identified action phase requests pipeline cancellation under PCA-010. The overlay and command window expose
accessible action activity; PNG encoding has indeterminate progress because
its native codec does not provide a known-work progress callback.

Escape or Cancel Capture reaches the pipeline token. Under PCA-010, an already
running uninterruptible save can finish and retain its file; its result records
that cancellation arrived during it. Remaining actions are cancelled. Clipboard
requests are claimed on the Qt GUI thread while the overlay/focus is retained.
Unclaimed cancelled requests cannot mutate the clipboard; a claimed action
finishes with its actual outcome. Destruction releases the GUI without joining
an encoding worker and wakes pending clipboard waits. Ordinary failures keep
independent actions eligible and expose stable value-free diagnostics.

A native receipt contains ordered Core results, confirmed working media and
actual save metadata. The controller retains its latest receipt in memory until
a later receipt replaces it or the controller/application closes. This preserves
working media and completed files after output failure; it does not provide a
complete recovery UI, configured retention/discard policy, external canonical
serialization or PCA-012 durable journal. These remain Required alongside the
remaining action catalogue, configured workflows, privacy/security review and
physical clipboard/input-serial/compositor/performance qualification.

## Evidence and required reviews

`PortalScreenshotTest` invokes the production transport and controller on a
private bus with no service-activation directories. PNGs, output paths and the
offscreen clipboard are fixtures. Tests cover early/legacy response races,
owner/sender/path authority, single-use results, native cancellation/Close,
late-handle cleanup, transport timeout versus user interaction, denial,
unavailable sources, corrupt/oversized input, unchanged returned files and
actual overlay Escape. They do not query the host's capture portal or acquire
real desktop pixels.

Composition tests pause the actual production compositor at deterministic row
checkpoints on that private bus. They verify GUI responsiveness, Escape before
worker start/midway/at the final row, unchanged fixture clipboard/files, late
old-worker isolation, non-blocking destruction and one successful pipeline
with a correctly sized temporary PNG. Core/native pixel tests cover exact
placement, transparent gaps, metadata/ownership, bounded extents, exceptions,
monotone progress and discarded partial pixels.

Native mixed-scale sources, display identity/rotation/colour/availability,
cursor options, HDR/wide-gamut conversion, last-region persistence/invocation,
complete overlay controls, cancellation/progress during subsequent pipeline
work, performance, physical compositor and
assistive-technology qualification remain Required. Permission-settings
navigation where available, portal-owned file retention/revocation and human
capture-permission/privacy/security approval also remain Required.
The [source-topology review](SOURCE-TOPOLOGY-REVIEW.md) records the remaining
physical coordinate, identity and frozen multi-source authority evidence.

Protocol references: [Screenshot](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html),
[Request](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html)
and [request lifecycle](https://flatpak.github.io/xdg-desktop-portal/docs/requests.html).

Atomic publication reference: [Linux renameat2](https://man7.org/linux/man-pages/man2/rename.2.html).

`CapturePipelineTest` uses offscreen native clipboard fixtures and real private
PNG publication to verify responsive cancellation, GUI dispatch, isolated
concurrent tasks, failure fallbacks, true claimed-action outcomes, non-blocking
destruction and retained media/artifacts. The private portal controller test
presses Escape on the actual overlay while saving and verifies the completed
PNG, cancelled clipboard and unchanged borrowed source.
