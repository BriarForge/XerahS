# Per-profile capture output — native candidate evidence

Date: 15 October 2026. Extends [saved workflows](SAVED-WORKFLOWS.md) using
FILENAME-GENERATION-001 and the existing PCA Save engine.

## User behavior

**Capture workflows…** now edits each profile's **Save folder** and **Filename
pattern**. Browse chooses a folder; typing an absolute folder also works. Blank
folder inherits the standard capture folder. Save changes persists the entire
profile atomically; closing or cancelling Browse keeps the saved values.
**Open captures folder** uses the saved default profile's folder.

The read-only example uses the production filename preview with fixed
2 January 2026 15:04:05 / 1920×1080 context and deterministic preview randomness.
It exposes token descriptions, sanitized PNG output and expansion errors with
token and zero-based Unicode-scalar offset. Preview creates no directory, reads
no random file and commits no counter. Actual Save uses its own immutable local
time/locale/image-size context and cryptographic randomness. PNG is appended
automatically. Unknown tokens and literal `%%` retain the existing engine rules.
Window/user/host metadata is currently unavailable in capture naming context.

Invalid, relative or traversal folders and malformed/oversized patterns cannot
be saved or run. Native profiles visibly refuse counters until FN-016 atomic
reservation/persistence is implemented; random-file tokens have no file-read
grant. These are partial native limitations, not accepted contract dispositions.
Blank patterns retain the contracted `_.png` result. Save As keeps its separate
explicit new-PNG picker. Fixed-name collisions preserve existing files and report
failure; random-name collisions retain the existing bounded retry behavior.

The controller freezes effective folder and pattern before portal acquisition.
Later edits affect the next capture. Retained-action retry keeps the original
folder/pattern after profile changes. Result/history metadata includes only known
output fields; nested unknown extension payloads are excluded. Names, folders
and patterns remain private data exposed only in explicit settings/result review.

## Version and authority

The private native format now writes version 2 with profile `output.directory`
and `output.pattern`. Version 1 reads with unchanged inherited-folder/default-name
semantics; only explicit Save upgrades it. Compatible root/profile/output unknown
fields round-trip. A formerly unknown v1 `output` field requires explicit
migration and leaves the file unchanged; it receives no silent execution meaning.
Older v1-only native binaries refuse version 2 rather than ignore its destination.
The 1 MiB/64-profile bounds, private atomic storage and stale-write checks remain.
No Avalonia or shared interchange migration is performed.

## Verification and remaining work

Core tests verify deterministic preview/sanitization/unknown/percent/blank rules,
typed errors/scalar offsets, invalid folders/patterns, non-committing validation,
v1 explicit upgrade and conflict refusal, nested unknown preservation and report
filtering. Native controls verify persisted output across restart/Run, real PNG
names/pixels, failed draft correction, Browse Accept/Cancel, original-setting
retry and collision/unavailable-folder failure with independent clipboard success.
Private-portal cases verify both default/explicit acquisition snapshots, the next
profile's actual output and refusal before acquisition. Invalid native Save
patterns now fail before directory creation or encoding. Synthetic UI inspected.

The initial product change passes 49/49 locally (70.88 s) and cached Qt 6.4.2
(67.68 s). Its Ubuntu CI 38743958392 passed the workflow tests and conformance,
but the existing aggregate editor-window class exceeded its 30 s deadline.
That class now discovers its actual 103 Qt test slots and distributes them over
three private-bus runs, each keeping the 30 s deadline. Comparing full completed
output proves all 323 prior behavior cases still pass exactly once on both Qt
versions; no case/assertion was removed. Manual unpartitioned invocation remains.
Final full CTest passes **51/51 locally (70.70 s)** and **51/51 cached Qt 6.4.2
(67.52 s)**, including all 114 existing published vectors. The three editor runs
take 7.57/12.41/4.85 s locally, 7.87/12.08/4.60 s minimum. All 32 tooling tests
pass. Exact pushed CI is reported with the checkpoint. Shared expectations and
all per-process deadlines stay intact.
Next implement explicit workflow-ID activation/CLI through the same dispatcher
and controller, then global entry points and atomic counter reservation. Complete
token-help/context/grant coverage, workflow/action families, target matrix and
human security/privacy/accessibility/parity/release qualification remain Required.
