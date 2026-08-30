# Product Contract

This directory is the source of product truth for native XerahS. It is not a runtime binary.

Humans and agents read it at development time. Native applications in `platforms/` implement it. `conformance/` proves they did.

## Pilot capabilities (Phase 1)

These packages are selected. Contracts are not written yet.

| ID | Intent |
|---|---|
| `FILENAME-GENERATION-001` | Token expansion, counter padding, date formatting, illegal-character handling |
| `POST-CAPTURE-ACTIONS-001` | Ordered post-capture actions, failure continuation, task-result recording |
| `REGION-CAPTURE-001` | Interactive region selection, permissions, DPI/monitor mapping, confirm/cancel |
| `EDITOR-SESSION-001` | Source-image load, rectangle annotation, selection, undo/redo, export, document round-trip |

## Format

See BXIP001 [product contract design](../docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/03-product-contract.md) and [D-CON-001](../docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/08-decisions.md#d-con-001-contract-schema-versioning-and-tooling).
