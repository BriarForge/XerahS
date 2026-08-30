# AGENTS.md

Greenfield native XerahS. This file is the root constitution. Child `AGENTS.md` files add constraints; they do not restate or weaken these rules.

## Git identity and wrappers (mandatory)

All git activity in this repo MUST go through a per-person wrapper. No bare `git push`.

| Agent | Wrapper |
|---|---|
| Aoife | `git-aoife` |
| Declan | `git-declan` |
| Milena | `git-milena` |
| Sofia | `git-sofia` |

Whoever pushes uses their own wrapper. Example: Declan pushes with `git-declan push`, Aoife with `git-aoife push`. Wrappers set committer identity and route the push to the correct per-person remote on the matching `github-<person>` SSH host.

Run `git-<person> whoami` to confirm before pushing.

If the matching wrapper is not on `PATH`, fall back to the configured git identity and authenticated `gh` account. Confirm `git config user.name`, `git config user.email`, and `git remote -v` before pushing. Do not invent a wrapper identity.

## Source of truth

Inherited from `/Users/mike/Projects/BriarForge/AGENTS.md`. When this file and the parent conflict, the parent wins until this file is updated to match. Product behavior is defined by `product-contract/`, not by parent workspace rules.

## Constitution

These rules apply everywhere and cannot be weakened by a child file.

- **ROOT-CONTRACT-001** The Product Contract is the source of product truth. No platform implementation becomes the specification by being first or most complete.
- **ROOT-NOT-FORK-001** This repository is not a fork of ShareX/XerahS. Do not copy that tree's structure as product definition. Treat it as a behavioral reference and fixture source with recorded provenance.
- **ROOT-PLATFORMS-001** Supported native targets are Windows, macOS, and Linux. A feature is complete when every supported platform has an accepted disposition.
- **ROOT-IDENTITY-001** Pilot apps use `com.xerahs.native` and the display name "XerahS Native". Do not reuse the Avalonia product identity or overwrite its live settings without an explicit user action.
- **ROOT-LICENSE-001** This repository is GNU GPL v3. Dynamically link LGPL Qt. Do not add a dependency that conflicts with GPL v3.
- **ROOT-SECURITY-001** Security, privacy, accessibility, and data-compatibility invariants are not optional. Capture permission flows, credential storage, uploader secrets, and plugin hosts require human review.
- **ROOT-AUTHORITY-001** Child `AGENTS.md` files inherit ancestors, may add stricter local rules, and MUST NOT duplicate or contradict parent rule IDs. Conflict stops work for the governance owner.
- **ROOT-EVIDENCE-001** Product behavior changes follow contract, impact analysis, native implementation, conformance, parity review, then release. Landing code on one platform is not completion.
- **ROOT-IMPROVE-001** Agents are authorized and expected to perform bounded, behavior-preserving restructuring when encountered friction makes implementation, verification, navigation, or maintenance needlessly slow, fragile, ambiguous, or repetitive. No separate ticket or permission is required. Keep the requested outcome in scope, update affected tests, documentation, instructions, architecture maps, and traceability, and verify the changed surface.
- **ROOT-IMPROVE-002** Self-directed restructuring MUST NOT silently change the Product Contract, public or persisted compatibility, security or privacy posture, licensing, supported platforms or framework decisions, repository boundaries, or this root constitution. Those changes follow their applicable approval path. Avoid speculative rewrites, unrelated cosmetic churn, and refactoring that leaves the original task unfinished.
- **ROOT-PROHIBIT-001** Do not silently weaken a contract, waive a platform, or derive expected test results solely from one implementation.
- **ROOT-ESCALATE-001** If a requirement is infeasible, submit a clarification, a documented deviation, a time-bound waiver, or a scope change. Do not edit the contract only to make an implementation pass.
- **ROOT-REVIEW-001** User-visible contract changes, waivers, root constitution changes, security-sensitive code, and releases require a human product owner. Agents may author; they must not be the sole approver of those classes.

Architecture decisions: [BXIP001](docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/README.md).

## Child scopes

| Path | File |
|---|---|
| Product Contract | [product-contract/AGENTS.md](product-contract/AGENTS.md) |
| Native platforms | [platforms/AGENTS.md](platforms/AGENTS.md) |
| Conformance | [conformance/AGENTS.md](conformance/AGENTS.md) |
| Tooling | [tools/AGENTS.md](tools/AGENTS.md) |
