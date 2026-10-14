# Native annotation duplication and object clipboard

Partial native candidate evidence for EA-003/016/017/018/019/020 and related
ES-004/005/006/007/015/017/019/027/028, ESET-010/016 and CORE-004/007/027.
Every affected requirement retains a Required disposition. Full annotation
conformance, all 20 tools, compatible assets, native targets and human gates
are unfinished. [Candidate requirement map](ANNOTATION-TRANSFER-EVIDENCE.json)
records the slice without registering a nonexistent shared EA vector package.

## Plan and implemented behavior

Continue the verified Print/Copy/Save checkpoint with one Core transfer path:
project an explicitly selected supported object batch, validate before any
insertion/deletion, assign fresh identities, retain source/document data, and
commit exactly one history operation. Native Qt menus, shortcut dispatch and
clipboard I/O call that production path. The document parser and transfer parser
share annotation field validation and serialization; the conformance adapter
retains its existing production calls and no new expected vectors are invented.

**Edit → Duplicate Annotations**, Ctrl+D, duplicates currently represented
visible, unlocked rectangles in document order. New objects retain bounds,
stroke/fill/width/rotation/opacity and compatible extra fields, receive fresh
non-colliding UUIDs, and become the selected topmost batch. Source pixels/profile,
document identity/metadata and all existing embedded assets stay intact. A single
Undo restores the preceding document and its actual selection; Redo restores the
same new IDs. Insertion reserves history before changing selection/Redo/checkpoint.
Failure/stale state/invalid ID/geometry/resource limits leave this operation's
document, selection, checkpoint and Redo unchanged.

**Copy Annotations**, Ctrl+Shift+C, offers only the supported selected objects
and their compatible fields using a private native MIME representation. Document
paths/hash/source pixels and top-level metadata are excluded. Object extra fields
are retained as inert data and may contain user content; they are never executed
or resolved as external resources. **Cut Annotations**, Ctrl+X, publishes that
same owned batch first and removes the unchanged selection only after a known
clipboard acknowledgement. Ownership uncertainty, foreign replacement, callback
exception or malformed acknowledgement keeps the selection. If publication is
acknowledged but removal fails, the UI reports a partial result and keeps the
clipboard's acknowledged copy rather than repeating or reverting an effect.

**Paste Annotations**, Ctrl+V, reads owned bytes only after explicit action,
validates the complete batch and inserts it atomically with fresh UUIDs. Repeated
pastes of identical bytes advance the offset. Native duplication/paste offsets
provide at least 10 logical viewport pixels on each axis, with a minimum of one
source pixel; the Core API accepts the explicit offset. The candidate geometry
policy permits out-of-canvas rectangles as the existing editor does; renderer
clipping is unchanged. Complete placement/edge/zoom/accessibility qualification
and shared offset policy remain Required, rather than being inferred from a
coordinate difference alone.

Copy does not change dirty state/history/selection. Cut, Paste and Duplicate
create one history operation each. None saves, closes, changes recents/defaults,
modifies source or image/sidecar files, or uploads data. Successful explicit Save
continues using the previously verified immutable PNG/sidecar path. Source/asset
and new-object identity preservation is tested through actual save/reopen.

Hidden or explicitly locked compatible objects are protected across direct
selected move/resize/rotation/style/order/delete and transfer commands. Opaque
non-boolean `locked` fields fail closed. A mixed protected selection is refused
as a whole. Locked pointer selection produces no move preview; native Delete and
Rotate are disabled with an explanation. Creation defaults can still change
without mutating those objects. Global canvas transformations/clear and full lock
field/selection UI semantics remain separate Required qualification.

The four new commands are hidden in the unchanged 14-command default toolbar,
bringing current registered commands to 43. Fixed menus/protected shortcuts remain
available with an empty toolbar. Ctrl+C remains Copy Image. Local text fields,
modal/popup state, active gestures and busy operations retain input precedence;
commands do not auto-repeat. Missing read/publish callbacks disable the relevant
commands and explain service unavailability. Duplicate remains independent of
clipboard availability.

## Native boundary, format and resource safeguards

`AnnotationClipboardPorts` declares GUI-only publication/read callbacks, borrowed
publication bytes and owned read results. Retained values require owned copies.
Owner deletion is allowed; callbacks are frozen for an attempt and cannot grant
an implicit fallback. Reentrant ordinary document/output actions are refused by
the existing output busy guard. Completion is emitted after the acknowledged
copy or committed edit, while still busy. Observer exceptions are contained
without repeating the transfer; deletion during publication/read/completion
cannot cause late editor access.

