# 5. Hierarchical AGENTS.md Governance

## 5.1 Principle

The greenfield repository SHALL use multiple `AGENTS.md` files so that an agent receives both repository-wide law and precise instructions for the directory it is changing.

The strength of this model comes from scope and enforceability, not from file count alone. Adding instructions everywhere without a hierarchy would increase context consumption, duplication, contradiction, and stale rules. A child file therefore adds local constraints; it does not restate the parent.

The intended instruction chain is:

```text
Root constitution
      |
      +-- Product Contract governance
      |
      +-- Shared platform governance
      |       +-- Windows native rules
      |       +-- macOS native rules
      |       +-- Linux native rules
      |
      +-- Independent conformance governance
      |
      +-- Governance-tooling rules
```

## 5.2 Root constitution

The root `AGENTS.md` SHALL be short, stable, and non-overridable. It defines rules that apply to every agent and every directory, including:

- The Product Contract is the source of product truth.
- The pinned KovaForge snapshot is the discovery baseline for full desktop functional parity.
- Every baseline capability must remain traceable through census, contract, implementation, conformance, and release evidence.
- Supported platforms and release parity requirements.
- Security, privacy, accessibility, licensing, and data-compatibility invariants.
- The authority and precedence model for instructions.
- Required planning, review, verification, and evidence.
- Standing authority for bounded, behavior-preserving restructuring that removes material agentic-development friction.
- Prohibited actions, including silently weakening contracts or waiving a platform.
- The process for escalating contradictory or infeasible requirements.
- Links to each first-level child instruction scope.

Root rules SHOULD use stable identifiers, for example `ROOT-CONTRACT-001`, so CI reports and child files can refer to rules without copying their text.

## 5.3 Parent-child authority

The hierarchy SHALL follow these rules:

1. Root constitutional rules apply everywhere and cannot be weakened by a child.
2. A child `AGENTS.md` inherits all applicable ancestors automatically.
3. A child MAY add stricter or more specific rules for its directory subtree.
4. A child MUST NOT duplicate an ancestor rule; it references the ancestor's rule identifier instead.
5. A child MUST NOT contradict an ancestor. If conflict is unavoidable, work stops and the governance owner resolves it explicitly.
6. A rule that applies to sibling trees belongs in their nearest common ancestor.
7. A deeper `AGENTS.md` is created only when that subtree has a real architectural, security, tooling, or verification boundary.

Each child SHALL link to its immediate parent. Each parent SHALL maintain a child-scope index, but agents are not required to load unrelated sibling instructions. This provides navigable parent-to-child and child-to-parent relationships without recursive instruction loading.

## 5.4 Standard child-file schema

Every scoped `AGENTS.md` SHOULD use the same concise structure:

```md
# Scope
Applies to: platforms/macos/**
Parent: ../AGENTS.md

# Purpose
What this subtree owns and does not own.

# Local Rules
Stable rule IDs containing only additions to inherited rules.

# Required Workflow
Planning, implementation, and review steps for this subtree.

# Verification
Commands, test environments, and evidence required before completion.

# Improvement Authority
Locally owned structures agents may change without separate permission, protected boundaries, and required validation.

# Prohibited Changes
Actions that are unsafe or architecturally invalid in this subtree.

# Escalation
Conditions that require a contract change, waiver, security review, or human decision.

# Child Scopes
Links to immediate child AGENTS.md files.
```

Platform-specific files would then carry only genuinely local rules. Examples include Windows packaging and API constraints, macOS entitlements and AppKit/SwiftUI boundaries, and Linux portal, Wayland, X11, toolkit, and packaging requirements.

## 5.5 Role isolation through instruction scopes

The hierarchy SHALL preserve separation of duties:

