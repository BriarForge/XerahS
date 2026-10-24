# Linux production backlog — 24 October 2026

Audited source: `26ddb93ea5587b3e2e1c7ce2c8cb610b447f2115`. Scope is the English
Product Contract 0.3.1 and every desktop capability, including catalogue rows and
supported Linux environments. This is a candidate implementation audit, not
release acceptance. [Machine-readable counts](PRODUCTION-BACKLOG.json) and all
15 [requirement maps](traceability/) enumerate the work. No estimated completion
percentage is used.

The specification now contains **325 numbered desktop requirements** (PCA-024
was added after the earlier 324-row audit). The maps currently label **99
implemented, 192 partial, 33 not-started, and one outside Linux desktop scope**
(CORE-029). These labels are unweighted. A catalogue requirement can cover
hundreds of linked items, and an implemented domain seam is not an approved
production release. Automated test evidence and live/human qualification are
separate. Source/fixture presence is recorded before the mandatory push gate;
the gate supplies execution results for the exact committed tree.

## What exists now, and what remains

| Capability | Native code and automated evidence now present | Principal remaining work |
|---|---|---|
| CORE | Registry-driven app, onboarding, capture variants, recording/audio, uploads/keyring, scheduling, history and watch folders; domain/native/integration suites | Explicit migration and rollback, diagnostics/recovery/export, signed updates, packaging/provenance, localization, isolated plugins/assistant, full desktop/a11y/consent qualification |
| Filename generation | 51 published vectors, per-profile CounterStore with locks/restart/failure tests and native counter filenames | Native publication currently ignores counter commit failure; full native failure/crash/collision tests and scoped random-file grants |
| Post-capture actions | Shared ordered engine, native annotate/save/copy/reveal/upload/URL clipboard/pin/print, receipts, journal and retry tests | Complete action families and retained-media recovery, privacy/cleanup/retention, full retry/idempotency and target matrix |
| Region capture | Frozen portal snapshot, overlays, keyboard selection, window snapping and native cancellation tests | Authoritative per-monitor mixed-DPI/rotation mapping, cursor/live/HDR policy, resize handles/presets, last-region persistence, performance and physical a11y |
| Editor session | Validated document/assets, history, guarded immutable save/copy/print/open, annotation persistence and published vectors | Full supported fields/colour/compatibility, recovery/temp retention, performance, approved visual goldens and human qualification |
| Annotations | All 20 tool modes and shape/path/asset/cut-out rendering/undo fixtures | Complete text font/style/alignment and balloon controls, step styles/start/reset, border/arrow/shadow fields, all-tool clipboard/assets/persistence, physical accessibility |
| Effects | All 232 census entries are visible; local effect implementations, preview/Apply, recents/favorites/presets and SXIE fixtures | Approved background-removal model, full parameter/asset/resource/native qualification; SXIE human security review. Internal regression images do not replace accepted shared goldens |
| Canvas | New/Open, viewport, crop/auto crop, resize/rotate/flip, clear/flatten, compare and inserted image tools | Full clipboard/screen insert, background composition, all-annotation affine transforms and complete native colour/resource matrix |
| Editor output | PNG save/sidecar repair, Copy Image, Print, capture pins/uploads and native failure tests | Editor JPEG/WebP choices, complete upload/pin/task-mode hosts, wallpaper, every output fault/permission/callback path and target/a11y qualification |
| Editor settings | Private preferences, recents, appearance, window placement, toolbar and effects history tests | Full ledger/default/downstream coverage, font/style/tool/background preferences, convenience options and physical/native qualification |
| Utilities | Colour formats/picker, QR generation, hash checker, ICO/video conversion and comparison tests | QR scan/OCR dependencies and background-removal model await Mike; picker magnification/coordinates, full utility availability and native qualification remain |
| Application settings | Private versioned fields, General/Capture/Hotkeys/Recording, schedule preferences, credential references and storage tests | Complete 922-row dispositions/defaults, import/export/migration, all scopes and secret/resource boundaries; preference existence is not full ledger acceptance |
| Workflows | Saved capture definitions, authenticated IDs, global shortcuts, durable execution observations, native capture/upload/recording components | Execution retention, complete 436-row semantic schemas/entry points/status taxonomy, full recording invocation indexing, MCP/assistant and all-resource arbitration |
| Interfaces | Shared registry/QActions and CLI activation/discovery/result queries with actual-process tests | Full CLI operations/progress/signal/cancel, MCP schemas/authority, complete provider interfaces, unavailable explanations, RTL/localization/scaling and assistive technology |
| Compatibility | XANN, current native JSON, SXCU and SXIE isolated parsers/fixtures/unknown preservation | Explicit Avalonia settings/workflow/history/secret migration, rollback/idempotency, SQLite/XML snapshots, plugin/MCP compatibility and accepted legacy/cross-platform fixtures |