The default native provider transfers owned QMimeData to Qt and requires both
unchanged offered-object identity and current clipboard ownership to acknowledge
publication, as in the verified Copy Image boundary. It never clears/restores
foreign contents. Failed/Cancelled host replies promise no effect. Exceptions,
invalid states, selection/artifact overrides or explicit uncertainty become stable
`annotation-clipboard-delivery-unknown`; private callback diagnostics are redacted.
The offscreen Qt backend's readable offer may correctly remain unknown. Controlled
host-positive fixtures model acknowledgement while recording actual temporary
MIME data; they are not physical clipboard acceptance.

The local candidate MIME is `application/x-com-xerahs-linux-qt-annotations+json`.
Version 1 contains exactly `version` and `annotations` and requires compact
canonical Qt JSON, excluding duplicate keys and ambiguous alternate encodings.
The decoder accepts only the currently represented rectangle type and rejects the
entire batch on unknown/malformed/protected objects. It applies a 4 MiB encoded
cap, conservative 4 MiB DOM/encoding budget, 1,024 objects, depth 32, 100,000 nodes,
finite positive-area geometry within ±100,000 source coordinates and a 100,000
object destination insertion limit. The bounded preflight checks metadata before
encoding; no source PNG is encoded for clipboard use. Native Qt retrieves complete
MIME bytes before the application can check their size; Qt transport allocation,
blocking/native owner behavior and full shutdown/resource policy remain Required.
These are candidate safety bounds, not approved shared compatibility limits.

No external resource loading, compressed document inflation, pixel decoding,
script execution, telemetry, automatic paste/retry or durable clipboard authority
is introduced. Invalid payloads leave the clipboard and editor unchanged.

## Concrete product-owner clarification

**EA-016-LINUX-2026-10-14** remains pending. The approved specification requires
object/asset preservation but supplies no shared clipboard envelope, selected
asset-reference schema or offset/resource policy. Example: a compatible rectangle
extra field references `stamp`, while `embeddedImages` also contains an unrelated
private image. Copying every image would disclose unrelated data; excluding all
images could break the compatible reference. The current native clipboard refuses
asset-bearing documents rather than choosing either lossy policy. Local Duplicate
retains the document's complete asset map without disclosure or removal.

The product owner/shared conformance owner must decide the interoperable MIME and
version/canonicalization rules, recognized asset references/projection/key collision
handling, compatible unknown-field behavior, lock field semantics, offset and
resource policy, then publish deciding schema/text/scenarios/vectors. All 19 other
annotation tools/assets and their transfer outcomes remain Required. This candidate
private transport is not accepted cross-platform interchange or a waiver.
The earlier EO-004 format-policy question remains pending independently.

## Verified evidence and remaining scope

`AnnotationTransferTest` observes selected order and field round-trip, omitted
source/document data, assets retained by local duplication, fresh IDs and exact
history/selection/checkpoint/Redo behavior, cross-document insertion/source/default
preservation, hidden/locked/unknown/empty/asset projection refusal, corrupt/future
shape/duplicate-ID/null-ID/zero-width/foreign-key/duplicate-key/deep/oversized input,
metadata/batch/envelope boundary limits, collision/invalid generator/overflow/stale failures and
unchanged state. It tests production Core calls without pixels or native I/O.

`EditorWindowTest` exercises actual menus and Ctrl+Shift+C/Ctrl+V/Ctrl+X/Ctrl+D,
real private MIME bytes, sequential paste/new IDs, zoomed-out viewport displacement,
Undo/Redo/branch behavior, unchanged source/files/preferences/viewport before Save,
actual PNG/sidecar reopen, asset-retaining duplication and asset clipboard refusal,
protected native selection/movement/delete/rotate, local text Cut/Paste, missing
providers, responsive busy/reentrant guards, failures/unknown outcomes and redacted
diagnostics, real ownership/foreign replacement and owner/completion lifetime.
Existing Save/Copy/Print fixtures regress the common busy and session boundaries.
Toolbar tests retain defaults and protected shortcut ownership for all 43 commands.

The checkpoint passes all 43 local CTest targets and the existing 114 published
vectors, contract lint/backlog and 32 tooling/runner unit tests. The six affected
Core/editor/toolbar/conformance targets also pass cached Qt 6.4.2. Exact pushed-SHA
Ubuntu 24.04/Qt 6.4.2 CI supplies the authoritative baseline result. All native
fixtures use private buses, temporary settings/files and a private unavailable
CUPS socket; no real capture, upload or printer is run and live Avalonia settings
stay untouched. These fixtures do not accept shared EA conformance or human gates.

Complete annotation tools/fields/asset clipboard, compatible shared protocol,
selection/lock controls and semantics, physical GNOME/Plasma/wlroots Wayland/X11
clipboard owners/targets/manager/app-exit behavior, native resource/performance,
geometry/colour/goldens, accessibility and human privacy/security/parity/release
reviews remain Required. Next implement native Ellipse's represented document,
creation/selection/history and rendering under EA-005, while resolving transfer
and export policies and retaining every domain in CONTRACT-AUDIT.md.
