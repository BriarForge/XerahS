# Read-only legacy JSON history preview

In Capture History, choose **Legacy history…**, then **Choose JSON history…**.
The native window shows file name, recorded timestamp, type, host, local reference
and URL in pages of 200 records. Close leaves the source and live history unchanged.
Paths and URLs are plain text: preview does not load media, follow links, upload,
delete, import into live history or copy values to the clipboard.

The reader recognizes the pinned KovaForge 0.29.0 JSON history representation:
objects separated by commas without outer array brackets. An empty source is an
empty history. XML and SQLite are not supported by this preview.

## Data and failure behavior

The candidate retains all recognized fields, compatible unknown metadata, source
ordering and original record ordinals. Existing missing-media references remain
visible. Tags and secondary/deletion URLs are retained in the in-memory candidate
but are not displayed or activated. Known tags include window/process metadata,
annotation sidecar references, Favorite, Tag and Errors.

The reference JSON serializer excludes `Id` and supplies no operation-state field.
The preview discloses this absence rather than inventing task identity or success.
Timestamp text, including offsets or an unspecified timezone, remains unchanged;
preview does not migrate or reinterpret the timestamp.

Semantically malformed records and unknown fields caught by the existing unsafe
name classifier are set aside in memory with their original values and ordinal.
Valid neighbors remain ordered and readable. The report uses counts, ordinals
and fixed reasons only; the window shows at most ten record findings. This is
the existing conservative classifier, not a general secret detector. Invalid
file syntax, duplicate keys or encoding block the entire preview explicitly.

The worker reads at most 1 MiB plus one overflow byte. JSON preflight limits
nesting to 64 and lexical elements to 100,000. File I/O and parsing run through
OwnedWork; closing abandons the result. Choosing another file replaces the prior
candidate only after that read settles. Cancel in the chooser keeps the prior view.

## Evidence and remaining scope

`LegacyHistoryTest` exercises the source-shaped fixture, inert metadata and
quarantine preservation, malformed/duplicate/UTF-8/depth/size failures, real-file
native Choose, paging, chooser cancellation/destruction and pending-read close.
`CaptureHistoryWindowTest::legacyHistoryPreviewIsReachableAndClosesWithHistory`
drives the actual entry point and its owner lifetime.

Fixture provenance is in `tests/fixtures/legacy-history/provenance.json`. The
fixture is sanitized and hand-authored from pinned source; it is not a baseline
runtime observation or an accepted shared golden. Source evidence is EXTRACTED
from `HistoryManagerJSON.Load/Append` and `HistoryItem` at the pinned commit.

COMPCAT-009 remains partial: persistent/idempotent migration, rollback, complete
identity/state/privacy mappings, XML, SQLite, baseline runtime observation and
cross-platform qualification are still required. COMPCAT-010 is unchanged. No
format activation approval, security review or full compatibility claim is
established by this preview.
