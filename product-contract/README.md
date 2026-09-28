# Product Contract

This directory is the source of product truth for native XerahS. It is not a runtime binary.

Humans and agents read it at development time. Native applications in `platforms/` implement it. `conformance/` proves they did.

## Baseline census before contract expansion

BXIP001 requires the pinned KovaForge baseline and parity ledgers to exist before
the Product Contract can credibly claim complete coverage. The reproducible
census lives under `reference-baselines/` and `parity/`; its open review gates
are recorded in `reference-baselines/provenance/README.md`.

Generate and verify it with `tools/baseline-census/census.py`. Inventoried rows
are the contract-writing queue. They are not normative until an approved
capability package supplies stable requirement IDs and acceptance evidence.

## Progressive completion backlog

The maintained work plan is [`backlog/backlog.json`](backlog/backlog.json). It
routes every currently uncontracted parity row to an owned package across
twelve dependency-ordered daily waves. The generated
[`backlog/STATUS.md`](backlog/STATUS.md) is the review-friendly progress view.

Run `python tools/contract-backlog/tracker.py --check` to prove that no parity
row is orphaned and the status view matches the current ledgers. Planning
status and dates do not make behavior normative; Product Contract requirements
and the required human approvals do.

The current human review packet is
[`reviews/DRAFT-COVERAGE-REVIEW-2026-08-31.md`](reviews/DRAFT-COVERAGE-REVIEW-2026-08-31.md).
It summarizes all approved packages, high-impact decisions, and evidence still
required for activation. The durable approval record is stored beside it.

## Implementation readiness

[`READINESS.md`](READINESS.md) states which native targets can start coding,
the start order, the test seams each implementation provides, and the gates
that remain open.

## Conformance vectors

Every capability's `test-vectors.json` follows [`VECTORS.md`](VECTORS.md): one
structure, one set of comparison rules, and a headless test seam per operation
that every native implementation and Linux edition uses.

## Qualification capabilities (Wave 1)

These packages are selected as the first contract-writing tranche.

| ID | Intent |
|---|---|
| `FILENAME-GENERATION-001` | Token expansion, counter padding, date formatting, illegal-character handling |
| `POST-CAPTURE-ACTIONS-001` | Ordered post-capture actions, failure continuation, task-result recording |
| `REGION-CAPTURE-001` | Interactive region selection, permissions, DPI/monitor mapping, confirm/cancel |
| `EDITOR-SESSION-001` | Source-image load, rectangle annotation, selection, undo/redo, export, document round-trip |

## ImageEditor expansion (approved)

The pinned `ShareX.ImageEditor` submodule now has a dedicated static census of
its tools, concrete annotations, effect types and parameters, operations,
commands, settings, assets, and UI surfaces. The following approved packages
turn that discovery evidence into normative cross-platform behavior. Activation
still requires conformance and specialist evidence.

| ID | Intent |
|---|---|
| `EDITOR-ANNOTATIONS-001` | Complete annotation-tool families, styling, selection, ordering, clipboard, and history |
| `EDITOR-EFFECTS-001` | Effect discovery, parameters, preview/apply/cancel, favorites, determinism, and failures |
| `EDITOR-CANVAS-001` | New/open/insert, zoom/pan, crop/resize/rotate/flip, backgrounds, comparison, and flatten |
| `EDITOR-OUTPUT-ACTIONS-001` | Copy, save, upload, print, pin, wallpaper, task continuation, and cancellation |
| `EDITOR-SETTINGS-001` | Persisted editor, tool, toolbar, appearance, recent-item, and effect preferences |
| `EDITOR-UTILITIES-001` | Color picker, QR, hashing, icon conversion, comparison, background removal, and video conversion |

## Format

See BXIP001 [product contract design](../docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/03-product-contract.md) and [D-CON-001](../docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/08-decisions.md#d-con-001-contract-schema-versioning-and-tooling).

Machine-readable schemas are published in `schemas/`. The manifest schema
requires every listed capability to declare its version, lifecycle status, and
stable requirement IDs.
