# Native editor save and companion repair

Candidate evidence for ES-008/011/012/013/014/015/027 and
CORE-004/007/027, with related partial EO-001/003/005/006/013/014/015/016
behavior. This is a PNG checkpoint, not completion of the output capability,
an accepted security policy or a release approval.

## User-visible behavior

Editor Save and Save As freeze the current owned source, document and history
state, then render, encode and persist on an owned worker. The rendered PNG
includes all represented visible annotations; separate backgrounds remain
unimplemented and cannot be claimed included. The canvas, document actions,
menus and output shortcuts are busy until acknowledgement. A named native
progress dialog reports rendering, encoding, PNG publication and annotation
persistence. Its Cancel button and rejection request cooperative cancellation;
an uninterruptible encoder finishes before the next safe boundary. Closing or
replacing the document cannot race that save.

Save uses the current path; untitled Save and Save As use a Qt Widgets picker
with PNG suffix and normal overwrite confirmation. The picker has an accessible
name and avoids the local GTK theme's blocking picker path. Only PNG is currently
offered. A non-PNG destination is explicitly refused before encoding, preserving
the existing file. Complete PNG/JPEG/WebP options under EO-004 remain Required.
Save As adopts its path and records a recent image only after full persistence;
partial publication keeps the previous durable path.

A full save acknowledges the exported history state rather than whatever state
is current when a worker returns. Undo to that state is clean; subsequent edits
or an unreachable truncated saved state stay dirty. PNG success with companion
failure invalidates the clean checkpoint, including an unchanged document.
The failure dialog names the actual local destination, PNG format and failed
artifact using stable diagnostics, explains retained data and leaves the editor
open. Path text is plain text and is not telemetry.

**File → Retry annotation save…** is available for the current live partial
receipt's history state. Its confirmation defaults to Cancel and explains that
the PNG will be kept. Retry validates the original publication and repairs only
the companion, without rendering, encoding or opening a picker. Changing the
document disables that repair; Undo to the saved state enables it again without
truncating Redo. A stale confirmation cannot authorize a different receipt.
Ordinary Save remains an explicit new snapshot save. A successful repair of
partial Save As adopts that chosen path only after the companion succeeds.

## Native I/O and ownership

`EditorSave.cpp` replaces the synchronous QSaveFile/read-all path with native
descriptor-bound publication. It preserves the kernel's interpretation of
parent symlinks and `..`, opens the chosen directory, and binds its device/inode.
Same-directory random temporaries use exclusive no-follow creation and 0600
permissions. Output bytes are flushed/fsynced before publication. New targets
use `RENAME_NOREPLACE`; replacement requires the observed owned, regular,
single-link destination's identity to still match. Symlink, directory,
multilink and foreign-owner leaf destinations are refused. Cleanup removes
only the temporary name still bound to the writer's own descriptor.

SHA-256 streams the encoded descriptor in 64 KiB chunks before publication,
checks identity before/after and bounds encoded inputs to 1 GiB. Repair also
checks the PNG's directory, device/inode/length/times/owner/mode and hash.
Missing or changed PNGs cannot authorize companion creation or recreation of
the raster. Both acknowledged publication facts survive cancellation or a
completion observer exception; cancelling before publication preserves the old
destination, while cancelling after PNG publication retains an incomplete pair.

The existing Core `saveSidecar` and `.xann` codec remain the production policy
and compatibility seam. A required companion embeds the original unannotated
source and binds to the actual encoded PNG hash. Explicit editor Save supplies
sidecar-save intent. A prior default companion can be replaced or removed only
when its live identity matches and its bounded contents parse as a supported
document. Unsupported future-major, damaged, arbitrary, changed and symlink
companions remain preserved. Empty documents return no sidecar path and remove
only that compatible default companion, keeping the raster. Baseline alternate
name observation and complete shared consent/compatibility policy remain Required.

