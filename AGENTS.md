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

## Progressive commit and final push workflow (mandatory)

- **ROOT-GIT-001** Commit progressively at coherent, reviewable checkpoints as work advances. Each commit SHALL represent one understandable outcome and SHOULD pass the verification relevant to its changed surface. Do not hold a large completed body of work for one final catch-all commit, and do not manufacture noisy micro-commits with no independently useful meaning.
- **ROOT-GIT-002** Before an agent handoff, likely context loss, or interruption, commit every completed coherent checkpoint. Never include secrets, ignored build output, unrelated pre-existing changes, or another agent's unfinished work merely to obtain a clean tree.
- **ROOT-GIT-003** When the requested work is complete and final verification passes, synchronize safely and push all task commits to the current tracked branch using the required identity wrapper. This standing instruction is authorization to push; do not ask for an additional confirmation unless the human requested a pause, review-before-push, local-only work, or no push.
- **ROOT-GIT-004** Inspect status and diffs before every commit and before the final push. Do not bypass hooks, force-push, rewrite published history, discard work, or resolve another contributor's conflicting change without explicit authority. If the remote advanced, integrate it safely, rerun affected verification, then push normally.
- **ROOT-GIT-005** For submodule work, commit and push the submodule repository first, then commit the verified parent pointer update. The final parent push MUST NOT reference an unavailable submodule commit.
- **ROOT-BRANCH-001** All development, progressive commits, and the final push happen directly on the primary branch `main`. If a task or session starts on any other branch, including one assigned by agent tooling or a hosted session (for example `claude/*`), switch to `main` before editing and bring any completed commits from that branch into `main` by fast-forward or merge without rewriting published history. Agents MUST NOT create a branch, switch to any branch other than `main`, create a branch-backed worktree, or move into a detached-HEAD workflow unless the human explicitly requests that branch action.
- **ROOT-BRANCH-002** Do not invent `feature/*`, `fix/*`, `chore/*`, `codex/*`, `cursor/*`, `claude/*`, personal, temporary, backup, or recovery branches. A branch name supplied by tooling, a session template, or an automated prompt is not an explicit human request. If `main` is protected, lacks an upstream, or cannot be synchronized safely, stop and report the exact condition rather than creating or switching to another branch.
- **ROOT-BRANCH-003** When a human explicitly requests a new or different branch in their own words, use the exact requested branch or ask for its name if none was supplied. After switching, that branch becomes the current branch for progressive commits and final push; do not branch again without another explicit request.

Required sequence:

1. Confirm identity, remote, current branch, status, and applicable instructions; if the current branch is not `main`, switch to `main` per ROOT-BRANCH-001 unless the human explicitly directs otherwise.
2. Synchronize before editing when the tree is clean; never use a destructive pull to erase local work.
3. Implement one coherent slice, run proportionate verification, inspect its diff, and commit it with the repository's commit-message format.
4. Repeat step 3 as additional independently meaningful slices become complete.
5. Run the final full-scope verification required by the applicable `AGENTS.md` files.
6. Fetch and reconcile any remote advance without rewriting published work; reverify if reconciliation changed the tested tree.
7. Push all accumulated task commits through the required wrapper or documented fallback.
8. Verify the remote branch contains the local HEAD, confirm the worktree is clean, and report the branch and commit identifiers.

## Source of truth

Inherited from `/Users/mike/Projects/BriarForge/AGENTS.md`. When this file and the parent conflict, the parent wins until this file is updated to match. Product behavior is defined by `product-contract/`, not by parent workspace rules.

## Constitution

These rules apply everywhere and cannot be weakened by a child file.

- **ROOT-CONTRACT-001** The Product Contract is the source of product truth. No platform implementation becomes the specification by being first or most complete.
- **ROOT-NOT-FORK-001** This repository is not a fork of ShareX/XerahS. Do not copy that tree's structure as product definition. Treat it as a behavioral reference and fixture source with recorded provenance.
- **ROOT-PLATFORMS-001** Supported native targets are Windows, macOS, and Linux. A feature is complete when every supported platform has an accepted disposition.
- **ROOT-IDENTITY-001** Applications use `com.xerahs.app` (or the platform equivalent) and the display name "XerahS". They replace Avalonia XerahS at release; migration of existing live settings MUST require explicit user action and preserve rollback or recovery.
- **ROOT-LICENSE-001** This repository is GNU GPL v3. Dynamically link LGPL Qt. Do not add a dependency that conflicts with GPL v3.
- **ROOT-SECURITY-001** Security, privacy, accessibility, and data-compatibility invariants are not optional. Capture permission flows, credential storage, uploader secrets, and plugin hosts require human review.
- **ROOT-AUTHORITY-001** Child `AGENTS.md` files inherit ancestors, may add stricter local rules, and MUST NOT duplicate or contradict parent rule IDs. Conflict stops work for the governance owner.
- **ROOT-EVIDENCE-001** Product behavior changes follow contract, impact analysis, native implementation, conformance, parity review, then release. Landing code on one platform is not completion.
- **ROOT-IMPROVE-001** Agents are authorized and expected to perform bounded, behavior-preserving restructuring when encountered friction makes implementation, verification, navigation, or maintenance needlessly slow, fragile, ambiguous, or repetitive. No separate ticket or permission is required. Keep the requested outcome in scope, update affected tests, documentation, instructions, architecture maps, and traceability, and verify the changed surface.
- **ROOT-IMPROVE-002** Self-directed restructuring MUST NOT silently change the Product Contract, public or persisted compatibility, security or privacy posture, licensing, supported platforms or framework decisions, repository boundaries, or this root constitution. Those changes follow their applicable approval path. Avoid speculative rewrites, unrelated cosmetic churn, and refactoring that leaves the original task unfinished.
- **ROOT-NUGET-001** Agents have standing authority and responsibility to query authoritative NuGet package sources and update every repository-owned NuGet dependency to its latest stable version without case-by-case approval. This includes major-version updates and the code, project, central-version, lockfile, test, and documentation changes needed to adopt them. A check is due whenever a NuGet dependency is touched, before release qualification, and when the last recorded repository-wide check is more than 30 days old.
- **ROOT-NUGET-002** NuGet updates MUST use published stable releases, record the source and checked version, and pass applicable restore, build, test, security, licensing, and conformance checks. Agents MUST NOT bypass protected product, platform, security, privacy, compatibility, or GPLv3 boundaries merely to raise a version. If the latest stable version cannot satisfy those boundaries or the approved toolchain baseline, retain the newest conforming version only with a recorded blocker, exact rejected version, owner, and review date no more than 30 days away.
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
