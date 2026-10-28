# Linux production backlog

Audit date: 2026-10-28. Reviewed source: `e7528f29a44236ddbb153e1ab28039697573e376`. Product Contract 0.3.1.

This is a source/test audit of every capability in `product-contract/manifest.yaml`,
not release approval or an accepted platform disposition. The 325 desktop rows
classify **90 tested candidate implementations**, **200 partial**, **27 not started**,
**7 blocked**, and **one mobile-scope-only row** (CORE-029). All 20 MOB requirements
are outside Linux desktop scope. Linked field, action and algorithm ledgers remain
obligations; these unweighted labels do not measure product readiness.

The machine-readable inventory is [PRODUCTION-BACKLOG.json](PRODUCTION-BACKLOG.json).
Every row in [traceability/](traceability/) carries its normative specification line,
classification and reviewed revision. Capability review anchors name current production
and automated test files, method locations and hashes. Anchors are **EXTRACTED**
source evidence for the named candidate seam; they do not imply full row coverage.
Graph relationships were navigation aids and were not used as conformance evidence.

## Capability findings

| Capability | Candidate counts | Current source/test finding | Remaining scope |
|---|---|---|---|
| [FILENAME-GENERATION-001](traceability/FILENAME-GENERATION-001.json) | 25 implemented, 2 partial | Immutable expansion context, Unicode-scalar sanitization, token/random vectors, preview and persisted reservation tests are present. Native failed-counter publication now retains the published PNG and reports failure. | Explicit native random-file permission flow and crash/power-loss atomicity between publication and counter persistence remain incomplete. |
| [POST-CAPTURE-ACTIONS-001](traceability/POST-CAPTURE-ACTIONS-001.json) | 12 implemented, 12 partial | Shared catalogue ordering, artifact dependencies, cancellation, canonical results and retries have engine/native tests. CaptureActions uploads saved paths, but its no-save path still encodes QBuffer bytes rather than a managed physical temporary file. | Managed temporary.file lifecycle, quick-task and before-upload native flows, guarded native deletion, complete recovery/retention and all action-family qualification remain incomplete. |
| [REGION-CAPTURE-001](traceability/REGION-CAPTURE-001.json) | 9 implemented, 3 not-started, 15 partial | RegionSession implements physical geometry/state vectors; native settings freeze quick-confirm/snapping before acquisition, handles resize, and LastRegionStore atomically saves geometry after acquired pixels through OwnedWork. | CaptureController still derives one global scale from the acquired portal bitmap and uses QScreen names. Authoritative mixed-scale/rotation/stable identity and pre-acquisition last-region validation, cursor/HDR/privacy/performance and physical accessibility remain incomplete. |
| [EDITOR-SESSION-001](traceability/EDITOR-SESSION-001.json) | 18 implemented, 3 not-started, 9 partial | Production session/history, bounded XANN parsing, exact rectangle compositor, source binding and owned native save/copy/print/history paths have automated fixtures. Partial raster/companion acknowledgement remains honest. | Complete color/profile and all-tool compatibility, separate autosave/recovery and retention, performance, approved shared visual fixtures and physical/native qualification remain incomplete. Legacy fallback sidecar policy remains blocked. |
| [EDITOR-ANNOTATIONS-001](traceability/EDITOR-ANNOTATIONS-001.json) | 20 partial | Native tools reach all modes; text/balloon creation and editing use guarded asynchronous dialogs with family/style/alignment/text color/tail/fractional size. Numeric Step and fractional strength edits, create admission bounds, history/reload and ownership regressions are tested. | Text border/fill semantics, border/arrow/shadow and all-tool asset/transfer/affine breadth, ellipse/blur options and physical accessibility remain incomplete. Alphabetic/Roman/start/reset semantics await product answers. |
| [EDITOR-EFFECTS-001](traceability/EDITOR-EFFECTS-001.json) | 9 implemented, 9 partial | Census tests enumerate 232 stable IDs and parameter inventories. Local runner, owned preview/Apply/Cancel, history, seeds, recents/favorites and inert unknown preset data have tests; catalogue presence alone does not establish each algorithm. | Approved local remove_background model, every parameter/asset/algorithm boundary and accepted cross-platform golden, hardware/native qualification and human SXIE review remain incomplete. |
| [EDITOR-CANVAS-001](traceability/EDITOR-CANVAS-001.json) | 9 implemented, 2 not-started, 6 partial | New/Open, viewport-only zoom/pan, crop, auto-crop, canvas anchors, quarter/custom rotation, clear and comparison use tested production operations and owned preparation. Unknown/unsupported transforms are refused or retained explicitly. | Clipboard/screen insertion, all-tool resize/directional transforms, background composition, color/resource breadth and target qualification remain incomplete; smart padding is blocked on policy. |
| [EDITOR-OUTPUT-ACTIONS-001](traceability/EDITOR-OUTPUT-ACTIONS-001.json) | 1 not-started, 15 partial | Owned immutable save/copy/print paths and native PNG/JPEG/WebP Save As are present with explicit quality/matte and encoded-raster companion hashes. Tests cover atomic failures and companion-only repair without changing the durable path early. | Complete editor upload/pin/task host integration, wallpaper, every codec/profile/callback/permission/disk fault and target/accessibility qualification remain incomplete. Format selection metadata is still draft and persisted encoding defaults unapproved. |
| [EDITOR-SETTINGS-001](traceability/EDITOR-SETTINGS-001.json) | 2 not-started, 14 partial | Private preference, recent-image, drawing, toolbar, appearance and window-placement stores/dialogs have atomic/unknown/conflict and native tests. Text font controls are per-object content, not persisted font preference coverage. | Full approved field ledger/defaults, saved text/step/emphasis/background defaults, convenience preferences, retention/backup policy and all-field/all-platform vectors remain incomplete. |
| [EDITOR-UTILITIES-001](traceability/EDITOR-UTILITIES-001.json) | 5 blocked, 8 implemented, 6 partial | Production streamed hashes, QR generation, ICO writing, image comparison and structured atomic video conversion have tests. ToolsFeature exposes native windows and missing dependencies honestly. | QR scanning/OCR dependencies and background-removal model remain unapproved; picker coordinates/magnification, every codec/input/native lifecycle and physical accessibility qualification remain incomplete. Konami decision remains blocked. |
| [SETTINGS-CATALOG-001](traceability/SETTINGS-CATALOG-001.json) | 3 not-started, 17 partial | ApplicationSettings uses PrivateJsonFile, field-level repair and portable hotkey plans. Native settings expose region quick-confirm/snapping and scheduling/audio options. Execution-history metadata can be removed explicitly without removing outputs. | Full 922-row approved ledger/default/scope coverage, import/export/migration/rollback, retention/backup, provider fields and reviewed secret boundaries remain incomplete. |
| [WORKFLOW-CATALOG-001](traceability/WORKFLOW-CATALOG-001.json) | 18 partial | Saved capture definitions, authenticated activation IDs, shared shortcuts and durable execution snapshots connect to native capture pipelines. Confirmed metadata removal preserves outputs and refuses stale/newer/corrupt records. | All 436 ledger actions, complete recording/clipboard/upload entry-point and state taxonomy, bounded search/paging/retention, retry/arbitration and MCP/assistant authority remain incomplete. |
| [INTERFACE-CATALOG-001](traceability/INTERFACE-CATALOG-001.json) | 3 not-started, 15 partial | CommandRegistry feature installers and authenticated activation route shared semantic requests. Native/process tests cover busy admission, redacted execution discovery/results, modal lifetime and stable errors. | Complete CLI progress/cancel/signal/exit semantics, MCP schemas and human authority, provider schemas and all-surface keyboard/reader/localization/scaling and accepted unavailable dispositions remain incomplete. |
| [CORE-PLATFORM-001](traceability/CORE-PLATFORM-001.json) | 2 blocked, 1 not-applicable, 4 not-started, 30 partial | Native identity, single-instance protocol, shell commands, permission/capture, scheduling, recording, upload, watch-folder and execution-history components have candidate tests. No catalogue or target-wide acceptance is inferred. | Full ledger dispositions, explicit migration/recovery/diagnostic export, signed distribution/update/plugin trust, NuGet governance evidence, target matrix and human security/privacy/a11y/licensing/parity acceptance remain incomplete. |
| [COMPATIBILITY-CATALOG-001](traceability/COMPATIBILITY-CATALOG-001.json) | 6 not-started, 12 partial | XANN, private native JSON, isolated SXCU secret-reference import and SXIE data parsing have bounded/malformed/unknown-field fixtures. Tests do not constitute legacy migration acceptance. | Explicit Avalonia configuration/history/secret migration, XML/SQLite consistent snapshots, rollback/idempotency, plugin/MCP compatibility and accepted runtime/cross-platform legacy fixtures remain incomplete. |

