# 9. Greenfield Full-Parity Implementation Strategy

The original four-feature pilot is retained as a qualification tranche inside a larger committed delivery program. Approval of BXIP001 authorizes implementation through full desktop functional parity; it does not require a second go/no-go proposal after qualification.

The detailed reference protocol is [section 20](11-reference-baseline.md). The detailed capability domains, waves, packets, and release gates are [section 21](12-full-parity-delivery.md).

## Phase 0: Approve and pin

- Approve the Product Contract as product truth.
- Confirm Windows, macOS, and Linux desktop as the native targets.
- Adopt the [architecture decisions](08-decisions.md) as implementation-binding defaults.
- Approve [D-BASE-001](08-decisions.md#d-base-001-reference-baseline-and-full-parity) and pin KovaForge commit `5c7e36dea77ab131fe0f5e2101e5d578ccde0306`.
- Assign the named roles in [D-OWN-001](08-decisions.md#d-own-001-product-contract-ownership-and-approval-authority).
- Establish the scoped `AGENTS.md` hierarchy using [D-AGT-001](08-decisions.md#d-agt-001-agentsmd-hierarchy).
- Encode the standing restructuring authority and protected boundaries from [D-REF-001](08-decisions.md#d-ref-001-agent-directed-restructuring-authority) as root and scoped agent rules.
- Encode the progressive checkpoint and final-push workflow from [D-GIT-001](08-decisions.md#d-git-001-progressive-commit-and-final-push-policy) as protected root Git rules.
- Encode primary-branch development from [D-BRANCH-001](08-decisions.md#d-branch-001-primary-branch-development) and prohibit automatic agent, feature, fix, temporary, and worktree branches.
- Preserve the existing Avalonia installation and live settings until an explicit replacement test or release migration.

## Phase 1: Census and contract foundation

- Create the baseline and parity artifacts required by [section 20.4](11-reference-baseline.md#204-required-baseline-artifacts).
- Run the independent source-structure and user-journey censuses.
- Establish contract schemas, conformance adapters, provenance, ledger validation, and the parity dashboard.
- Begin contracting high-dependency formats and interfaces while the census continues.

No domain may be declared complete before the baseline census closes, but implementation need not wait for the entire census when its ledger rows and dependencies are stable.

## Phase 2: Qualification tranche on all platforms

Implement these capability packages on Windows, macOS, and Linux:

1. `FILENAME-GENERATION-001`: token expansion, counter padding, date formatting, extension handling, and illegal-character rules.
2. `POST-CAPTURE-ACTIONS-001`: ordered actions, independent failure continuation, cancellation, and task-result recording.
3. `REGION-CAPTURE-001`: native selection, permissions, monitor/DPI mapping, accessibility, confirm, and cancel.
4. `EDITOR-SESSION-001`: source load, rectangle annotation, selection, undo/redo, deterministic export, and annotation-document round trip.

The tranche validates architecture, tooling, agent coordination, and measurement. Passing it starts full delivery automatically. Failing it triggers remediation or an explicit architecture amendment; it does not silently reduce the parity target.

## Phase 3: Full implementation waves

Execute Waves 2 through 7 from [section 21.3](12-full-parity-delivery.md#213-delivery-waves):

- product foundation and compatibility
- capture, workflows, and recording
- ImageEditor, VideoEditor, and media
- destinations and extensibility
- history, automation, and power-user integration
- UX completeness and native distribution

Within a wave, bounded platform and domain work SHOULD be delegated to independent agents with non-overlapping ownership. The coordinating agent owns contract consistency, integration, and parity state.

Agents MAY restructure internal code, tooling, tests, directories, and scoped instructions as pain points surface under D-REF-001. These improvements are part of delivery capacity, not deferred cleanup, provided they preserve contracted outcomes, remain reviewable, and complete the wave objective that exposed the pain point.

Each implementation wave SHALL be preserved through coherent local checkpoint commits under D-GIT-001. The coordinating agent performs the final cross-scope verification and pushes the complete checkpoint sequence; it does not collapse delegated or independently meaningful outcomes into one opaque commit.

All coordinating and delegated agents remain on `main` under D-BRANCH-001. Non-overlapping path ownership and progressive handoffs provide isolation; agents do not create branches per platform, feature, agent, or packet unless the human explicitly requests that model.

## Phase 4: Full-parity release candidate

Execute Wave 8 and the gate in [section 21.5](12-full-parity-delivery.md#215-full-parity-release-gate). Publish the parity attestation and unresolved-ID count.

The greenfield applications remain non-production replacements until this phase passes. Production replacement of Avalonia requires the full-parity attestation and release acceptance.

# 12. Success Criteria

BXIP001 implementation succeeds when:

1. The pinned KovaForge baseline and submodule references are reproducible.
2. The independent baseline census is closed with no unresolved discovery gaps.
3. Every in-scope ledger row has a Product Contract and all-platform disposition.
4. Every Required or Equivalent capability is release-verified on every applicable platform.
5. Configuration, workflow, uploader, history, annotation, and media compatibility migrations pass.
6. Native GUI, capture, recording, editors, destinations, history, CLI, MCP, assistant, daemon, and integration surfaces satisfy the same compatible contract.
7. The four-feature qualification tranche and all full-delivery waves meet their conformance, native integration, accessibility, security, and performance gates.
8. Installable native packages pass clean install, Avalonia replacement migration, update, rollback, recovery, and uninstall journeys.
9. The parity attestation identifies the exact baseline, contract, builds, ledger hashes, approved corrections, and intrinsic platform limitations.
10. BriarForge builds and releases without the KovaForge checkout or legacy editor submodules.
11. Repeated agentic-development pain points have durable structural, tooling, test, diagnostic, or instruction remedies rather than accumulating as accepted friction.
12. Material implementation history is recoverable and reviewable through verified progressive commits, and every completed task's final local revision is present on its intended remote branch.
13. Agentic development stays on `main` unless a human explicitly requests another branch, with no unrequested branch or worktree proliferation.

Compiling three applications, completing only the qualification tranche, or reporting a high percentage while unresolved IDs remain does not satisfy this proposal.

# 13. Non-Goals

This proposal does not:

- Copy or preserve KovaForge's internal project structure, Avalonia UI, service locator, namespaces, or class boundaries.
- Reproduce verified defects when a human-approved corrected behavior and regression test exist.
- Retire or modify KovaForge XerahS, ShareX Team XerahS, ShareX.ImageEditor, or ShareX.VideoEditor.
- Collapse the native and Avalonia application identities before a later product decision.
- Require pixel-identical native UI; exported media and deterministic behavior follow contract tolerances.
- Make AI-generated changes exempt from product, security, accessibility, conformance, or release review.
- Include experimental Android or iOS implementation; those rows are explicitly classified `OutOfScope-BXIP001` pending a mobile proposal.
- Require store publication as proof of parity. Signed/notarized installable desktop artifacts are required; store submission may follow separately.

The out-of-process plugin protocol and platform packaging details MAY be specified in child proposals, but those deliverables cannot remain incomplete at the full-parity release gate.

# 16. Definition of Done for This Proposal

- The full-parity mandate and pinned reference baseline are approved.
- The architecture decisions are accepted or individually superseded.
- The baseline census, parity ledger, and moving-baseline rules are accepted.
- The Product Contract layout and governance hierarchy are active.
- The four qualification capabilities are accepted as Wave 1 rather than final scope.
- ImageEditor and VideoEditor are accepted as native internal features, not production submodules.
- Full delivery Waves 2 through 8 and the release gate are authorized.
- Contract, product, platform, conformance, security, governance, and release roles are assigned.
- The root and scoped `AGENTS.md` hierarchy grants and bounds self-directed restructuring under D-REF-001.
- The root `AGENTS.md` requires progressive verified commits and a final push under D-GIT-001.
- The root `AGENTS.md` prohibits creating or switching branches without explicit human instruction under D-BRANCH-001.
- No production implementation step depends on modifying the KovaForge reference checkout.
- A later proposal is required to retire Avalonia or collapse application identities, not to continue from qualification into full implementation.
