# CORE-025: watch publication disposition and retention

Decision: CORE-025-WATCH-PUBLICATION-2026-11-14, resolved by maintainer Aoife on
2026-11-14.
Source: interactive maintainer brief, "CORE025 watch publication retention".
Contract: 0.4.0; CORE-PLATFORM-001: 0.6.0. Requirements: CORE-025.

## Problem

A watch folder publishes a file to a remote destination. When the attempt ends,
the application must decide what the file's fate is: done, worth trying again
later, or unknowable. A crash, a timeout, or a lost reply makes that decision
hard, and getting it wrong either loses work or publishes the same file twice.

The previous behavior did not distinguish these cases. A publication that was
never sent and a publication whose reply was lost could end up on the same
path, and the checkpoint store had no way to record that a publication was in
flight when the process stopped.

## Binding amendment, 2026-11-14

Maintainer Aoife made decisions A, B and C under ROOT-REVIEW-001 after review
of candidate a5232a4. Contract 0.4.0 and CORE-PLATFORM-001 0.6.0 are amended
in place because neither version has been published. Other capability packages
remain unchanged from 81220c4. No schema or linter is relaxed.

## Decision A: accepted uploads without a usable result

An acknowledged failure requires a started request, no prior-process `sending`
record, an HTTP response on every attempt, and no 2xx response on any attempt.
A 2xx status is evidence of acceptance, not of rejection; answered-attempt
accounting MUST still count it as answered. A failed upload with a 2xx response,
including an unparseable result or invalid result URL, is unconfirmed.

The first matching rule decides the publication:

1. A `sending` record left from an earlier process is `unconfirmed`, even when
   the new process's runtime fields are empty.
2. An upload that succeeded is `processed`. It MUST be checkpointed as complete
   and MUST NOT be uploaded again automatically under the same key.
3. A request that started and received a 2xx response on any attempt, without
   ultimately succeeding, is `unconfirmed`.
4. A request that started and received an HTTP response on every attempt, with
   no 2xx response on any attempt, is `released`. It MUST NOT be retried in the
   current session (P3) and MUST remain discoverable for a later session.
5. A request that started and has any unanswered attempt is `unconfirmed`.
6. A publication with no request started is `released` and MUST remain
   discoverable. This includes validation failure, a missing destination,
   cancellation before transfer, and an intent that was not saved.

An unconfirmed publication MUST NOT be uploaded again automatically under its
key. An implementation MUST NOT derive acknowledgement from the last status
alone. A later answered attempt does not repair an earlier unanswered attempt;
a later failed response does not erase evidence of an earlier 2xx acceptance.
An implementation unable to prove every attempt answered MUST treat the
publication as unconfirmed unless the upload ultimately succeeded.

A successful upload keeps its existing result. An explicit Upload file command
still reports `parse-failed` or `invalid-result-url` when applicable. Watch
publication holds do not invent a usable URL or a successful receipt.
The executor counts both answeredAttempts and acceptedAttempts; actual HTTP
2xx responses count in both. The controller and service preserve these facts.

## Durable intent and local file effects

An upload intent is durable before dispatch through the existing checkpoint
OwnedWork lane. Failed intent means no HTTP request, watching stops, and the
stable diagnostic is `watch-store-write-failed`. Earlier-process sending is
unconfirmed. Local copy/move outputs remain UUID-named, never overwrite, and a
move removes its source only after the copy commits. Copy/move crash replay
before checkpoint remains possible.

## Decision B: parent identity retention and store format

Retention MUST record each canonical parent directory's volume and object
identity: POSIX and macOS use `st_dev` and `st_ino`; Windows uses the volume
serial number and file ID. Both components MUST be decimal strings. Each
request MUST freeze the recorded parent map before worker observation.

For each key, the worker MUST observe parent identity before the file
fingerprint and again afterwards. The first matching rule applies:

1. A key with no observation facts (an unrecognized historic key) MUST be kept.
2. A `sending` key MUST be kept.
3. If either parent observation is missing, or the observations differ, the key
   MUST be kept.
