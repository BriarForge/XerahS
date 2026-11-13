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

## Decision

The publication disposition follows the first matching rule.

1. A publication left over as `sending` from an earlier process is
   `unconfirmed`.
2. A publication that succeeded is `processed`: it is checkpointed as complete
   and is never uploaded again automatically under the same key.
3. A publication for which at least one request was started and every request
   attempt received an HTTP response, but which did not succeed, is `released`:
   it is not retried again in the current session and stays discoverable, so a
   later session finds the work again.
4. A publication for which at least one request was started and any attempt
   ended without an HTTP response is `unconfirmed`: it may or may not have
   reached the remote, and it is never uploaded again automatically under the
   same key.
5. A publication for which no request was started is `released`. That includes
   validation failure, a missing destination, cancellation before transfer, and
   a send intent that was not saved.

A publication is acknowledged when the upload succeeded, or when at least one
request was started, no prior-process sending record survived a restart, and
every request attempt received an HTTP response of any status. An acknowledged
failure releases the file. An implementation that cannot show that every
attempt was answered must treat the publication as unconfirmed.

The controlling rule is proof, not inference from the last status. A later
answered attempt does not repair an earlier unanswered attempt, so a
publication that saw a lost reply and then a `5xx` is unconfirmed, while a
publication whose every attempt was answered with any status, including `5xx`,
is released.

### Send intent and startup

The intent to publish is durably saved before the request is dispatched, using
the checkpoint work lane that already owns watch-folder checkpoint writes. If
the intent cannot be saved, no HTTP request is issued, watching stops, and the
failure is reported as `watch-store-write-failed`.

Because the intent is saved first, a process that stops between the intent and
the response leaves a durable `sending` record. Every leftover `sending` record
is unconfirmed at startup, whatever the new process's runtime fields contain.
This is why the startup rule is checked first and overrides every runtime fact.

### Retry eligibility and notices

Retry is controlled by explicit user action. The user can use the existing
Upload file command independently of the watch key, or change the file, which
gives it a new fingerprint and therefore a new key. The Upload file command
does not change the fingerprint or clear the existing watch hold. No new user
interface is added.

During a session, an unconfirmed publication is named in the current notices.
At startup, one `watch-publication-unconfirmed` notice names up to five base
names and reports how many further names remain. It discloses no directory
path, no remote URL, and no other location detail.

### Local copy and move

Local copy and move behavior is unchanged. A crash before the checkpoint is
written may repeat the copy. Generated output names never overwrite an
existing file, and a move removes the source only after the copy has committed.

### Retention

Each checkpoint write prunes a `processed` or `unconfirmed` entry when the
entry's parent directory exists AND the file's current fingerprint differs
from the fingerprint recorded in the key, because the file was then deleted,
moved, or changed. An entry whose parent directory does not exist is kept,
because the parent may be an unmounted or removed root rather than a deleted
file. An entry recorded as `sending` is never pruned.

A file that is deleted and later reappears with the same size and modification
time is treated as a new file and is accepted again.

### Store format

The watch checkpoint store becomes version 2:

```json
{"items": ["<key>"], "publications": [{"key": "<key>", "state": "sending", "at": "<timestamp>"}]}
```

The `state` field is `sending` or `unconfirmed`. A version 1 store stays
readable and is upgraded on the next write. A binary that only understands
version 1 refuses a version 2 store with `watch-store-invalid-version-or-data`
and fails closed without republishing anything. The watch rules store version
is unchanged.

This is a pre-release format. It carries no live-migration user interface.

## Authorised implementation constraint

The following future implementation shape is authorised by the maintainer and
recorded here so the contract and the implementation cannot drift. No
implementation code is written in this checkpoint.

The upload executor's attempt outcome must carry a separate answered flag
alongside the attempt count, so that the "every attempt was answered" proof is
available without inferring it from the last status. The executor's attempt
completion path sets the flag true only on the path where a status was
received, and false for a timeout, a network error or abort, a transient
failure with no status, and any other attempt that ended without a response.
The executor counts answered attempts and reports the count next to the total
attempt count. The controller derives "every attempt received an HTTP
response" as "answered attempts equal attempts, and attempts is greater than
zero".

The derivation from the request-built flag, the attempt count, and the last
HTTP status alone is explicitly insufficient: it reports the last attempt's
status and therefore treats a lost reply followed by a `5xx` as answered. The
required test is a timeout, then a `5xx`, then a retry exhaustion, which must
be unconfirmed and must fail any last-status derivation.

This is a minimal counter and result field, not a new architecture. Because the
executor touches the credential surface, the implementation needs the
independent second review that ROOT-SECURITY-001 requires, in addition to the
maintainer's review.

## Qualification

CORE-025 is implemented on Linux only after a real process `SIGKILL` proof on
`tmpfs` and the approved Qt 6.4.2 baseline. Network and FUSE qualification is a
separate Layer 5 concern. A periodic scan fallback exists.

## Impact

The watch checkpoint store moves from version 1 to version 2 and gains a
`publications` array. The watch rules store is unchanged. The uploaded file
content, the destination contract, the upload request itself, and the local
copy and move behavior are unchanged.

The `Upload file` command, the file fingerprint scheme, the checkpoint store
location, and the destination configuration are unchanged. No platform waiver,
ownership expansion, credential access, activation, census closure, or release
approval is granted. Windows and macOS remain unimplemented and must apply
this policy when built.
