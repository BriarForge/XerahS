# Native paired capture persistence

Candidate evidence for ES-011/012/013/014/030, PCA-007/008/010/011/012/016/018/021
and CORE-004/007/010/023/027. This extends the verified annotation handoff;
the full Linux contract and native/human acceptance gates remain Required.

## User-visible behavior

The capture annotation window offers **Also save editable annotations beside
each saved PNG (.xann)**. It is unchecked for this candidate's per-run choice;
Apply freezes explicit intent with the accepted document. The adjacent notice
explains that the companion contains the original capture and editable objects.
Cancel does not adopt either the document or the checked intent. Workflow-wide
pairing/default/import policy is still a product-owner clarification, not an
accepted change to the shared contract or a replacement for ES-011.

When requested, Save and Save As each publish their own rendered PNG and
compatible `full-raster-name.png.xann`. The existing Core `saveSidecar`,
`writeXann` and document validation implement gzip/JSON, source retention,
annotation ordering, timestamps and empty-document policy. The sidecar binds
to SHA-256 of the actual published PNG, while embedding the unannotated source,
so native reopening restores editable objects without compounding them.

Both artifacts must satisfy policy for the save action to succeed. A companion
failure records **PNG succeeded / sidecar failed**, reports
`sidecar-save-failed`, keeps the PNG and editable document, and exposes the
incomplete save in the normal notification and result review. The retained
document has no invented clean editor checkpoint; Edit retained capture still
opens it as an unsaved document. Independent saved-file consumers can use an
acknowledged PNG. Partial publication grants no delete authority.

The shared, default-Cancel retry confirmation explains companion repair.
Retry of a typed partial Save or Save As validates and repairs only the
existing PNG's companion. It does not encode another PNG, reopen the Save As
picker, re-run annotation, or repeat successful consumers. Repeated failures
retain the same PNG bytes, binding and creator; a newly published sidecar gets
the new attempt's creator ID. Missing documents, arbitrary failed-save/file
combinations and interrupted/unknown outcomes cannot authorize this repair.
Preserved raster provenance is separate from preserved fully successful
actions, including after a cancelled repair attempt.

## Native publication and recovery

PNG publication retains the existing new-destination/no-replacement policy.
The companion is encoded on the pipeline worker into a private same-directory
temporary, flushed and fsynced, and atomically published using Linux
`RENAME_NOREPLACE`. Existing regular files, directories and dangling/safe
symlinks are preserved; collisions remain explicit incomplete saves.

Hashing opens the live-bound PNG with no-follow checks for every path component,
then verifies private ownership, regular type, single link, length and exact
device/inode/time identity before and after bounded streaming. It checks
cancellation throughout and buffers no whole PNG. Validation repeats before
companion publication. Changed/missing/unbound rasters cannot authorize a
companion write or a replacement PNG. Cancellation after PNG publication keeps
its acknowledgement, leaves the required companion incomplete and stops later
actions. The remaining path race after validation, filesystem/power-loss
durability, full resource/performance and physical target qualification are
review limits; this is not a security approval.

For an empty document, absence of a default sidecar satisfies this new-path
policy and returns no sidecar path. A colliding unowned companion is preserved
and reported failed. Capture has no prior companion deletion authority merely
because a filename ends in `.xann`. The Core ES-014 removal vector remains unchanged.
The subsequent [editor-save checkpoint](EDITOR-SAVE.md) adds explicit supported
companion replacement/removal and bounded worker repair for editor Save. Complete
shared consent/compatibility policy and native qualification remain Required;
this conservative capture case does not claim full ES-014 qualification.

The write-ahead recorder acknowledges PNG file facts while the save action is
still pending, before permitting the companion effect. A failed acknowledgement
blocks companion publication and preserves known live PNG/document facts.
The writer supplies raster/companion kind explicitly, separately from the path.
Killed-process recovery retains the known PNG and pending companion with an
unknown outcome; it invents neither save success nor a retryable failure.
Native restart review displays those artifact states separately, hides private
paths in optional JSON details and performs no file reads or external actions.
It never reconstructs live bindings or media/documents from metadata.

The four published canonical fields remain unchanged. Additive candidate
fields under the existing native namespace contain `artifact_outcomes`
(`action`, `artifact`, `state`, optional stable `diagnostic` and `absent`) and
retry `preserved_raster_actions`. Private journal v1 adds
`pending_save_artifact`; parsed recovery reports `unknown_artifact`.
`save.sidecar` and `save-as.sidecar` file facts describe actual publications,
not new catalogue prerequisites. Parsers reject inconsistent pair/stage facts.
These native extensions still require shared schema/upgrade qualification;
no source bytes, live identities or original capture pixels enter the journal.

## Verification and next work

- `CapturePairedSaveTest`: actual canvas/explicit checkbox, both real pairs,
  focused Space-key consent (the baseline fixture's default centre click did
  not toggle the Qt 6.4 checkbox; checked-state/artifact assertions are retained),
  correct hashes/embedded source, native reopening, private permissions and
  review rows; Save and Save As partial failures with two successive repairs,
  unchanged PNG bytes/identity/creator, no repeated picker/annotation/consumer,
  symlink collision and changed-raster rejection, cancellation after PNG,
  actual private-journal failure/recovery, empty-document absence/collision.
- `CaptureAnnotationTest`: unchecked intent and checked-but-cancelled intent
  leave PNG-only outputs and original/accepted pixels as appropriate.
- `PortalScreenshotTest`: private-bus acquisition/chooser, honest normal
  notification, shared default-Cancel repair confirmation, no new capture or
  repeated successful consumer, unchanged borrowed image and original PNG.
- `PipelineJournalTest` and its real process fixture: kill before companion
  publication, reopen known PNG plus unknown companion and no invented save
  result, source bytes or media authority.
- `PipelineHistoryDialogTest`: read-only native restart artifact review keeps
  the journal and PNG unchanged. `PipelineRetryTest` and
  `PostCapturePipelineTest`: typed partial admission, invalid facts, preserved
  raster lineage, namespaced serialization and deletion guards.

The checkpoint runs all 39 native/domain/conformance CTest targets, all 114
published vectors, contract lint/backlog checks and their unit tests. The
affected targets also run against cached Qt 6.4.2; exact pushed-SHA Ubuntu
24.04/Qt 6.4.2 CI is the authoritative baseline checkpoint evidence. Passing
these checks does not close human security/privacy/accessibility, parity,
golden/compatibility, supported-distro/compositor, packaging or release gates.

The subsequent [editor-save evidence](EDITOR-SAVE.md) records bounded immutable
worker output, explicit companion repair, guarded replacement/removal and actual
cancellation/dirty-state/lifetime fixtures under EO-001/003/005/006/014/015 and
ES-011/012. Next implement explicit PNG/JPEG/WebP save options and retain all
recorded qualification limits. Continue full annotation tools, formats, workflow/default
configuration, capture topology, recording/audio, uploads/credentials,
catalogues and every other domain in CONTRACT-AUDIT.md alongside that work.
