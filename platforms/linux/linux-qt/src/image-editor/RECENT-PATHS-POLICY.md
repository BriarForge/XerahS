# Native recent-image implementation policy

Implements the native selection portion of EC-002 and persisted recent data in
ESET-012. This checkpoint does not activate EDITOR-SETTINGS-001 or approve
shared settings metadata, migrations, privacy review or capability completion.

Recent paths default to off. Explicit opt-in starts an empty list and records
only subsequent successful editor openings and complete raster/sidecar saves.
New untitled images, failed/cancelled opening and incomplete/cancelled saves do
not add paths. A complete explicit Save may add its path even if a subsequent
replacement is cancelled. Newest paths come first, with a validated inclusive
limit of 1..100 and default 10. Reducing the limit removes oldest entries;
opting out clears every path. Individual removal and Clear All do not require
files to exist. Clearing does not disable opt-in or silently backfill old paths.

Linux paths are absolute and case-sensitive. Repeated separators and current
directory segments are normalized; parent-directory segments remain intact
because POSIX resolves them after following symlinks. Relative, empty, URI and
NUL-containing paths are rejected. Symlinks are not resolved:
the list retains the user's chosen absolute path, even when its target later
changes or disappears. A separately injected Windows path rule supports
case-insensitive drive/UNC deduplication, slash normalization and dot segments
bounded by the drive/share root. Its tests include the published Windows
deduplication fixture; this is not Windows edition qualification.

The editor and tray share the same live preference model. Menu selection opens
the explicit path through the production validation/progress/cancellation and
dirty confirmation flow. Missing files show the normal typed opening failure
and preserve both the current editor and the removable recent entry. Native
labels escape ampersands while retaining the literal path in action data.

Manage Recent Images edits a draft. Cancel leaves every active preference and
entry unchanged; Apply replaces them together. An incoming instance request
can open another image while the modal event loop runs. A changed model
revision prevents the older draft from overwriting those newer entries and
asks the user to reopen the controls. Preference changes never enter document
history or change source pixels, selection, dirty state, viewport or files.

Paths are private local data. The native application stores them in its
isolated XDG preference file with owner-only file/directory permissions and
bounded atomic replacement. The native controls describe on-disk retention.
The store keeps current and privacy-pruned previous preferences together;
Remove, Clear, trim and opt-out erase forgotten paths from both versions, so
recovery cannot reintroduce them. No reference Avalonia settings are imported,
and no recent paths enter telemetry/support output.

Missing preferences cause no startup write. Compatible unknown fields survive
a saved list change. Invalid fields recover independently; unsupported or
unreadable formats are preserved read-only and native startup warnings expose
recovery. Failed writes preserve the active list and file. An automatic recent
save failure is visible across editor refreshes without undoing successful
image Open/Save. Native tests use only temporary preference files, including
restart, actual image saves, failed Apply, incomplete sidecars and stale drafts.

The [preference policy](PREFERENCES-POLICY.md) records metadata and whole-file
recovery limitations. Complete ledger semantics, approved retention policy,
remaining settings, cross-platform conformance and human privacy/security/
compatibility qualification remain Required under ESET-001/002/003/015.