`EditorSaveProgress.cpp` owns value copies of path/document/hooks and a Qt
copy-on-write image that owns its pixels. The worker has no references to a
window, session or GUI stack. Synchronous encoder/publication hooks borrow
their arguments only for the worker call; they must not call GUI APIs. A false
encoder result or exception fails before its publication and exposes no private
exception payload. Progress observers cannot retract acknowledged artifacts.
`EditorWindow::saveFinished` runs on the GUI after checkpoint/receipt recording
while Save is busy; reentrant Save is ignored and deletion of the owner is
guarded, including during busy-state restoration. Parent destruction requests
cancellation and releases the modal bridge without joining the worker; remaining
work owns its data and cleans its temporary at the next boundary.

## Verified evidence and review limits

- `EditorSaveTest` exercises real PNG/companion reopening and hash/source
  binding, concurrent caller mutation, encoder exception, racing replacement,
  sparse oversized temporary rejection, cancellation before/after PNG, repeated
  companion repair, changed-raster rejection, explicit compatible removal,
  future-major preservation, symlinks/parent segments, completion exception and
  parent destruction without joining or late publication.
- `EditorWindowTest` exercises actual progress/Cancel controls and responsive
  worker gating, disabled and reentrant actions, retained dirty state, the
  default-Cancel repair confirmation, Undo/Redo eligibility, successful repair
  without another encode, failed Save As's unchanged current path, local failure
  disclosure, owner deletion during work/completion/restoration and non-PNG
  refusal. Existing dirty replacement/close and complete/incomplete recents
  assertions remain. The new progress stage required waiting for the actual
  failure box; fixture cancellation clicks the real Cancel button because the
  programmatic QProgressDialog `cancel()` slot does not emit user cancellation.
- `EditorSessionTest` checks acknowledgements of an older immutable state,
  unchanged split-save dirtiness and an unreachable truncated checkpoint.

This checkpoint passed 40 CTest targets and all 114 existing published vectors,
contract lint/backlog and 32 tooling/runner unit tests. Affected native targets
also run with cached Qt 6.4.2. Exact pushed-SHA Ubuntu 24.04/Qt 6.4.2 Product
Contract CI supplies the authoritative baseline result. EO has no published
vector package yet; these native fixtures do not invent shared expected results
or claim full EO conformance.

The final identity check and existing-file rename/unlink still have a path-race
window; they are not a filesystem compare-and-swap guarantee. Directory fsync,
power-loss behavior, native disk-full/permission matrix, cleanup after process
death, application shutdown during an encoder and durable editor media recovery
remain Required. Temporaries/new outputs are private; replacements currently
also use 0600, and full permission/ACL/ownership policy needs human review.
The 1 GiB encoded limit rejects oversize after encoding and does not cap every
encoder write. Complete resource/performance, physical keyboard/accessibility,
supported distro/compositor, visual/compatibility, privacy/security and release
qualification remain Required. This adds no restart journal or reconstructed
retry authority for editor saves.

## Recorded format clarification and next work

EO-004-LINUX-2026-10-14 remains pending with the human product owner. The approved
specification requires the reachable PNG/JPEG/WebP set, but
`format-disposition.json` still declares draft selection and does not decide
default quality, WebP lossless selection, JPEG transparent-pixel flattening or
export profile/metadata policy. Concrete decisions requested are: confirm that
set; choose JPEG/WebP quality defaults and whether WebP exposes lossless;
require an explicitly chosen opaque JPEG background or approve a documented
default; and decide original-profile versus explicit export-profile handling.
For example, a half-transparent red pixel saved to JPEG over white or black
produces different opaque pixels, so a native encoder default cannot become
the shared expected behavior. Shared scenarios/vectors must record the decision.
This is a clarification, not a format waiver or approval of this PNG candidate.

Next implement explicit PNG/JPEG/WebP save options and compatible persistence
under EO-004/005/006 using the decided policies. Continue the specified format
surface and independent contract work while clarification is pending, keeping
all other domains in CONTRACT-AUDIT.md in scope.
