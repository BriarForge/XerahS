# 9. Greenfield Implementation Strategy

## Phase 0: Approve principles and boundaries

- Approve the Product Contract as the future source of product truth.
- Confirm Windows, macOS, and Linux as the initial supported native targets.
- Adopt the [architecture decisions](08-decisions.md) as pilot-binding defaults.
- Assign the named roles in [D-OWN-001](08-decisions.md#d-own-001-product-contract-ownership-and-approval-authority).
- Establish the root and first-level `AGENTS.md` hierarchy in [BriarForge/XerahS](https://github.com/BriarForge/XerahS) using [D-AGT-001](08-decisions.md#d-agt-001-agentsmd-hierarchy).
- Record that greenfield development does not deprecate the existing Avalonia application by itself.

## Phase 1: Build a contract pilot

The pilot SHALL specify these four capability packages:

1. `FILENAME-GENERATION-001`: deterministic shared behavior: token expansion, counter padding, date formatting, and illegal-character handling.
2. `POST-CAPTURE-ACTIONS-001`: workflow behavior: ordered post-capture actions, independent failure continuation, and task-result recording.
3. `REGION-CAPTURE-001`: deeply native behavior: interactive region selection, permission prompts, DPI/monitor mapping, and confirm/cancel.
4. `EDITOR-SESSION-001`: ImageEditor slice covering source-image load, rectangle annotation, selection, undo/redo, deterministic export, and annotation-document round-trip.

For each slice:

- Write the contract package.
- Map the current Avalonia/platform implementation to requirement IDs.
- Create shared conformance vectors and scenarios.
- Identify ambiguities and missing product decisions.

## Phase 2: Implement the pilot slices natively on all three platforms

Implement the Phase 1 workflow in each native platform application while retaining the current production application. Each implementation SHALL use the Product Contract rather than porting UI code screen by screen.

Sequencing MAY start on the platform with the highest learning value, but Phase 2 is not complete until Windows, macOS, and Linux each implement the same bounded workflow. The experiment SHALL exercise native UI, lifecycle, permissions, persistence, and at least one XerahS-specific integration on every supported platform.

## Phase 3: Evaluate the economics

Measure:

- Agent and human elapsed time per platform
- Contract-authoring and review effort
- Defects found by conformance versus platform testing
- Behavioral drift
- Native accessibility and UX quality
- Startup time, memory use, and package size
- OS integration quality
- Framework and dependency burden
- Release and signing complexity

Compare these results with equivalent behavior in the existing Avalonia architecture and include the cost of maintaining the `AGENTS.md` hierarchy and conformance system.

## Phase 4: Decide the long-term topology

After the pilot, choose among:

1. Continue the greenfield native applications governed by the Product Contract.
2. Use a shared non-UI engine within the greenfield repository while retaining native shells.
3. Limit the greenfield project to selected native experiences and retain Avalonia for the remainder.
4. Stop the native rollout but retain Product Contracts and hierarchical agent governance for future development.

Only a subsequent approved proposal may authorize a broad native rewrite, change or freeze framework choices beyond the [architecture decisions](08-decisions.md), or retire Avalonia components.

# 12. Success Criteria for the Pilot

The pilot succeeds when:

1. At least four representative capabilities, including the mandatory ImageEditor slice, have approved, versioned contract packages.
2. Every normative pilot requirement maps to evidence on Windows, macOS, and Linux, or to an approved platform disposition.
3. The conformance runner detects intentionally introduced behavioral differences.
4. Independent platform agents can implement a contract change without treating another platform's source code as the specification.
5. Contract and platform review effort is measured, including human decision time.
6. Native implementation produces a demonstrable improvement in at least one of accessibility, OS integration, reliability, performance, or user experience.
7. The total maintenance and release burden is documented well enough to support a go/no-go decision on a broader native rollout.
8. The instruction linter can resolve and report the effective root-to-leaf `AGENTS.md` rules for every pilot path without conflict.

The pilot does not succeed merely because AI generates three compiling implementations.

# 13. Non-Goals

This proposal does not:

- Authorize removal of Avalonia or the current .NET projects.
- Freeze platform frameworks beyond the pilot-binding defaults in [architecture decisions](08-decisions.md). A later proposal may change a framework after measured evidence.
- Require all existing XerahS behavior to be specified immediately.
- Require duplicated implementations where shared code is demonstrably safer.
- Define mobile, web, or server targets.
- Specify the plugin IPC schema or ship a plugin host. Decision [D-PLUG-001](08-decisions.md#d-plug-001-cross-language-plugin-automation-and-configuration) locks the direction; a follow-up proposal owns the wire protocol.
- Promise pixel-identical UI across platforms.
- Make AI-generated changes exempt from code review, security review, testing, signing, or release governance.
- Create `AGENTS.md` files in directories that have no distinct governance boundary.
- Modify or retire the existing `KovaForge/ShareX.ImageEditor` repository, which remains independently owned by its existing consumers.
- Authorize MSIX Store distribution, Apple App Store distribution, or Flathub publication of the greenfield applications. Those remain separate packaging proposals.
- Support Windows 10 or macOS 13 and earlier in the greenfield applications. The Avalonia application continues to serve those users until a later proposal says otherwise.

# 16. Definition of Done for This Proposal

- The architectural principles are accepted or rejected explicitly.
- The [architecture decisions](08-decisions.md) are accepted or individually superseded.
- A Product Contract pilot location and format are approved (`product-contract/` as in [§4.1](03-product-contract.md#41-proposed-repository-structure) and [D-CON-001](08-decisions.md#d-con-001-contract-schema-versioning-and-tooling)).
- The greenfield [BriarForge/XerahS](https://github.com/BriarForge/XerahS) repository has a root constitution and scoped first-level `AGENTS.md` files following [D-AGT-001](08-decisions.md#d-agt-001-agentsmd-hierarchy).
- The four named pilot capabilities in Phase 1 are selected: `FILENAME-GENERATION-001`, `POST-CAPTURE-ACTIONS-001`, `REGION-CAPTURE-001`, `EDITOR-SESSION-001`.
- ImageEditor is accepted as an internal feature of each native solution rather than a production submodule.
- The ImageEditor host boundary, annotation-document compatibility policy, and conformance ownership are approved.
- Roles in [D-OWN-001](08-decisions.md#d-own-001-product-contract-ownership-and-approval-authority) are assigned.
- CI requirements for traceability and parity are agreed, including [D-REL-001](08-decisions.md#d-rel-001-release-policy-when-a-platform-cannot-implement-a-capability) and [D-GOLD-001](08-decisions.md#d-gold-001-golden-image-tolerances).
- No Avalonia deprecation or native rewrite begins without the pilot evidence and a follow-up proposal.
