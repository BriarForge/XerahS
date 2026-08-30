# BXIP001 Contract-First Agent-Native Platform Architecture

**Status**: Proposed - Full Functional Parity Program
**Created**: 2026-08-30
**Updated**: 2026-08-30
**Area**: Architecture | Agentic Development | Windows | macOS | Linux
**ShareX-side ID**: [XIP0086](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0086-contract-first-agent-native-platform-architecture.md)
**Implementation repository**: [BriarForge/XerahS](https://github.com/BriarForge/XerahS)
**Reference baseline**: [KovaForge/XerahS](https://github.com/KovaForge/XerahS) commit `5c7e36dea77ab131fe0f5e2101e5d578ccde0306` (`v0.29.0`)
**Decision requested**: Approve a greenfield, contract-first implementation in this repository that reaches full desktop functional parity with the pinned KovaForge XerahS baseline on Windows, macOS, and Linux. The [architecture decisions](08-decisions.md), [reference-baseline protocol](11-reference-baseline.md), and [full-parity delivery program](12-full-parity-delivery.md) bind implementation unless a later approved proposal supersedes them.

This directory is the canonical proposal. It was originally a single markdown file (XIP0086) and is split here so each concern can be read, reviewed, and revised on its own.

## Documents

| File | Contents |
|---|---|
| [01-overview.md](01-overview.md) | Executive summary and motivation |
| [02-proposal.md](02-proposal.md) | Three sources of truth, native apps, ImageEditor ownership |
| [03-product-contract.md](03-product-contract.md) | Contract layout, required contents, examples, evidence |
| [04-governance.md](04-governance.md) | Hierarchical `AGENTS.md` and the agentic development protocol |
| [05-conformance.md](05-conformance.md) | Traceability, CI parity, capability matrix, compatibility |
| [06-architecture-boundaries.md](06-architecture-boundaries.md) | Product vs platform, ImageEditor host boundary, coexistence |
| [07-pilot.md](07-pilot.md) | Full-parity implementation strategy, success criteria, non-goals, definition of done |
| [08-decisions.md](08-decisions.md) | Implementation-binding architecture decisions and required follow-ups |
| [09-alternatives-and-risks.md](09-alternatives-and-risks.md) | Alternatives considered, risks, and mitigations |
| [10-history.md](10-history.md) | Related proposals, review record, evolution history |
| [11-reference-baseline.md](11-reference-baseline.md) | Pinned KovaForge source baseline, census rules, and migration ledger |
| [12-full-parity-delivery.md](12-full-parity-delivery.md) | Capability domains, implementation waves, and completion gates |

## Architecture decisions

These IDs bind the greenfield implementation. Full text is in [08-decisions.md](08-decisions.md).

| ID | Topic |
|---|---|
| [D-BASE-001](08-decisions.md#d-base-001-reference-baseline-and-full-parity) | KovaForge baseline and full-parity mandate |
| [D-OWN-001](08-decisions.md#d-own-001-product-contract-ownership-and-approval-authority) | Ownership and approval |
| [D-CON-001](08-decisions.md#d-con-001-contract-schema-versioning-and-tooling) | Contract schema, versioning, tooling |
| [D-WIN-001](08-decisions.md#d-win-001-windows-framework-and-os-baseline) | Windows framework and OS baseline |
| [D-MAC-001](08-decisions.md#d-mac-001-macos-swiftuiappkit-boundary-and-os-baseline) | macOS SwiftUI/AppKit boundary |
| [D-LIN-001](08-decisions.md#d-lin-001-linux-toolkit-desktop-display-server-portal-distro-and-packaging) | Linux toolkit and packaging |
| [D-REPO-001](08-decisions.md#d-repo-001-monorepo) | Monorepo |
| [D-SHARE-001](08-decisions.md#d-share-001-shared-binaries) | Shared binaries |
| [D-PLUG-001](08-decisions.md#d-plug-001-cross-language-plugin-automation-and-configuration) | Plugins, automation, configuration |
| [D-REL-001](08-decisions.md#d-rel-001-release-policy-when-a-platform-cannot-implement-a-capability) | Release when a platform cannot implement |
| [D-REV-001](08-decisions.md#d-rev-001-human-review-boundaries) | Human review boundaries |
| [D-AGT-001](08-decisions.md#d-agt-001-agentsmd-hierarchy) | `AGENTS.md` hierarchy |
| [D-REF-001](08-decisions.md#d-ref-001-agent-directed-restructuring-authority) | Agent-directed restructuring authority |
| [D-GIT-001](08-decisions.md#d-git-001-progressive-commit-and-final-push-policy) | Progressive commit and final push policy |
| [D-GOLD-001](08-decisions.md#d-gold-001-golden-image-tolerances) | Golden-image tolerances |
| [D-KERN-001](08-decisions.md#d-kern-001-shared-headless-rendering-kernel-threshold) | Shared rendering-kernel threshold |
| [D-VID-001](08-decisions.md#d-vid-001-native-videoeditor-and-media-tools) | Native VideoEditor and media tools |
| [D-ID-001](08-decisions.md#d-id-001-development-application-identity) | Development application identity |

Section numbers in the split files match the original XIP0086 numbering so existing references remain stable.
