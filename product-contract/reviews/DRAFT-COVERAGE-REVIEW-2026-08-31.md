# Contract approval review — 2026-08-31

Decision state: **All 15 contract definitions approved by the human product owner; activation evidence pending**

This packet summarizes the first exhaustive draft linkage of the pinned
KovaForge 0.29.0 census. The product owner approved all 15 version `0.1.0`
contract definitions through an interactive instruction on 2026-08-31. The
durable decision is `APPROVAL-2026-08-31-ALL-PACKAGES.json`. Approval makes the
requirements normative and authorizes implementation; it does not close the
baseline census, activate capabilities, prove compatibility, or authorize a
release.

## Coverage result

All 1,592 parity rows now link to an approved Product Contract package:

| Ledger | Linked | Total |
|---|---:|---:|
| Capability | 44 | 44 |
| Settings | 922 | 922 |
| Workflow | 436 | 436 |
| Interface | 174 | 174 |
| Compatibility | 16 | 16 |

The manifest contains 15 approved capability packages and 286 stable normative
requirement IDs. No package is active. The baseline census remains open because
runtime, semantic, compatibility-fixture, and specialist reviews are not
complete.

## Approved packages

| Package | Requirements | Principal decision |
|---|---:|---|
| `FILENAME-GENERATION-001` | 17 | Token, counter, date, path-safety, and collision behavior |
| `POST-CAPTURE-ACTIONS-001` | 16 | Ordering, continuation, cancellation, and result recording |
| `REGION-CAPTURE-001` | 21 | Permission, selection, scaling, target loss, confirm, and cancel |
| `EDITOR-SESSION-001` | 22 | Editor session, document, history, selection, and export foundation |
| `EDITOR-ANNOTATIONS-001` | 20 | Complete annotation families and manipulation behavior |
| `EDITOR-EFFECTS-001` | 18 | Effect catalog, parameter, preview, determinism, and failure rules |
| `EDITOR-CANVAS-001` | 17 | Canvas creation, navigation, geometry, composition, and flattening |
| `EDITOR-OUTPUT-ACTIONS-001` | 16 | Copy, save, upload, print, pin, wallpaper, and continuation |
| `EDITOR-SETTINGS-001` | 16 | Persisted editor preferences, toolbar, recents, and shortcuts |
| `EDITOR-UTILITIES-001` | 19 | Picker, QR, hashing, conversion, comparison, and background removal |
| `SETTINGS-CATALOG-001` | 20 | All remaining settings, secure storage, migration, scope, and portability |
| `WORKFLOW-CATALOG-001` | 18 | All remaining workflow/action semantics and shared entry-point behavior |
| `INTERFACE-CATALOG-001` | 18 | GUI accessibility, CLI, MCP authority, and provider surfaces |
| `CORE-PLATFORM-001` | 30 | Shell, capture, recording, media, upload, history, automation, operations, distribution, and mobile disposition |
| `COMPATIBILITY-CATALOG-001` | 18 | Safe import, export, round-trip, migration, fixtures, and runtime observation |

## Accepted high-impact decisions

1. Secrets use native credential services and ordinary configuration exports do
   not carry plaintext secrets.
2. MCP and assistant text cannot manufacture human consent or authority;
   credential changes, external publication, destructive actions, plugin
   installation, and sensitive capture/history access require trusted review.
3. Plugins execute only in an isolated, permissioned host and never inside the
   trusted UI or credential process.
4. Compatibility is semantic and evidence-backed, not an implicit byte-for-byte
   promise; unknown executable or authority-bearing content is quarantined or
   rejected.
5. Windows, macOS, and Linux remain required dispositions for all supported
   behavior. Native mechanism and presentation may differ while domain results
   remain equivalent.
6. Experimental mobile remains outside the BXIP001 desktop target set unless the
   product owner explicitly changes scope.
7. Native XerahS retains `com.xerahs.native` / `XerahS Native` identity and does
   not overwrite the reference application's settings without explicit import.
8. Distribution remains GPLv3 and dynamically links LGPL Qt; signed provenance,
   update recovery, coexistence, and platform-native packages are required.

## Evidence still required before activation

- Source-structure reconciliation for reflection, generated registrations,
  stale graph data, dead code, false positives, and omissions.
- User-journey review from launch through success, cancellation, failure, retry,
  and recovery for visible operations.
- Representative runtime observation on Windows, macOS, and Linux, including
  permission denial and degraded behavior.
- Per-setting accepted defaults, validation, privacy, migration, and downstream
  effects using `setting-disposition.schema.json`.
- Per-workflow accepted inputs, outputs, authority, side effects, failure,
  cancellation, and concurrency using `workflow-action.schema.json`.
- Complete keyboard, screen-reader, scaling, localization, CLI, MCP authority,
  and provider-form evidence using `interface-disposition.schema.json`.
- Sanitized configuration, history, uploader, plugin, ImageEditor, and MCP
  fixtures plus adversarial cases and runtime-observed round-trip behavior.
- Requirement-to-conformance traceability and accepted platform results.
- Security, privacy, accessibility, compatibility, licensing, and final product
  owner review records; any deviation must be explicit and time-bound.

## Product-owner decision record

The human product owner approved the complete package list in this packet on
2026-08-31. Activation and census closure remain separate decisions.

- [x] Approve the 15 packages as written, move their manifest lifecycle from
  `draft` to `approved`, and authorize native implementation against version
  `0.1.0`.
- [ ] Request revisions, identifying package IDs and requirement IDs.
- [ ] Approve an explicit scope change, deviation, or time-bound waiver with
  rationale, owner, affected platforms, expiry, and replacement plan.
- [ ] Sign baseline census closure only after every closure requirement in
  `reference-baselines/provenance/README.md` has accepted evidence.
