# Linux production backlog

Audit date: 2026-11-04. Reviewed source: `5b5b70d4fac6f09ebdf096b8044e5403004d7e11`. Product Contract 0.3.1.

All 16 manifest capabilities and 345 requirement IDs were reviewed. The 325 desktop rows classify **162 tested candidate implementations, 118 partial, 10 not started, 34 blocked and 1 mobile-scope-only row**. The separate 20 MOB requirements are outside Linux desktop scope. These are unweighted candidate-behavior classifications, not a completion percentage, platform waiver or release acceptance.

The per-row reasons and exact current source/test method hashes are in [traceability/](traceability/). [PRODUCTION-BACKLOG.json](PRODUCTION-BACKLOG.json) preserves prior audit metadata, vectors, dispositions and historical incremental outcomes. Anchors are **EXTRACTED** source evidence; graph links and test presence alone were not compliance evidence.

Trace files retain the established native status enum. Blocked rows additionally carry `audit.classification: blocked` and `blocked_reason`; the counts below use that audit classification, so implemented coverage never includes blocked work.

This is a dated audit snapshot. Current requirement status is in traceability and
PROGRESS.md. The November 11 verification reconciled the stale QR dependency
blocker with Mike's November 2 approval in DEPENDENCIES.md; the old inventory
counts below are retained as historical audit evidence.

## Capability inventory

| Capability | Desktop IDs | Implemented | Partial | Not started | Blocked | Mobile only |
|---|---:|---:|---:|---:|---:|---:|
| FILENAME-GENERATION-001 | 27 | 24 | 3 | 0 | 0 | 0 |
| POST-CAPTURE-ACTIONS-001 | 24 | 24 | 0 | 0 | 0 | 0 |
| REGION-CAPTURE-001 | 27 | 18 | 5 | 3 | 1 | 0 |
| EDITOR-SESSION-001 | 30 | 21 | 5 | 1 | 3 | 0 |
| EDITOR-ANNOTATIONS-001 | 20 | 16 | 3 | 0 | 1 | 0 |
| EDITOR-EFFECTS-001 | 18 | 7 | 10 | 0 | 1 | 0 |
| EDITOR-CANVAS-001 | 17 | 14 | 2 | 0 | 1 | 0 |
| EDITOR-OUTPUT-ACTIONS-001 | 16 | 8 | 8 | 0 | 0 | 0 |
| EDITOR-SETTINGS-001 | 16 | 8 | 7 | 0 | 1 | 0 |
| EDITOR-UTILITIES-001 | 19 | 7 | 7 | 0 | 5 | 0 |
| SETTINGS-CATALOG-001 | 20 | 2 | 13 | 0 | 5 | 0 |
| WORKFLOW-CATALOG-001 | 18 | 4 | 12 | 1 | 1 | 0 |
| INTERFACE-CATALOG-001 | 18 | 3 | 10 | 1 | 4 | 0 |
| CORE-PLATFORM-001 | 37 | 6 | 21 | 2 | 7 | 1 |
| COMPATIBILITY-CATALOG-001 | 18 | 0 | 12 | 2 | 4 | 0 |

## Next work in build order

1. Restore ES-029 parse diagnostic precedence on the existing import journey. Oversized input currently changes from document-too-large to unsafe-content/path-traversal when those fields are added; the adapter reproduces this without allocating pixels. Add the regression before fixing the native parser; preserve the contract diagnostic order.
2. Move watch-folder copy/move work out of the GUI rescan loop through OwnedWork; retain private atomic output and provide cancellation/progress and durable outcomes. Watch-folder upload already delegates to FileUploadController.
3. Connect file-upload/watch-folder receipts to the shared traceable workflow result contract (WFCAT-002/004/017): preserve origin, add the missing effective-settings snapshot, ordered results and terminal correlation. Do not redo native capture/recording workflow execution.
4. Finish remaining approved capture options (cursor/output exclusion, picker options) and editor input/output host gaps only where they unblock a working journey. Native quick-task selection, managed temporary upload, before-upload confirmation, guarded deletion, recording, QR/OCR code, image insertion, editor upload/pin and annotation styles already exist; verify current source before any claim.
5. Follow the explicit product questions for authoritative mixed-monitor/recapture topology, Text/Step semantics, legacy fallback removal, background composition, retention and %rf files above 256 lines. Do not turn source defaults or reference-derived mappings into contract decisions.
6. After the working journeys, finish missing approved ledger/entry-point/compatibility coverage, then physical target/performance/accessibility/security/privacy/licensing/parity and release qualification. Model, threshold, dependency and human authority gates remain explicit below and in the machine inventory.

## Findings that change the old backlog

- Current native capture engine has meaningful ordering, selection, upload, consent, deletion, journal/retry and isolation tests. Do not classify those finished flows as absent merely because a wider catalogue remains partial.
- Full annotation/style/asset paths have substantial native matrix coverage. Exact Text-border/Step semantics and complete accessibility/package qualification still constrain their relevant rows.
- Core region vectors do not establish native compositor topology. Qt screen origins are packed into rows/columns, so arbitrary mixed-scale/staggered physical desktop mapping and pre-acquisition recapture authority remain unresolved.
- File-upload history preserves origin, but its separate receipt lacks the shared effective-settings snapshot and ordered-step lifecycle. Watch-folder copy/move still does file work synchronously in rescan.
- Import reports and bounded readers exist. Aggregate XANN rejection reports do not identify the affected record; complete format/secret classifications and cross-platform canonical fixtures remain partial. Timezone-less XANN input still uses local interpretation.
- Filename %rf currently silently ignores eligible lines after the first 256. An approved wider sampling rule is needed; published small-file vectors do not cover this gap.

## Blocked product and qualification decisions

The machine inventory lists every blocked requirement and its reason. Existing local implementations do not approve human authority or settle missing semantics. Preserve the scheduler gates for remove_background model approval; scroll alignment threshold; human SXIE/Secret Service review; release/signing/contract/security-posture changes. Mike explicitly approved installed zxing-cpp 3.1.1 and Tesseract 5.5.3 for local processing on November 2, with explicit missing-dependency messages (DEPENDENCIES.md). Do not reopen that same dependency question. Native recognition verification and wider target/privacy/release qualification remain distinct obligations. Existing exact questions in the shared loop state remain pending.

## Verification and limits

The full 163-test suite passed 162 tests first run; the 500ms 4K opening check measured 799ms under parallel load, then passed isolated rerun. The push gate must pass the reconciled tree and rerun any failure alone. No tests, expected pixels or thresholds were changed. Contract/backlog/progress checks and final gate outcome are recorded with the push in shared loop state.

Nine desktop packages still lack published shared vectors. Windows/macOS/mobile runtime parity, physical desktop/provider/accessibility behavior and human security/privacy/licensing/release review are separate obligations. Historical checkpoint counts are not current audit totals.