The nine packages without published shared vector files remain **partial native
evidence**. CMake names those absences explicitly; no vector or expected output
was invented, waived or generated from Linux behavior. Existing shared vector
files and approval records are unchanged.

## Work order

0. **Restore verified minimum-Qt compatibility (CORE-036, EE-018).** The local
   audit gate passed 114/114 targets, but [Ubuntu CI 40098357984](https://github.com/BriarForge/XerahS/actions/runs/40098357984)
   failed while compiling drawing effects: QImage::flipped is a Qt 6.9 API.
   Use the shared compatibility helper and independently specified reflection
   matrices; require green exact-revision Qt 6.4.2 CI before claiming compatibility.
   [Qt's API reference](https://doc.qt.io/qt-6/qimage.html#flipped) documents the
   version boundary. No minimum, vector, golden or test is weakened.

1. **Fix counter publication reporting (FN-016, PCA-007, PCA-013).** A published
   PNG can currently be reported as complete success even when persisting the
   next counter fails. Add a real native fault fixture, demonstrate the failure
   before the fix, preserve the successful file and report the persistence fault.
2. **Execution retention (CORE-023, WFCAT-002/017, SETCAT-015).** The bounded
   store has no user-facing metadata removal. Previously unfinished work is
   preserved; adapt it to CommandRegistry/feature installers and OwnedWork.
   Use current private-state infrastructure and preserve existing formats,
   outputs and unknown/newer records. Do not replay unconfirmed executions.
3. **Explicit migration and recovery (ROOT-IDENTITY-001, CORE-002/012/027,
   COMPCAT-003/004/007/009/017, SETCAT-019).** Isolated bounded candidates, preview,
   source backup, user confirmation, per-record outcomes and rollback first.
   Never inspect or mutate live Avalonia settings as a development test.
4. **Native distribution (CORE-028).** Install rules and reproducible Arch plus
   AppImage/Flatpak candidate packaging, dynamic Qt/GPL notices, clean install,
   replacement/rollback/uninstall fixtures. Signing keys and release approval
   remain outside the repository and require the release owner.
5. **Highest-value editor/catalogue gaps.** Text/step/style options and durable
   defaults, canvas insert/background/affine behavior, remaining output hosts,
   settings/workflow/interface coverage. Preserve already shipped source and
   test boundaries; any unresolved schema/semantic choice goes to Mike.
6. **Diagnostics and signed-only update checks (CORE-027/011).** Redacted,
   bounded, user-reviewed export and explicit recovery; authenticated provenance
   and a release-owner trust anchor before any live update qualification.
7. **Complete native qualification and remaining catalogue operations.** Per-
   monitor capture, Linux environment matrix, keyboard/screen readers, colour,
   hardware/audio/codec/resource/performance, privacy/security and parity review.

## Waiting on Mike — do not implement by guessing

- Approve the local background-removal model, provenance/license/device policy.
  The relevant requirement IDs are **EU-009..EU-011**; EE-009..EE-011 govern
  effect pixel/cancellation/history behavior. The task handoff's EE prefix is
  not a change to the contract.
- Approve **zxing-cpp** for QR scanning (EU-004) and **Tesseract** for OCR. No new
  third-party dependency is authorized by this audit.
- Supply/approve the scrolling alignment threshold (CORE-015). The existing
  placeholder is not production acceptance.
- Human security review of **SXIE import and Secret Service credentials**.
- Existing unresolved product-owner dispositions remain explicit: editor raster
  format policy, complete legacy document fields/sidecars where still pending,
  isolated plugin model/provenance, and the optional shader/easter egg. Generic
  continuation does not sign an approval record or silently select a policy.
- Release trust anchors/signing/provenance, shared visual goldens, privacy,
  accessibility, compatibility, parity and release approval remain human gates.

## Supported-target completion gate

The supported reference edition must qualify on Ubuntu LTS, Fedora Workstation
and Arch, with GNOME, Plasma and declared wlroots behavior, Wayland first and
X11 fallback. CI on Ubuntu/Qt 6.4.2 and local automated fixtures are necessary
but do not prove physical permission dialogs, screen lock, mixed scaling,
monitor removal, audio devices, clipboard clients, printers, keyrings or screen
readers. Candidate editions do not replace the supported reference profile.

Completion is declared only when every applicable requirement and linked ledger
item has accepted behavior/evidence and all required target and human gates pass.
If only Mike decisions or live desktop qualification remain, stop and list them
in the requested progress log; do not claim full-contract 100% completion.
