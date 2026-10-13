# Native saved-file outputs

Candidate production evidence for PCA-004/005/010/011/012/013/014/018/021 and
CORE-004/005/007/010. Complete action catalogue, artifact compatibility,
supported native target matrix and human review/release gates remain Required.

## Actual operation and authority

The per-run chooser now offers **Copy image file**, **Copy file path**, and
**Show saved file in folder**, alongside Save and Copy image. These file
consumers require Save in this run; the chooser states that prerequisite and
that copying the file/path shares its saved location. It does not implicitly
select Save. Without a successful save, the Core engine records each consumer
as dependency-skipped with `missing_artifact: save.file`. Independent image
clipboard work continues after failed save. Defaults remain Save then Copy image.

Successful PNG publication captures a native in-memory identity from its still
open descriptor: device, inode, byte length, nanosecond modification/change
timestamps. Only a private, owned, regular, singly linked file gets this binding.
The live receipt retains it through explicit linked retry. Canonical reports and
journals serialize the existing artifact facts, never this binding; restart
metadata and arbitrary paths cannot reconstruct native consumption authority.

Consumers validate the binding on the worker and again at GUI dispatch. Absolute
path components open with `openat` and `O_NOFOLLOW`; directories, symlinks,
dot/dot-dot components, FIFOs, missing/unbound files, shared permissions,
additional hard links, replacements and observed metadata changes fail closed.
Failure keeps the saved file and original pixels, with value-free
`saved-file-unavailable` or `saved-file-changed` diagnostics.

This is a dispatch-time identity check, not a cryptographic content proof or
permanent filesystem lock. File managers and later clipboard consumers receive
a URI/path and can observe subsequent external changes. Eliminating that race,
shared persisted artifact authority and filesystem/security qualification remain
review work; this checkpoint grants no deletion, upload or restart authority.

## Native clipboard and reveal

File copy publishes Qt URL MIME data (`text/uri-list`) and the GNOME copy-form
`x-special/gnome-copied-files`, with a `copy` verb and encoded local URI. It never
publishes a cut request. Path copy publishes the absolute native path as text.
Both run on the GUI and report success only according to Qt clipboard ownership.
Private tests check actual payloads, URI escaping and unchanged saved bytes;
physical paste/ownership behavior on every supported desktop remains Required.

Reveal uses the [freedesktop File Manager D-Bus interface](https://wiki.freedesktop.org/www/Specifications/file-manager-interface/):
`org.freedesktop.FileManager1`, `/org/freedesktop/FileManager1`, `ShowItems`,
with one encoded local URI and an empty startup ID. A successful empty method
reply acknowledges the request; it does not prove the file manager's window was
visually inspected. Missing/denied services return `reveal-unavailable` without
silently substituting a folder-opening action.

The GUI dispatch is asynchronous and bounded to ten seconds. Cancellation
before claim prevents effects. After an uninterruptible request has been sent,
cancellation waits for its real acknowledgement and records cancellation
arrival. A timeout, disconnect, malformed reply or owner destruction before
acknowledgement produces typed unknown delivery. The Core engine stops as
`interrupted`, records no invented failed/cancelled result for that action,
starts no later action, and leaves the journal's reveal intent pending.
The native receipt carries `reveal-outcome-unknown`. Retry rejects interrupted
or unknown outcomes; late replies cannot change a completed observation.

## Retry, provenance and recovery

The Qt Core planner now covers save, image/file/path clipboard and reveal. It
targets only failed/dependency-skipped selected outputs and preserves successful
actions. A file consumer can reuse an acknowledged original save, or run after
the original selected failed Save is retried. It cannot implicitly add an
unselected prerequisite, repeat a successful save to repair an externally changed
file, or retry uncertain reveal delivery. A missing original prerequisite reports
`retry-prerequisite-unavailable`.

The engine receives inherited save availability only after native retry admission;
the executor still validates the actual live artifact. Inherited prerequisites
receive no invented action results/timings. They do not become files created by
this attempt: the Core delete guard rejects deletion of an inherited-only save.
New retry identity, preserved successes and true file creator remain append-only.

Write-ahead intent precedes every file output when a saved artifact exists.
Interrupted reveal keeps that intent; a killed file-clipboard fixture reopens
with the successful save and unknown pending consumer, retaining the actual PNG.
Restart review remains read-only and cannot repeat effects or reconstruct pixels.

## Verified scope and remaining work

`CaptureFileOutputTest` exercises native identity rejection, actual Qt MIME/text
publication, private-bus ShowItems acknowledgement/denial/missing service/timeout,
responsive GUI and owner destruction, production pipeline cancellation,
interrupted journals, unchanged files, prerequisite retry and preserved successes.
`PortalScreenshotTest` executes actual private-bus acquisition, per-run native
chooser and shared Retry/Cancel UI with true saved pixels and artifact provenance.
Core tests cover prerequisite availability, original selection limits, unknown
outcome interruption and inherited-file deletion prevention. Real-process
`PipelineJournalTest` kills and reopens a pending file consumer.

The checkpoint passed 36 local CTest targets, all 114 published vectors,
contract lint/backlog checks and their 25 tests, plus seven runner tests. Tests
use temporary files/stores and private non-activating buses; live Avalonia
configuration and real capture/upload commands remain untouched. Exact pushed
revision qualification uses the Ubuntu 24.04/Qt 6.4.2 Product Contract CI.

Native diagnostic names and typed unknown-delivery semantics need shared
conformance/schema qualification. Private-bus remote error fixtures include both
standard D-Bus `Timeout` and `TimedOut` spellings, `NoReply`, and `Disconnected`;
none can become a known failed action or retry permission. Concrete owner decisions include independently
published unknown-outcome vectors and whether originally configured symlink
save roots may be admitted through a bound directory identity; this candidate
rejects symlink components while preserving the saved file. Native Save As/thumbnail, multiple artifact
tracking, generic catalogue/prerequisite retry, uploads/editor handoffs,
configured workflows, restart media authority, retention/cleanup and all
supported filesystem/distro/compositor/physical accessibility/security/privacy/
parity/release reviews remain Required. Next implement native Save As with
multiple-artifact tracking and matching retry/journal coverage.