4. If there is no recorded identity for the parent, the key MUST be kept. An
   unchanged file fingerprint MUST nominate its stable parent identity for
   adoption. Missing or changed files MUST NOT nominate an identity.
5. If the recorded identity differs from the observed identity, the key MUST
   be kept and the recorded identity MUST NOT be refreshed.
6. Otherwise the key MUST be kept when its fingerprint matches, and pruned
   when its fingerprint differs or the file is absent.

The output parent map MUST retain recorded identities referenced by kept keys.
It MUST also include a nominated identity for an unrecorded parent only when
all nominations for that parent agree. Conflicting nominations MUST adopt
nothing for that parent. Identities unreferenced by kept keys MUST be dropped.
An identity adopted in this write MUST NOT authorize pruning until a later
write. The GUI MUST adopt worker results only under the existing snapshot
fence.

Legacy keys can qualify through an unchanged sibling under their parent.
A recorded identity MUST NOT be refreshed even after a directory is recreated.
Such conservatively kept entries remain bounded by the 16 MiB store limit,
which fails closed with `watch-store-full`; a refresh policy is separate work.
A file that was pruned after deletion and later reappears with the same size
and modification time is treated as new and is accepted again.

### Store format

The watch checkpoint store is version 2:
`{"items":[keys],"publications":[{"key":...,"state":"sending|unconfirmed","at":...}],"parents":[{"path":...,"device":...,"inode":...}]}`.

A missing `parents` field MUST read as empty, for version 1 and earlier
unpublished version 2 files. A present `parents` field MUST be an array. Every
entry MUST have an absolute string `path` and string `device` and `inode`
components of 1–20 ASCII digits. Duplicate paths or malformed entries MUST
fail closed with `watch-store-invalid-version-or-data`. Every version 2 write
MUST include `parents`.

Version 1 MUST remain readable and MUST upgrade on the next write. A reader
that only understands version 1 MUST refuse version 2 with
`watch-store-invalid-version-or-data` and MUST NOT republish watched files.
The watch rules store stays at version 1. This pre-release format adds no
live-migration user interface.

A private tmpfs/bind-root probe demonstrated the gap in parent-existence-only
retention: hold_before=1, parent_exists_unmounted=1, fingerprint_restored=1,
hold_after=0. The amended rule covers processed entries as well as holds.
The worker observes stat(parent), fingerprint(file), stat(parent), decides
against the frozen map and serializes. Snapshot-guarded GUI adoption publishes
the resulting sets and map without filesystem work on the GUI thread.

## Decision C: retry session and notices

A session lasts for the watch service's life in one application process.
Stopping and starting watching does not begin a new session. Answered released
failures remain attempted even when settlement follows Stop. Cancelled work
whose request never started remains retryable at the next Start. Persisted
unconfirmed holds survive Stop/Start and process restart.
An explicit Upload file command is independent of the watch hold. Changing the
file gives it a new key. No new UI is added. An in-session notice names an
unconfirmed file; one startup notice names up to five basenames and the count
of remaining names, with no directory paths or URLs.

## Implementation and qualification

Reuse OwnedWork, PrivateJsonFile, the existing executor/controller and shared
pure disposition/retention helpers. The adapter only translates observations.
POSIX identities use stat; the C++17 baseline is unchanged. Add no watcher or
mount-management mechanism, rules-store version change or identity-refresh UI.
The unbounded exit drain and conservatively retained entries at the 16 MiB
limit are accepted follow-ups, not new mechanisms in this slice.
Prove unusable accepted results, real mount/unmount/remount preservation and
safe later pruning, legacy/invalid parent metadata, and process-lifetime retry
ordering on both Qt baselines. A skipped namespace proof is not qualification.
Rebuild adapters, run the full suites, obtain maintainer line review and an
independent Sonnet review of acceptedAttempts and the parents reader, then run
the rebased push gate. Linux stays partial until qualified; other platforms
are not waived. No activation, census closure or release is approved.