- `product-contract/AGENTS.md` governs normative language, requirement identifiers, schemas, compatibility, and product approval. It MUST prohibit changing a contract solely to satisfy an implementation.
- `platforms/AGENTS.md` governs requirements shared by all native implementations, including traceability manifests and platform parity.
- Each platform child governs native framework use, OS baselines, packaging, signing, permissions, accessibility, and platform tests.
- Each platform's `image-editor/AGENTS.md` governs native editor UI, input, rendering, persistence adapters, and verification without granting authority to redefine the editor contract.
- `conformance/AGENTS.md` governs independent verification. It MUST prohibit deriving expected results solely from one platform implementation.
- `conformance/image-editor/AGENTS.md` governs editor fixtures, tolerance policies, `.xann` compatibility, and golden-image comparison independently of all three renderers.
- `tools/contract-linter/AGENTS.md` governs tooling that validates the governance system itself.

This separation allows an agent to be highly constrained in its own scope without granting it authority over the specification or another platform.

## 5.6 Governance linting

CI SHALL validate the instruction hierarchy. At minimum, the governance linter SHALL detect:

- Broken parent or child links.
- Missing or duplicate rule identifiers.
- Child rules that attempt to override protected root rules.
- Duplicate normative text that is likely to drift.
- Directories that declare an instruction scope but are absent from the root scope index.
- Invalid verification commands or references where they can be checked statically.
- Contract or platform changes that lack the required traceability updates.
- Baseline ledger rows with missing source evidence, contracts, platform dispositions, owners, or evidence.
- User-reachable surfaces discovered by census tooling but absent from every ledger.

The linter SHOULD generate an effective-instructions report for any repository path. An agent and reviewer can then see exactly which root-to-leaf rules governed a change.

## 5.7 Instruction quality controls

More instructions are not automatically stronger governance. Each `AGENTS.md` SHALL be reviewed for:

- **Necessity**: the rule belongs at this scope and prevents a concrete failure.
- **Uniqueness**: the rule is defined in one authoritative location.
- **Testability**: compliance can be demonstrated where practical.
- **Currency**: commands, paths, SDK versions, and links remain valid.
- **Context cost**: the file is concise enough for an agent to apply reliably.
- **Ownership**: a named role is responsible for resolving ambiguity and maintaining the scope.

Executable architecture tests, CI gates, schemas, and conformance tests remain stronger than prose. `AGENTS.md` tells agents what must happen; repository automation proves that it happened.

# 6. Agentic Development Protocol

## 6.1 Feature workflow

Every product behavior change and baseline-parity implementation SHOULD follow this sequence:

1. **Baseline evidence**: Link the capability, setting, workflow, command, integration, or format to the pinned source snapshot and ledger.
2. **Contract change**: Create or update the relevant capability contract and acceptance evidence.
3. **Impact analysis**: Identify affected requirements, data formats, integrations, migrations, and platforms.
4. **Platform planning**: Produce a platform-specific implementation plan for Windows, macOS, and Linux.
5. **Native implementation**: Implement the feature independently using the platform's native framework and conventions.
6. **Conformance**: Run shared scenarios, deterministic vectors, migration fixtures, and platform-specific integration tests.
7. **Parity review**: Compare outcomes, update ledger lifecycle state, and document any deviation.
8. **Release decision**: Release only when the parity gate passes or an authorized waiver exists.

A feature is not complete because one implementation has landed. It is complete when the contracted product behavior has an accepted disposition on every supported platform.

During the full-parity program, agents SHALL draw implementation work from unresolved ledger IDs. They MUST NOT substitute a self-selected feature list, declare a domain complete from source inspection alone, or stop after the qualification tranche.

## 6.2 Separation of agent responsibilities

To reduce correlated mistakes, the same agent SHOULD NOT be the sole author, implementer, verifier, and approver of a material contract change.

Recommended roles are:

- **Contract agent**: turns product intent into normative requirements and examples.
- **Windows agent**: implements and tests the Windows realization.
- **macOS agent**: implements and tests the macOS realization.
- **Linux agent**: implements and tests the Linux realization.
- **Conformance agent**: reviews from the contract rather than from another platform's code.
- **Human product owner**: resolves ambiguous intent and approves durable platform differences.