## Changes since the previous full audit

RC-007 changes from partial to implemented candidate. RegionSession selects the
confirmation state from the frozen quick-confirm preference; the native
`PortalScreenshotTest::regionOptionsFreezeBeforeAcquisitionAndAllowKeyboardAdjustment`
fixture verifies release waits for Enter when disabled and confirms when enabled.
The profile controls and independent field-repair tests now exist. This closes this
confirmation row; it does not accept mixed-scale topology or physical qualification.

Other recent implementation remains recorded as partial requirement coverage:
confirmed execution-history metadata removal; explicit PNG/JPEG/WebP Save As;
last-region metadata storage after pixel acquisition; region handles; native
Text/SpeechBalloon formatting, tail and asynchronous creation; full-range numeric
Step editing; fractional strength/font-size preservation and creation admission
bounds. These features are preserved, not scheduled for reimplementation.

## Ranked work

1. Remaining native annotation options and fields (EA-005/006/008/010/011/016/019): choose one approved text border/fill, ellipse/blur, border/arrow/shadow or all-tool asset/transfer slice. Preserve existing asynchronous formatted creation/editing, fractional/default values, numeric Step admission and history; do not redo completed controls.
2. Managed temporary uploads (PCA-006/019): replace the native no-save QBuffer/fileBytes path with the contracted managed temporary.file lifecycle, acknowledgement and cleanup behavior. Resolve any unspecified retention policy with Mike; keep credential review gates intact.
3. Native post-capture quick-task, before-upload and explicit deletion flows (PCA-014/020/021): one approved action per iteration through the shared engine, stable results and consent. Injected executor tests do not count as native flows.
4. Capture breadth (RC-013/015/019/020, CORE-014/016): cursor option and output exclusion, remaining overlay/picker options and physical/performance coverage under approved semantics. Recapture and mixed-monitor identity/mapping remain blocked below; retain durable RC-017 storage and tested RC-007 confirmation.
5. Canvas and editor output breadth (EC-004/009/011/013/014/017, EO-007/009/010/011/014/016): clipboard/screen insertion, all-tool affine/flatten behavior and remaining output hosts. Background/smart-padding and new saved quality/matte defaults require decisions below. Preserve tested PNG/JPEG/WebP Save As and companion-only repair.
6. Application/workflow breadth (CORE-017/018/019/020/021/023/024/025; WFCAT-004/008/010/011/012/014/015/017): recording/upload/watch-folder/action-family gaps and bounded history search/paging/retention. Confirmed execution metadata removal exists; do not redo it or infer all history coverage.
7. Settings/interfaces/compatibility (SETCAT-*, IFCAT-*, COMPCAT-*): approved field/action ledgers, complete CLI operations, isolated explicit import/export/migration with backup/rollback, supported legacy fixtures and all native value boundaries. Secret, plugin and MCP authority remain blocked.
8. Privacy/recovery and qualification after real features (ES-018/020, PCA-012/013, CORE-027/030/036): approved retention and separate recovery state, native fault/resource/performance matrix and Ubuntu/Fedora/Arch with GNOME/Plasma/wlroots/Wayland/X11. Investigate the recorded one-off parallel scroll test failure without changing the unapproved threshold or weakening tests. Human security/privacy/a11y/licensing/parity and release gates remain.
9. Blocked Step breadth (EA-009): alphabetic/Roman mappings, explicit start/reset and tail/all-style behavior wait for the exact product questions below. Numeric allocation and full-range asynchronous numeric editing are tested candidates; do not redo them.

