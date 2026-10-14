# Empty save and baseline companion policy

Decision ID: ES-014-030-LINUX-2026-10-15
Status: pending product-owner decision; no shared contract/approval changed.
Requirements: ES-011/012/014/015/030 and EA-019/020. Disposition: Required.

ES-014 requires explicit empty-document save to remove the default companion
and return no companion path. ES-030 requires readers to look for a last-extension
replacement companion when the default is absent. The legacy file may be a
separately owned/shared companion; deleting or renaming it is not established by
the current default-only intent. Successful deletion persistence must not silently
reintroduce its annotations on reopen.

A temporary native production-path probe created a blue 40x30 PNG, a valid
source-bound baseline shot.xann with one rectangle, and a default shot.png.xann.
After guarded all-object deletion and explicit empty save, the pre-guard result
was raster_saved=true, sidecar_saved=true, default_exists=false,
legacy_exists=true, session_annotations=0, reopened_annotations=1,
pending_choice=false, diagnostic="". This demonstrates the conflict without
using a real user image, live settings or an external reference implementation.
The native baseline path is observed; baseline Avalonia runtime-convention and
full compatibility qualification remain Required before activation.

The candidate implementation now refuses companion completion with the existing
sidecar-save-failed diagnostic when explicit empty persistence would expose an
existing baseline alias. Both companions are preserved; an already published
PNG and owned zero-annotation document/receipt remain available, and the editor
stays dirty. Capture reports the failed companion with explicit retry evidence.
Editor sibling existence checks stay bound to the opened receipt directory, and
the default-removal boundary rechecks after publication observers for a late
alias. No legacy file is read, deleted, renamed, archived or rewritten by this
refusal. Nonempty and ordinary no-alias saves retain the current contract path.

Core tests cover explicit/no intent and default-present/absent cases. Native
editor tests cover both existing forms plus an alias created at the deletion
boundary, retaining raster/doc and exact companion bytes with honest failure.
Capture tests preserve an opaque alias and zero-annotation receipt with a known
failed companion and retry plan. Shared expected vectors remain unchanged.

The product-owner question is concrete: after explicit empty save, should the
application offer a previewed, confirmed archival/reconciliation of the baseline
companion with recovery/rollback, or preserve every baseline companion and refuse
editable completion? Any archive/reconciliation needs ownership/source binding,
collision/resource handling, symlink/permission/race policy, cancellation,
rollback, cross-platform semantics and human privacy/security/compatibility
review. A tombstone would need a shared format/path decision; writing an empty
default document would conflict with ES-014 as currently written. Neither is
self-approved here.

The question was submitted to the user on 15 October 2026. An ordinary continuation
is not approval. Until resolved, keep the conservative refusal, keep this gate
Required, and continue independent authorized native work.
