# Codex continuous slice queue (XerahS Linux, linux-qt)

You are the Codex CLI (Declan Murphy, wrapper `git-declan`) working on BriarForge/XerahS, the native Linux edition in
`platforms/linux/linux-qt/`. You run unattended for many hours and may be stopped by your 5-hour usage limit at any
moment. Work through the QUEUE below one slice at a time, in order. Aoife (Claude Opus, lead) reviews, integrates and
pushes your work later. **You never push.**

## Where you work
- Workspace: `/home/mike/Work/BriarForge/XerahS/worktrees/codex-stores` (a detached-HEAD git worktree). Work ONLY here.
  Never touch `/home/mike/Work/BriarForge/XerahS` itself or any other worktree (`claude`, `minimax`, ...).
- Paths below are relative to `platforms/linux/linux-qt/` unless they start with `/` or with `tools/`/`product-contract/`
  (those are relative to the worktree root).
- Log file (create if missing): `/home/mike/Work/BriarForge/XerahS/worktrees/.codex-state/queue-log.md`.
- Read once per run, before the first slice: `AGENTS.md`, `platforms/AGENTS.md`, `platforms/linux/AGENTS.md`,
  `platforms/linux/linux-qt/README.md`, and `tools/agent-orchestration/implementer-brief.md` (the shared engineering
  rules; they apply to every slice below, except where this file says otherwise).

## Start of every run (including after a usage-limit stop)
1. Read the log. Find the first slice in the QUEUE whose ID is not logged as `done`, `done-uncommitted` or `skipped`.
2. Run `git status --short`. If there are uncommitted changes that are not listed in the log under a
   `done-uncommitted` slice, a previous run was interrupted mid-slice: read the log's last `started` entry and finish
   that slice. Never discard changes that belong to a `done-uncommitted` slice.
3. Append `## <slice ID> — started — <date time>` to the log, do the slice, then continue with the next one.

## Doing one slice
1. For every requirement the slice names: read the full requirement text in
   `product-contract/capabilities/<CAPABILITY>/SPEC.md`, the matching scenarios in `SCENARIOS.feature`, and the current
   row in `traceability/<CAPABILITY>.json`. Split the requirement into clauses. For each clause find the named test that
   proves it and **read the test body** — a test name is not evidence.
2. Close the real gaps with production code plus Qt Test cases that drive the real production path (native widgets,
   real stores, real workers). Follow existing patterns (OwnedWork workers, guarded async dialogs, PrivateJsonFile,
   stable value-free error codes, EditorSession history). Reuse before adding.
3. Traceability: only set `status` and `audit.classification` to `implemented` when **every** clause is proven by a
   named passing test. Then the note starts `Audit <today>: ` and names the tests clause by clause, and
   `audit.coverage` is exactly
   `Tested Linux candidate seam; native/physical/human qualification is not accepted by this classification.`
   Otherwise keep `partial`, and rewrite the note honestly: what is now proven (with test names) and what remains.
