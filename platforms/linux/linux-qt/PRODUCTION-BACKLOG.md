# Linux production backlog

Full audit date: 2026-11-11 at `d25064efea688636af7f6b7c7b043db2a4d0ed09`; integration reviewed 2026-11-12 at `4dffe3cbe074129f50a6eaf483d76b885261d459`. The frozen audit counted 169 implemented candidates; four filename/host completions plus ES-029 bring the current inventory to 174. Product Contract 0.3.1.

All 16 manifest capabilities / 345 IDs were reviewed. The 325 desktop rows classify **174 tested candidate implementations, 110 partial, 6 not started, 34 blocked and 1 mobile-scope-only row**; all 20 MOB requirements are outside Linux desktop scope. These are candidate-behavior classifications, not parity, release acceptance or remaining effort.

[traceability/](traceability/) records every clause, reason and exact source/test method hash. [PRODUCTION-BACKLOG.json](PRODUCTION-BACKLOG.json) retains previous audit metadata and current findings. Anchors are **EXTRACTED** source evidence; graph links and mere test presence did not establish compliance. Existing vectors, dispositions and earlier audit records were preserved, including the new upstream filename vectors and their approved contract clarification.

Trace status retains the established enum. `audit.classification` and `blocked_reason` distinguish the 34 blocked rows; blocked work receives no implemented credit.

## Capability inventory

| Capability | Desktop IDs | Implemented | Partial | Not started | Blocked | Mobile only |
|---|---:|---:|---:|---:|---:|---:|
| FILENAME-GENERATION-001 | 27 | 27 | 0 | 0 | 0 | 0 |
| POST-CAPTURE-ACTIONS-001 | 24 | 24 | 0 | 0 | 0 | 0 |
| REGION-CAPTURE-001 | 27 | 19 | 5 | 2 | 1 | 0 |
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

The current next foundation is the remaining synchronous storage boundaries.
Recovery recreation after external removal is implemented with store/native
regression proof; both reviews approved it and it landed as fdc728f after the full gate. Steps 1–4 below
have landed and remain as the historical build sequence; their broader retention
and supported-target qualification edges stay open.

1. Preserve newer-version and live-owner recovery records before pruning. A scratch program linked to current production code seeded an older private version-2 recovery file and wrote eight valid version-1 records; the newer file was deleted. Add the regression before fixing ownership/version-safe cleanup. Do not select an unapproved retention threshold.
2. Move editor recovery serialization, write/list/remove and restart review through OwnedWork with owner/snapshot fencing and asynchronous prompts. Separate recovery files and age/source disclosure already work; preserve that journey.
3. Fence file-upload network publication when shared execution admission/start/read-file-step persistence fails. FileUploadController currently records a false flag and still starts UploadExecutor; this is a source finding, not a demonstrated network repro. Inject writer failures in the existing native upload fixture before fixing it. Shared upload/watch snapshots and result records already exist.
4. Move watch-folder recursive directory discovery and stat work through cancellable owned work. Copy/move/upload effects already run on workers; synchronous discovery in rescan remains a GUI-thread gap for large or slow authorized trees.
5. ES-029 implemented candidate: capability 0.2.1 keeps public diagnostics closed and preserves value-free rejection reasons separately. Lexical resource bounds report document-too-large; malformed or unsafe input reports document-invalid. Native Open/replacement warnings and 23 conformance vectors pass on both Qt baselines. Maintainer accepted the code, contract, tests and evidence on 2026-11-13; the requested trace correction is applied and the full gate remains required before landing.
6. Retain explicit model/dependency/threshold/semantic/retention/security/release questions and finish supported-target qualification after working journeys. Do not redo reviewed JSON/XML/SQLite history import, shared execution, existing worker copies, diagnostics export or recovery disclosure.

## Current findings

- Current native paths include the ES-029 size-priority fix, owned watch-folder file effects, shared file-upload/watch execution, explicit diagnostic export and separate editor recovery. The audit does not treat these landed features as absent.
- Reproduced against current production code: recovery count pruning deletes a newer-version file; ES-029 size priority succeeds while unsafe/traversal inputs still return additional codes. Repro sources and observations are retained in shared loop evidence; these are findings for the next implementation slice, not fixes in this audit.
- Source-extracted, without a runtime latency/network claim: recovery storage and recursive watch discovery run on GUI callbacks; upload execution-storage failure did not fence the subsequent network start at the audited revision. The 2026-11-13 correction now stops before HTTP; three injected writer failures changed from one POST each to zero. Recursive discovery was moved to owned cancellable work on 2026-11-13; held-scan tests prove responsive GUI events, discarded obsolete results and retained-source restart. Synchronous watcher registration, final per-file identity checks, checkpoint persistence and recovery work remain open.
- XML/SQLite and JSON history review/import/archive/undo/recovery exist. COMPCAT-010 has direct WAL snapshot, schema, trigger isolation, bounds and failure tests; complete legacy/state/privacy/runtime/cross-platform qualification remains separate.
- The approved upstream FN-021 wide-draw rule and native/shared vectors now resolve random files above 256 lines; EO-014 host callback contracts and real owner/reentry/partial-publication tests also landed. Preserve these four incremental completions. Physical compositor mapping, Text/Step semantics, ledger completeness, retention and human gates remain explicit; code initializers and existing tests cannot approve them.

## Explicit gates

The scheduler still blocks remove_background, QR scanning/OCR dependencies, the scroll alignment threshold, human .sxie/Secret Service review, and release/signing/security-posture changes. Upstream implementations or approval notes do not cancel those instructions. Exact product questions and all blocked IDs remain in the shared loop state and machine inventory. Full shared vectors are absent for nine desktop packages; native tests and local goldens do not replace independent cross-platform evidence or human acceptance.

## Verification

The full native suite and final push gate run on the audited/reconciled tree. Their actual results are recorded in shared loop state after successful push. Contract lint, backlog/progress freshness and per-row hash/method/vector/disposition integrity are checked separately. No native behavior, contract, expected result, dependency or approval changed in this audit.
