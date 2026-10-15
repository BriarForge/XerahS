# Native launch and activation

This is a reviewable Linux implementation of CORE-006 and part of CORE-007,
CORE-024 and CORE-027, not an approved public CLI/MCP schema or release gate.
The Product Contract remains authoritative. Human security, privacy,
compatibility and accessibility review, shared conformance and target-matrix
qualification remain Required.

## Instance ownership

The ordinary native application uses `com.xerahs.app` under Qt's per-user
RuntimeLocation (normally `$XDG_RUNTIME_DIR`). Both that root and the application
directory must be owned by the effective user with no group/other permissions.
The final directory and lock components are opened without following symlinks.
The lock must be an owner-only, empty regular file with exactly one hard link.
The application never imports or initializes reference Avalonia configuration.

A kernel `flock` lease remains held throughout the primary process lifetime.
It cannot expire while an idle primary is still alive, and the kernel releases
it after a crash. The lock file is never removed to guess stale ownership.
Only the lease owner may remove an owned stale socket after a connection probe
returns refused or not found. Regular files, symlinks, live endpoints and
uncertain probe results are preserved and produce an actionable error.

A secondary never becomes a primary after rejection, busy, failed connection
or unknown delivery. It authenticates the endpoint through kernel `SO_PEERCRED`
before sending a private path. The primary authenticates incoming peers the
same way. Unknown or different effective-user credentials fail closed. These
checks do not sandbox malicious processes already running as the same user.

Production currently addresses the default profile. The injectable runtime
directory demonstrates isolated ownership in tests; complete shared user
profile selection and persistence integration remain Required under CORE-012.

## Private candidate protocol

One connection carries one newline-terminated UTF-8 JSON request and a correlated
reply. Request JSON is at most 65,536 bytes, excluding its newline; a reply is
at most 4,096 bytes. Intake and delivery have a two-second deadline. At most
16 incoming clients are active. Partial clients time out and release capacity.
The native application and tests use the same Qt Core validator and dispatcher.

Requests have integer version `1`, a canonical non-null UUID `id` and one
`action`: `activate`, `capture-region`, `edit-image`, `run-workflow` or
`capture-region-choose-actions`. Only `edit-image` accepts
a `path`, which must be absolute and contain no NUL. Unicode, whitespace and
newlines in valid filenames are preserved. Unknown fields/operations, duplicate
JSON keys (including escaped duplicates), invalid types, malformed UTF-8 and
oversized input are rejected. A caller cannot assert consent through payload
fields. A valid file path is an opening request; it does not bypass the editor's
normal source validation and native consent boundaries.

`run-workflow` requires only a canonical non-null `workflow_id` UUID. Its saved
definition is validated and frozen by Core resolution during admission; requests
cannot override actions, output settings or consent. Choose-actions uses a
distinct reserved operation and releases it through ordinary dispatch. Older
instances reject the new action names. See [workflow CLI](WORKFLOW-CLI.md) for
flags, exit codes, safe versioned delivery JSON and pre-GUI headless refusal.

The primary returns `accepted`, `busy` or `rejected`, with the UUID and a stable,
value-free diagnostic for unsuccessful admission. Accepted means delivered and
admitted, not capture/editor/output success. Actual operation results still
come from their production services. Startup has no admission handler until
the application services are ready, and therefore returns busy.

The dispatcher reserves one pending non-activate operation before deferred
dispatch. Nested editor interaction, an active modal dialog, and a busy capture
reject conflicting requests; simple activation can still expose the window
and bring an existing consent dialog forward. Availability is rechecked at
dispatch. A late conflict/failure is shown in the native command window rather
than fabricated as operation success. Complete approved workflow concurrency,
progress/cancellation and public automation semantics remain Required.

The primary retains 256 accepted/rejected receipts for its lifetime, storing
only an operation fingerprint and reply, not a private path. A repeated UUID
with the same canonical request replays that receipt without dispatching again;
altering its payload is rejected. Busy requests are not cached. This is bounded
process-local duplicate protection, not durable exactly-once execution. A missing,
malformed, excessive, partial or mismatched acknowledgement returns unknown
delivery; the client never resends or re-elects automatically.

## Native shell and evidence

Plain desktop launch shows/restores the native XerahS command window. Its
buttons and menus reuse the actual tray actions and capture availability.
Tray/window region actions, repeated-launch region requests and image-opening
requests reach the same production activation dispatcher. The window preserves
plain accessible capture result text after a notification disappears. Without
a tray host, the visible native window supplies the current commands and normal
last-window exit behavior. This is an application shell subset: onboarding,
history/settings chrome, complete workflow surfaces, global shortcuts,
command palette, localization and native notification authority remain Required.
[Session global shortcuts](WORKFLOW-SHORTCUTS.md) now feed explicit workflow-ID
activation into this dispatcher. Pending desktop setup blocks conflicting capture
admission; failed/busy presses are not queued or repeated on key hold. Persisted
opt-in, all shortcut families and physical desktop/human qualification remain Required.

`ActivationProtocolTest` exercises production validation, reservation,
reentrancy, changed availability and redacted exceptions. `NativeInstanceTest`
uses temporary private runtime directories and actual child processes for
ownership, authentication, recovery, bounded intake, receipt replay and delivery
failures. Its actual-application tests forward only to fixture peers with
temporary XDG paths, an unavailable private bus and the offscreen Qt platform;
they perform no capture, image opening, upload or preference writes.
`ApplicationWindowTest` uses actual actions, keyboard and accessible controls,
modal preservation, visible result text and minimized/maximized restoration.

CORE-PLATFORM-001 has no published `test-vectors.json` yet. CMake reports that
gap explicitly and still runs all published vectors of the six existing seams.
No expected vector is weakened or invented to accept this checkpoint.
Physical GNOME/Plasma/wlroots behavior, all target distributions, screen-lock
conditions, profile integration, assistive technology, cross-platform parity
and the required human reviews remain unverified.
