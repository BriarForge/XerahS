# 17. Related Proposals

These documents live in the ShareX Team XerahS tree and remain behavioral reference material.

- [XIP0013 macOS Implementation](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0013-macos-implementation.md)
- [XIP0014 Linux Support Implementation Plan](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0014-linux-support-implementation-plan.md)
- [XIP0019 Platform Abstraction and Architecture Audit](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0019-platform-abstraction-architecture-audit.md)
- [XIP0052 Agentic Refactoring and Architectural Modernization](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0052-agentic-refactoring-architectural-modernization.md)
- [XIP0063 XerahS CLI OpenClaw Compatibility](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0063-xerahs-cli-openclaw-compatibility.md)
- [XIP0064 XerahS MCP Server](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0064-xerahs-mcp-server.md)
- [XIP0068 Re-editing Saved Annotations](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0068-re-editing-saved-annotations.md)
- [XIP0075 Linux XDG + Flathub Readiness](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0075-linux-xdg-flathub-readiness.md)
- [XIP0078 macOS Improvement Plan](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0078-macos-improvement-plan.md)
- [XIP0079 Linux Improvement Plan](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0079-linux-improvement-plan.md)
- [XIP0082 Fedora Linux Validation and Flathub Submission Gate](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0082-fedora-linux-validation-and-flathub-submission-gate.md)
- [XIP0084 Windows Region Capture Algorithm Parity](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0084-windows-region-capture-algorithm-parity.md)
- [XIP0086 pointer in ShareX Team XerahS](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0086-contract-first-agent-native-platform-architecture.md)

# 18. Review Record

Review of the proposal before closing the architecture decisions found the following issues. Each is resolved in this revision unless marked residual.

1. **Framework tables deferred while the proposal asked to be approved.** [§3.3](02-proposal.md#33-implement-the-contract-through-native-platform-applications) now records the [architecture-decision](08-decisions.md) defaults instead of leaving Windows and Linux unspecified.
2. **Non-goals contradicted the request to close framework and plugin decisions.** [Non-goals](07-pilot.md#13-non-goals) now distinguish implementation-binding defaults from frozen forever choices.
3. **Qualification asked for one platform and all platforms.** [Phase 2](07-pilot.md#phase-2-qualification-tranche-on-all-platforms) requires the same tranche on Windows, macOS, and Linux.
4. **Qualification slices were examples, not identifiers.** [Phase 2](07-pilot.md#phase-2-qualification-tranche-on-all-platforms) names four capability IDs.
5. **Filename vector omitted padding and extension** that the expected output assumed. The example now includes `counter_padding` and `extension`.
6. **CI parity report was SHOULD while the release gate was SHALL.** The report is now SHALL.
7. **No coexistence rule** for Avalonia and greenfield installs. [§8.7](06-architecture-boundaries.md#87-coexistence-with-the-avalonia-application) and [D-ID-001](08-decisions.md#d-id-001-development-application-identity) require a distinct application identity and side-by-side install.
8. **Ownership was a role soup with no human approver.** [D-OWN-001](08-decisions.md#d-own-001-product-contract-ownership-and-approval-authority) names the product owner as the human authority for behavior, waivers, and releases.
9. **In-process .NET plugins cannot survive a non-.NET Linux or macOS app.** [D-PLUG-001](08-decisions.md#d-plug-001-cross-language-plugin-automation-and-configuration) sets out-of-process JSON-RPC as the direction and keeps custom HTTP uploaders as the native-app destination floor.
10. **Windows 10 as an implicit baseline is stale in August 2026.** [D-WIN-001](08-decisions.md#d-win-001-windows-framework-and-os-baseline) sets Windows 11 23H2+ for greenfield; Avalonia continues to serve Windows 10.
11. **Golden-image policy was an open question in an exactness-sensitive editor.** [D-GOLD-001](08-decisions.md#d-gold-001-golden-image-tolerances) sets fail-closed exact vs perceptual classes and bundled fonts.
12. **Shared-kernel exception had no evidence bar**, which would let convenience restore a common DLL. [D-KERN-001](08-decisions.md#d-kern-001-shared-headless-rendering-kernel-threshold) sets a measurable threshold.
13. **The proposal ended after four pilot features**, while the intended outcome is a fully functional native XerahS. [Section 20](11-reference-baseline.md) now pins KovaForge `v0.29.0`; [section 21](12-full-parity-delivery.md) requires exhaustive census, full delivery waves, and a zero-unresolved-ID release gate.
14. **The reference tree was ambiguous.** The primary discovery baseline is now explicitly KovaForge/XerahS commit `5c7e36de`; ShareX Team XerahS remains historical proposal context.
15. **VideoEditor had no native ownership decision.** [D-VID-001](08-decisions.md#d-vid-001-native-videoeditor-and-media-tools) now makes it a native platform feature and prohibits the legacy submodule as a production dependency.
16. **Agents could preserve known structural pain because refactoring authority was implicit.** [D-REF-001](08-decisions.md#d-ref-001-agent-directed-restructuring-authority) and [section 6.4](04-governance.md#64-agent-directed-restructuring-and-pain-point-removal) now grant standing authority for bounded behavior-preserving restructuring, encode it in `ROOT-IMPROVE-*`, and retain human review for protected boundaries.
16. **Plugins and packaging were residual follow-ups**, which allowed an incomplete product to claim parity. They may use child specifications but are now release-required deliverables.

Residual risks that remain acceptable for a proposed architecture:

- Qt on GNOME will look less native than GTK. That is a conscious product trade for overlay and power-user density, to be measured in Phase 3.
- Unpackaged WinUI plus Win32 overlay is a real integration tax. Phase 3 must count it.
- Python as the initial conformance runner is a convenience choice; pinning a binary later is allowed without a contract change.
- Named platform owners are roles. Filling them with people is an operational step, not an architecture gap.
- Experimental mobile remains visible in the baseline census but is intentionally outside this Windows/macOS/Linux desktop program.

# 19. Evolution History

| Date | Change | Rationale |
|---|---|---|
| 2026-08-30 | Initial proposal | Capture the contract-first, agent-native architecture and define a governed pilot before any existing framework retirement |
| 2026-08-30 | Defined greenfield repository and hierarchical agent governance | Establish BriarForge/XerahS as the implementation target and make root-to-leaf scoped instructions part of the architecture |
| 2026-08-30 | Defined native ImageEditor ownership | Integrate ImageEditor into each native solution, retain the existing repository as a legacy reference, and prohibit it as a production submodule |
| 2026-08-30 | Closed open decisions as pilot-binding architecture choices | Record ownership, contract format, platform baselines, monorepo, sharing, plugins, release, review, AGENTS.md, goldens, and kernel threshold so the pilot can start without a second architecture proposal |
| 2026-08-30 | Split the proposal into this directory | The single-file XIP exceeded a useful review size; BXIP001 is now the canonical split document set |
| 2026-08-30 | Converted pilot into full-parity implementation program | Pin KovaForge v0.29.0, require exhaustive capability ledgers, add native VideoEditor ownership, and authorize delivery through a zero-unresolved-ID release gate |
| 2026-08-30 | Authorized agent-directed restructuring | Let agents remove material development pain points without case-by-case permission while protecting product behavior, compatibility, security, licensing, platform/framework decisions, repository boundaries, and the root constitution |