4. Build and test from `platforms/linux/linux-qt`:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j4
   ctest --test-dir build -R '<affected tests>' --output-on-failure
   ctest --test-dir build -j4 --output-on-failure      # full suite, once per slice
   ```
   `editor-window-1/2/3`, `capture-annotation` and `upload-executor` can fail under parallel load: rerun a failure
   alone (`ctest --test-dir build -R '^<name>$'`); a pass alone counts. Tests that need a private D-Bus session or a
   portal may be impossible in your sandbox: list each one by exact name in the log as `sandbox-blocked` instead of
   treating it as a failure, but every other test must pass.
5. From the worktree root: `python tools/platform-progress/update.py`, then
   `python tools/platform-progress/update.py --check` and `python tools/contract-linter/lint.py` (both must pass).
   Do not run graphify and do not commit `platforms/linux/graphify-out/` (`git checkout -- platforms/linux/graphify-out`).
6. Commit: `git add` exactly the slice's files (`git status --short` and `git diff --stat` first; never `build/`), then
   `git-declan commit -m "<message>"`. Message: `[Linux] <imperative outcome sentence>` + blank line + 1-4 lines (what
   changed, which requirement IDs, status changes) + blank line +
   `Written by Codex from the queue in tools/agent-orchestration/contracts/codex-queue.md (slice <ID>).`
   **If the commit is refused** (sandbox: cannot write `.git`, index.lock, permission denied): do not retry and do not
   work around it. Leave the changes in place, log the slice as `done-uncommitted` with the full list of files it
   changed, and continue. Aoife commits them later as Declan.
7. Append to the log:
   ```
   ## <slice ID> — done | done-uncommitted — <commit SHA or "uncommitted"> — <date time>
   - What changed (1-4 lines). Files (for done-uncommitted: every changed path).
   - Requirement status changes (ID: before -> after). ctest result line. sandbox-blocked tests, if any.
   - Decisions taken under the similar-apps rule (one line each), and open product questions for Mike.
   - Findings: problems you noticed but did not fix, with file:line, or "none".
   ```
8. If a slice cannot be finished after two honest attempts, or finishing it needs a decision listed under "Never decide":
   implement and commit only the unambiguous part (if any), log the slice as `skipped` (or `done` with the remaining
   part listed), state exactly what blocks it, and move on. Never leave a broken build behind: if the tree does not
   build at the end of a slice, revert that slice's own edits with `git checkout -- <files>` before moving on.

## Decisions
When the contract leaves a behaviour unspecified and it is not in "Never decide": follow the reference XerahS app
(read-only, `/home/mike/Work/ShareX/XerahS`); if it has no answer, pick what the majority of ShareX, Flameshot and
Greenshot do. Record the decision in one line in the traceability note and in the log.

## Never decide (leave partial, log the question for Mike)
Product-contract text changes; human security review items (Secret Service storage, import quarantine/word lists,
`.sxie` import, destination connection tests, legacy plaintext secrets: COMPCAT-008, SETCAT-008, CORE-013, SETCAT-007);
release trust/signing/updates/plugins/MCP/assistant authority (CORE-011/022/026/028, IFCAT-011/012/013,
COMPCAT-012/014, SETCAT-017); background removal model and licence (EU-009/010/011); EO-009 pin service approval;
EO-010 wallpaper display/layout; EO-004 format-disposition metadata; CORE-015 scroll alignment threshold; mixed-scale
monitor mapping and recapture (RC-003/004/012/022/027, CORE-014); cursor/HDR via the portal (RC-013/015); smart
padding and retention limits (EC-013, ES-018/020); ES-014/030 companion policy; EA-009 Step styles; EU-019 Konami
shader; per-field ledger semantics (SETCAT-001/002/020, ESET-001/015); platform/edition dispositions and
qualification (CORE-001/003/004/005/007/019/029/030/033-037).

## Hard rules
- Never push. Never run `git stash`, `git reset --hard`, `git rebase`, `git checkout <branch>`, `git branch`,
  `git commit --amend`, or anything that moves HEAD other than `git-declan commit`. Never commit with plain `git commit`.
- Never edit `product-contract/`, `README.md` files, `tools/` (except running them), or other worktrees.
- Never weaken or delete an existing test assertion to make something pass. If an existing test must change because
  behaviour changed on purpose, change only that assertion and say why in the log.
- No new third-party dependency. Qt modules already linked are fine.
- No secrets, captures, clipboard contents, recognised text, user paths or credential values in logs, diagnostics or
  test fixtures.

## QUEUE

### C01 — Fix the cancel-upload regression in the core-platform work in progress
HEAD carries Codex's earlier core-platform work in progress (commit "Core platform work in progress: notifications,
scheduling, uploads and diagnostics"). `capture-upload` fails: test `transfer(cancel)` asserts
`!receipt->pipeline.upload`. Find why a cancelled transfer now records an upload in the pipeline receipt (read
`tests/CaptureUploadTest.cpp`, `app/CapturePipeline.cpp`, `app/CaptureActions.cpp`, `app/FileUploadController.cpp`) and
fix the production code so a cancelled upload never records an upload result. Do not change the test. Full suite green.

### C02 — CORE-020 uploads
Close CORE-020 (capture/file/watch-folder uploads, cancellation, hostile responses, retry). Decision already taken:
retry is bounded to 3 attempts with backoff, only for idempotent or keyed operations (uploads with a stable request
key, reads), never for non-idempotent actions without a key.

### C03 — CORE-021 destinations
Close CORE-021 (destination catalogue, custom HTTP, SXCU import results, URL/deletion results) as far as it does not
need the Secret Service security review.

### C04 — CORE-010 notifications and results
Close CORE-010. Decision: if a notification offers actions, a stale action (target deleted/moved) returns a safe
result with a visible message; otherwise state that notifications carry no actions. Private previews never appear in
notifications when the privacy option says so.

### C05 — CORE-016 scheduled and automatic capture
Close CORE-016 (lock/storage/permission/privacy behaviour of scheduled runs, stop controls, no overlap).

### C06 — CORE-023 capture history
Close CORE-023 (history search, paging, retention, outcomes, removal). Keep the existing journal format.

### C07 — CORE-024 launch protocol
Close CORE-024 (single-instance launch/activation protocol: versioning, correlation, bounded intake, redaction).

### C08 — CORE-027 diagnostics
Close CORE-027. Decision: an explicit "Export diagnostics…" action writes a redacted, versioned bundle (app version,
Qt/OS versions, settings field states without values, recent stable error codes with timestamps) after a preview the
user confirms; never automatic; never captures, clipboard, recognised text, paths or secrets.

### C09 — CORE-018 recording audio
Close the unambiguous parts of CORE-018 (consent before microphone/system audio, device loss mid-recording, clear
errors). Do not change codec policy.

### C10 — CORE-009 translation leftovers
MiniMax found strings that no translation context covers. Route them through translation (Q_OBJECT classes use `tr()`;
others `QCoreApplication::translate("<ClassName>", ...)`; stable ids, diagnostic codes and stored values stay literal;
static maps store source text and translate on use):
- `app/NativeAnnotationTools.cpp` (tool labels, Delete, ordering actions, `annotationEditMessage`, instructions),
- `app/CaptureController.cpp` `CaptureController::instructions()` (around line 71),
- `app/CaptureActions.cpp` `actionLabel()` — make it reuse the same source table as `ActionSelectionDialog`'s
  `labelSource()` instead of keeping a second copy,
- user-visible text in `app/EditorWindow.cpp`, `app/UploadDestinationsDialog.cpp`, `app/FileUploadResultDialog.cpp`,
  `app/ScheduledCapture.cpp`, `app/NativeNotifications.cpp`, `app/EditorUpload.cpp`.
Extend `tests/LocalizationExpansionTest.cpp` with the same pseudo-locale probe the existing slots use.

### C11 — CORE-009 appearance
Close what is unambiguous in CORE-009: runtime language change re-translates open windows, high-contrast and
reduced-motion desktop preferences are honoured where Qt exposes them, with tests. Keep partial for anything that needs
physical qualification.

### C12 — editor-window-2 crash
`editor-window-2` crashes intermittently with SIGSEGV inside glib `g_main_context_iteration` (about 1 in 3 full-suite
runs; passes alone). Reproduce with `ctest --test-dir build -R '^editor-window-2$' --repeat until-fail:30` and under
parallel load. Find the real cause (likely an object deleted while a queued event, timer or portal/D-Bus reply still
targets it, or a nested event loop) using a debug build, `QT_FATAL_WARNINGS`, ASan
(`-DCMAKE_CXX_FLAGS=-fsanitize=address` in a separate build dir such as `build-asan`) or gdb. Fix the product or test
lifetime bug; do not add retries or skips. Log the root cause.

### C13 — EC-014 and EC-017 canvas
Close EC-014 (compositing into the immutable source, clearing only represented objects, alpha/profile preservation,
full undo) and EC-017 (finite dimensions/resource limits, progress and cancellation for costly operations, session
atomicity).

### C14 — Editor output and session audits
Close EO-001, EO-006, EO-015, EO-016 and ES-009: each already lists proven clauses; find the missing clauses and prove
or implement them.

### C15 — Effects
Close EE-003, EE-007, EE-009 and EE-013 (parameter normalisation, file-path parameters, format/colour-space
preservation, effects dialog keyboard/accessibility).

### C16 — Utilities
Close EU-002, EU-016, EU-017 and EU-018 (colour picker, utility accessibility, cancellation/lifetime, failure
coverage) for the utilities that exist. Background removal stays out.

### C17 — Editor settings
Close ESET-003, ESET-006, ESET-009 and ESET-014 (atomic store with previous values, defaults propagation, validation,
boolean preferences).

### C18 — Region capture worker and topology failures
Close RC-020 (capture work off the GUI thread, responsiveness, non-blocking cancel) and RC-018 (topology mismatch
failures) where the existing code already does the work; add the missing evidence.

### C19 — Workflow action families
WFCAT-011: let saved workflows run the OCR, QR scan, hash, combine/split and effect actions through the shared
post-capture engine, with input/metadata validation and tests through the real workflow runner.

### C20 — Workflow closure
Close WFCAT-005, WFCAT-006, WFCAT-009, WFCAT-010, WFCAT-014 and WFCAT-015 (order and failure policy, cancellation,
output validation, uploads, concurrency, retry with the frozen snapshot — the CORE-020 retry decision applies).

### C21 — Interface states
Close IFCAT-003, IFCAT-005, IFCAT-006, IFCAT-016 and IFCAT-017 (state matrix, confirmations and modal precedence,
progress/cancellation, stable error presentation, availability) for every current native window.

### C22 — Layout robustness
IFCAT-007: right-to-left layout, 200% text scale and long pseudo-locale strings for every current dialog: no clipped
or overlapping controls, focus order intact. Tests with the offscreen platform.

### C23 — Settings closure
Close SETCAT-010, SETCAT-014, SETCAT-015 and SETCAT-019 (reset previews, collection recovery, private-data retention
and removal, settings import/export).

### C24 — Legacy history migration
COMPCAT-009: explicit, user-started import of the previous XerahS app's capture history (read the reference app's
history formats in `/home/mike/Work/ShareX/XerahS`) into the native history, with a preview, neighbouring-record
quarantine for malformed records, no network access, and fixture tests. Keep the row `partial` and note that the human
security review of import quarantine is still pending.

### C25 — Migration closure
Close what is unambiguous in COMPCAT-003, COMPCAT-007 and COMPCAT-017 (isolated candidate import, semantic
validation, recoverable backup), with fixture tests. Security review items stay open.

## When the queue is finished
Append `## QUEUE COMPLETE — <date time>` to the log with a summary: commits (or uncommitted slices), requirement status
changes, sandbox-blocked tests, open questions for Mike, and findings. Then stop.
