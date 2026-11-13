# Linux production backlog

Revalidated 2026-11-14 at `fdc728fb6c33dc0a7685205e77dfdcfd37217db6` against all 16 manifest capabilities and 345 requirement IDs. The previous source audit and later integration records remain in each trace map’s audit history. Unchanged source and test bodies were compared to their frozen revisions; changed behavior and test assertions were reviewed against the current contract. The current trace maps pin 1,815 production/test anchors across 205 files, with exact hashes and named test locations. Anchors are **EXTRACTED** source evidence; graph links and file presence do not establish behavior.

The 325 desktop requirements classify as **174 tested candidate implementations, 111 partial, 5 not started, 34 blocked and 1 outside desktop scope**. The 20 MOB requirements remain outside Linux desktop scope. These classifications do not approve parity, release or supported-target qualification.

[Trace maps](traceability/) and the [machine inventory](PRODUCTION-BACKLOG.json) retain dispositions, vectors, previous audits and the current classifications. Blocked rows receive no implemented credit.

## Capability inventory

| Capability | IDs | Implemented | Partial | Not started | Blocked | Outside desktop |
|---|---:|---:|---:|---:|---:|---:|
| FILENAME-GENERATION-001 | 27 | 27 | 0 | 0 | 0 | 0 |
| POST-CAPTURE-ACTIONS-001 | 24 | 24 | 0 | 0 | 0 | 0 |
| REGION-CAPTURE-001 | 27 | 19 | 6 | 1 | 1 | 0 |
| EDITOR-SESSION-001 | 30 | 24 | 3 | 0 | 3 | 0 |
| EDITOR-ANNOTATIONS-001 | 20 | 16 | 3 | 0 | 1 | 0 |
| EDITOR-EFFECTS-001 | 18 | 7 | 10 | 0 | 1 | 0 |
| EDITOR-CANVAS-001 | 17 | 14 | 2 | 0 | 1 | 0 |
| EDITOR-OUTPUT-ACTIONS-001 | 16 | 9 | 7 | 0 | 0 | 0 |
| EDITOR-SETTINGS-001 | 16 | 8 | 7 | 0 | 1 | 0 |
| EDITOR-UTILITIES-001 | 19 | 7 | 7 | 0 | 5 | 0 |
| SETTINGS-CATALOG-001 | 20 | 2 | 13 | 0 | 5 | 0 |
| WORKFLOW-CATALOG-001 | 18 | 4 | 12 | 1 | 1 | 0 |
| INTERFACE-CATALOG-001 | 18 | 3 | 10 | 1 | 4 | 0 |
| CORE-PLATFORM-001 | 37 | 8 | 19 | 2 | 7 | 1 |
| COMPATIBILITY-CATALOG-001 | 18 | 2 | 12 | 0 | 4 | 0 |

## Next work in build order

1. Finish asynchronous editor recovery control flow. Storage and serialization already run through a serialized OwnedWork queue. Replace nested wait/waitIdle loops and modal restart/close waits with owner-fenced continuations; preserve Save, Discard, Restore, deferred records and signal-exit cleanup.
2. Move file-upload execution admission/step/terminal persistence through owned work while retaining the existing pre-HTTP failure fence and durable terminal observation. Network requests already stop on admission/start/read-step storage failure.
3. Finish watch-folder GUI storage boundaries: checkpoint persistence, watcher registration and final file identity checks. Recursive discovery/stat and file effects already run on cancellable owned workers; retain obsolete-result fencing and source preservation.
4. Continue the lowest remaining capture/output/catalogue journey gap after the foundation above. Cursor capture has a tested default-off single-output compositor path; mixed-scale/rotated/desktop qualification stays partial. ES-029 public diagnostics and separate value-free reasons are implemented candidates under the approved 0.2.1 capability.
5. Retain model/dependency/threshold/retention/security/release gates. Do not rebuild landed history import, shared execution, diagnostic export, recovery protection/takeover, recursive discovery or ES-029 boundaries.

## Current findings

- Recovery count cleanup now protects newer, foreign, corrupt, locked and live records, plans under existing store locks, and deletes only after successful publication. A full protected collection refuses a new record. Restore takes over only its selected ID and snapshot. Held-worker and signal-exit tests cover recovery storage and cleanup. Externally deleted known snapshots now recreate safely under collection/record locks, with protected-capacity refusal and intervening replacement tests. Configured retention remains blocked.
- File-upload execution admission/start/read-step failures now stop before HTTP. Existing tests inject all three stages and assert zero requests. Non-idempotent Qt upload bodies cannot rewind for transport replay; missing remote acknowledgement remains interrupted. Execution persistence itself still runs in GUI callbacks.
- Recursive watch discovery/stat runs through OwnedWork. Held-scan tests cover Stop/restart, rule and capture-folder changes, owner destruction and retained-source restart. Watcher registration, final dispatch fingerprint and checkpoint persistence remain synchronous boundaries.
- Cursor inclusion is no longer absent. The default-off, profile-local compositor path has protocol-server tests, real controller/crop/PNG integration, and a retained Qt 6.4/Sway single-output proof. RC-013 moves from not started to partial; wider topology and target guarantees remain open.
- ES-029 now reports its closed public diagnostic set and keeps specific, value-free reasons separately. Current parser, reader, native warning and conformance evidence supersedes the old extra-code finding. The maintainer-approved contract and implementation remain unchanged by this audit.
- XML/SQLite/JSON history import, shared execution, output consent and the reviewed filename/host completions remain present. Their separate retention, privacy, compatibility, physical and human qualification edges remain explicit.

## Explicit gates

The scheduler still blocks remove_background, QR scanning/OCR dependencies, the scroll alignment threshold, human .sxie/Secret Service review, and release/signing/security-posture changes. No upstream note cancels those instructions. Product-policy questions remain explicit in shared loop state; this audit changes no contract, behavior, dependency or approval.

## Verification

The full Debug offscreen suite and final push gate must pass on the integrated tree. Actual results are recorded in shared loop state after push. Contract lint, backlog/progress freshness and current anchor integrity are checked separately.