## Blocked on Mike

- EA-009 alphabetic/Roman styles and reset: approve whether alphabetic 1 maps to A, what 0 means, casing and rollover after Z; approve Roman handling of 0 and values above 3999; define whether reset affects only future allocation or existing objects and how the explicit next starting value interacts with existing larger values. Current prose requires these features but supplies no mapping/reset vectors. Numeric editing within the existing version-1 document range can proceed independently; do not invent these policies.
- RC-027 native recapture: which authoritative physical topology source may validate the stored display bounds before requesting any screenshot? The current portal path derives physical scale from the acquired image, while RC-027 requires last-region-invalid without capturing. Approve a pre-acquisition mapping source or a contract disposition; do not infer geometry or request pixels to validate stale state. Candidate durable RC-017 storage is now implemented; native recapture remains blocked on this pre-acquisition decision.
- EO-004: format-disposition.json still says draft-selection-pending-product-owner-approval although the approved SPEC requires PNG/JPEG/WebP. Please reconcile that selection metadata and approve any future persisted quality/matte defaults before activation; this slice requires explicit values and introduces no saved defaults.
- EU-004 and OCR workflows: approve zxing-cpp and Tesseract dependencies before recognition work.
- EU-009/010/011 and remove_background: approve the local model, licence/provenance/integrity, tensor/device policy and any explicit download/setup behavior.
- CORE-015: what exact approved alignment threshold replaces kScrollAlignmentThreshold? Do not change the placeholder or claim scroll qualification.
- CORE-013, SETCAT-007: human security review of Secret Service storage remains pending. EE-015/COMPCAT-013: human security review of .sxie import remains pending.
- ES-014/030: how should empty-document save reconcile an existing legacy fallback companion without resurrecting old annotations? Which permitted removal/alternate-name policy is approved? Supply the mandatory baseline runtime observation for fallback naming before activation.
- RC-003/004/012/022 and CORE-014: which authoritative per-display physical mapping source or accepted degraded disposition applies when a single portal bitmap cannot establish mixed-scale/rotated monitor mapping? Current native code derives one global scale; Core vectors do not qualify this target.
- EC-013 and ES-020/CORE-027: define the deterministic smart-padding/background rules and approved temp/recovery/diagnostic retention limits before those behaviors are selected.
- SETCAT-001/002/020, ESET-001/015: approve missing per-field ledger semantics/defaults and shared fixtures; source initializers are not normative. No silent contract decisions.
- CORE-011/022/028/030, IFCAT-011/012/013, COMPCAT-012/014: approve release trust/signing/channel/provenance, isolated plugin-host and MCP/agent authority before security-posture or release work. Human product-owner review is mandatory; no agent-only acceptance.
- EU-019: preserve, correct or omit the Konami shader after the required accessibility/performance/security/maintenance review?
- Published shared vectors remain absent for nine desktop packages; accepted visual goldens, legacy/cross-platform runtime observations and security/privacy/a11y/licensing/parity/release acceptance are human gates. Native tests are candidate evidence only.

## Verification and limits

The audit gate configures a Debug Ninja build, builds with eight jobs and runs all
117 registered CTest targets with six-way concurrency. Contract lint, backlog and
generated desktop progress freshness are separate required checks. A green local
suite proves candidate behavior exercised by those tests; it does not qualify
unavailable desktops, hardware, physical accessibility, security review or releases.
Six desktop packages publish shared vector files; nine still lack them. No shared
expected result, contract, waiver or approval record was changed for this audit.
The previous dated findings remain in Git history. Current counts are the ones above.