Platform agents MAY inspect another implementation for interoperability or defect context, but SHOULD implement from the Product Contract. This reduces accidental copying of platform assumptions and bugs.

## 6.3 Change control

Native implementation agents MUST NOT weaken the contract merely to make an implementation pass. If a requirement is infeasible or inappropriate on a platform, the agent SHALL submit one of:

- A contract clarification that preserves the original user intent.
- A platform deviation with rationale and user impact.
- A time-bound waiver with an owner, expiry condition, and remediation plan.
- A proposal to change supported platform scope.

Contract changes that alter user-visible behavior require product review. Mechanical clarifications that do not alter behavior may use the normal documentation review path.

## 6.4 Agent-directed restructuring and pain-point removal

Agents are explicitly authorized and expected to improve the repository while delivering contracted work. They do not need a separate issue, proposal, or human permission for a bounded restructuring that preserves governed behavior and removes a material development pain point.

Examples of material pain points include:

- oversized or mixed-responsibility files that make safe changes difficult
- unclear ownership, misleading names, hidden coupling, circular dependencies, or fragile initialization order
- duplicated logic or platform boundaries that repeatedly drift
- slow, flaky, opaque, or unnecessarily broad build and test feedback loops
- brittle scripts, manual synchronization, hard-coded environment assumptions, or poor diagnostics
- repository layouts, interfaces, or generated artifacts that force agents to load irrelevant context
- stale, ambiguous, missing, or overly duplicated scoped instructions
- obsolete compatibility scaffolding or dead code whose safe removal is demonstrated

The standing authority covers internal module extraction or consolidation, directory and package organization, dependency direction, adapter boundaries, types and interfaces, test architecture, build and developer tooling, diagnostic improvements, and scoped `AGENTS.md` refinements. It applies across adjacent repository scopes when a coherent fix requires them; directory boundaries are not a reason to preserve a known structural defect.

An agent exercising this authority SHALL:

1. State the pain point and the intended structural outcome in its plan or change record.
2. Prefer the smallest coherent change that removes the cause rather than masking a symptom.
3. Preserve Product Contract behavior, accepted compatibility, and requirement traceability.
4. Keep the change incremental and reviewable, with migration or compatibility adapters where an atomic transition is unsafe.
5. Update affected tests, documentation, architecture maps, effective instructions, build tooling, and traceability manifests in the same change.
6. Run verification proportional to the complete changed surface, not only the originally requested file.
7. Return to and complete the original requested outcome unless the restructuring exposes a protected decision that requires escalation.
8. Record a repeated or cross-cutting pain point and its durable remedy in the nearest governed lesson, decision, instruction, or tooling check so later agents do not rediscover it.

Agents MUST NOT use this authority for speculative rewrites, stylistic churn, unbounded cleanup, or architecture astronautics. Refactoring is not successful merely because code moved; it must reduce a named cost, risk, ambiguity, or feedback delay.

Prior human approval remains required when the proposed restructuring changes or risks changing:

- user-visible Product Contract behavior or an approved platform disposition
- public protocols, persisted data, migrations, file formats, or compatibility guarantees
- security, privacy, permissions, credential handling, or trust boundaries
- licensing posture or a major third-party dependency commitment
- supported platforms, binding native framework decisions, shared-runtime policy, or repository boundaries
- release identity, signing, distribution, or another protected architecture decision
- the root `AGENTS.md` constitution or its non-overridable rule meanings

When the pain point originates in governance itself, an agent MAY improve a child `AGENTS.md`, instruction index, linter, or effective-instructions report without separate approval if the change clarifies or strengthens inherited policy. Weakening or changing a protected root rule follows [D-REV-001](08-decisions.md#d-rev-001-human-review-boundaries).
