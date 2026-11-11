# Legacy JSON history import and recovery

In Capture History, choose **Legacy history…**. The window opens saved imported
history. **Choose JSON history…** loads a separate, immutable review snapshot;
**Import reviewed snapshot…** shows record/quarantine counts and defaults to
Cancel. Accept saves that exact snapshot privately. The source file stays
unchanged, including a concurrent replacement after review. **Saved history**
reloads the durable archive; closing and reopening the app preserves imports.

**Undo last import…** removes that snapshot from the visible archive while
preserving other imports and its private recovery bytes. **Review recovery
snapshot** loads the last undone snapshot for review and explicit reimport,
including when the original file is gone. **Remove recovery snapshot…** requires
another default-Cancel confirmation and removes only an undone backup. Source
files and active imports are untouched. This is ordinary local data removal,
not a secure-erasure guarantee; no automatic backup cleanup is performed.

The window shows file name, recorded timestamp, type, host, local reference and
URL in pages of 200 records. Saved rows show their snapshot fingerprint and
original record ordinal; the full fingerprint is available in the row tooltip.
Paths and URLs remain text: importing does not load media, follow links, upload,
delete source files, create pipeline receipts or grant capture/retry authority.
Imported history is separate from native capture results.

## Source format and identity

The reader recognizes the pinned KovaForge 0.29.0 JSON history representation:
objects separated by commas without outer array brackets. An empty source is
an empty history. XML and SQLite are not supported by this adapter yet.

The candidate preserves all recognized fields, compatible unknown metadata,
source ordering and record ordinals. Missing-media references remain visible.
Tags and secondary/deletion URLs are retained in the private original snapshot
but are not displayed or activated. Known tags include window/process metadata,
annotation sidecar references, Favorite, Tag and Errors.

The reference JSON serializer excludes `Id` and supplies no operation-state
field. Import does not invent legacy task IDs or success states. Timestamp text,
including offsets or an unspecified timezone, remains unchanged.

A snapshot ID is SHA-256 of its exact reviewed bytes. Retrying the same snapshot
is a no-op and does not rewrite the archive. Distinct snapshots are kept in full,
even when records look identical: missing task IDs cannot establish that they
are the same task. Reformatting a file therefore creates a distinct snapshot.
The confirmation discloses this rule. Cross-format task identity resolution
remains an open compatibility qualification item.

## Private storage and publication

`LegacyHistoryArchive` is a Qt Core service backed by PrivateJsonFile. It writes
`$XDG_STATE_HOME/com.xerahs.app/legacy-history.json` (the normal XDG fallback
applies), with owner-only permissions and the versioned identity
`com.xerahs.app/linux-qt/legacy-history`, version 1. Each import retains its
fingerprint, `sourceFormat: legacy-json-fragments`, `sourceVersion: unversioned`,
UTC import time, active/undone state and exact original bytes as base64. This
backup also retains quarantined records; it is not an ordinary export or a
credential store and never enables imported authority.

Import, undo and recovery removal each publish one atomic document. Expected
bytes prevent one window overwriting another window's changes. A stale review
must reload Saved history before retrying. Cancel before publication writes
nothing; a commit already published remains available after restart. Close
abandons worker delivery. Malformed, future-version, changed or locked archives
are never silently overwritten or repaired.

The selected source is opened nonblocking and checked as a regular file through
its opened descriptor. The worker reads at most 1 MiB plus one overflow byte.
Source JSON preflight limits nesting to 64 and lexical elements to 100,000; the
private archive has a 16 MiB byte ceiling and the shared store's JSON limits.
File I/O, parsing and archive operations run through OwnedWork.

Semantically malformed records and unknown fields caught by the existing unsafe
name classifier remain in the private original snapshot. Valid neighbors remain
ordered and readable. Review reports contain counts, ordinals and fixed reasons
only, with at most ten findings shown. The classifier is conservative, not a
general secret detector. Invalid syntax, duplicate keys or encoding block the
source preview explicitly. Choosing Cancel keeps the prior view.

## Evidence and remaining scope

`LegacyHistoryArchiveTest` covers restart, exact backups, quarantine, repeated
identical records, exact retries, distinct overlapping snapshots, undo/reimport,
explicit recovery removal, stale/locked/cancelled writes and corrupt/future stores.
`LegacyHistoryTest` drives native Choose, default-Cancel/Yes confirmation,
concurrent source replacement, import/restart, undo without affecting another
import, recovery after source removal, backup removal, stale-write reload/retry,
paging, chooser cancellation/destruction and pending-read close. A FIFO fixture
proves special files cannot stall the reader. CaptureHistoryWindowTest drives
the actual entry point and its owner lifetime.

Fixture provenance is in `tests/fixtures/legacy-history/provenance.json`. It is
sanitized and source-shaped, not a baseline runtime observation or an accepted
shared golden. Source evidence is EXTRACTED from HistoryManagerJSON.Load/Append
and HistoryItem at the pinned commit.

COMPCAT-009 and COMPCAT-017 remain partial: XML/SQLite, cross-format identity,
full source state/privacy mappings, all migration families, baseline runtime
observation and cross-platform qualification are still required. COMPCAT-010
is unchanged. No full compatibility, security or release approval is implied.
