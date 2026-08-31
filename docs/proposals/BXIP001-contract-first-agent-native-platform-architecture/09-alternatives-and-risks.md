# 10. Alternatives Considered

## 10.1 Continue with Avalonia as both implementation and behavioral source

This minimizes near-term change and code duplication but leaves product intent embedded in implementation details. It does not solve agent ambiguity or parity governance.

## 10.2 Build three native applications without a formal contract

This maximizes native freedom but creates unacceptable drift risk. AI makes code generation fast enough to produce three applications; it does not ensure that they remain the same product.

## 10.3 Shared engine DLL with three native shells

This retains exact shared business logic while allowing native UI. It is a possible later optimization. Its limitation is that a shared binary and language runtime can constrain native application design and can again become an undocumented behavioral source. Under this proposal, implementation ships no shared product runtime by default ([D-SHARE-001](08-decisions.md#d-share-001-shared-binaries)). A shared engine remains subordinate to the Product Contract and requires [D-KERN-001](08-decisions.md#d-kern-001-shared-headless-rendering-kernel-threshold) or a later topology proposal.

## 10.4 Contract-first native applications

This is the proposed strategic direction. It maximizes platform independence and makes product behavior explicit. It also has the highest governance, validation, packaging, and operational burden. The qualification tranche measures and corrects the method; the pinned baseline and parity gate prevent that measurement step from becoming permanent partial scope.

# 11. Risks and Mitigations

| Risk | Consequence | Mitigation |
|---|---|---|
| Natural-language ambiguity | Three plausible but inconsistent implementations | Normative keywords, stable requirement IDs, examples, state machines, and executable vectors |
| Correlated AI mistakes | Spec, code, and tests repeat the same misunderstanding | Separate contract, implementation, and conformance roles; require product review for behavior decisions |
| Validation cost exceeds authoring savings | Native strategy becomes slower or less reliable | Measure every wave end to end, including testing and release work rather than code generation alone; amend architecture explicitly if evidence requires |
| Platform drift | Features ship on one OS and remain absent elsewhere | Traceability manifests, parity dashboards, release gates, and expiring waivers |
| Linux fragmentation | "Native Linux" behaves differently across desktops and packaging systems | [D-LIN-001](08-decisions.md#d-lin-001-linux-toolkit-desktop-display-server-portal-distro-and-packaging): Qt 6, GNOME 46+ and Plasma 6 first-class, Wayland first, Ubuntu 24.04 / Fedora current / Arch, XDG portals required |
| Duplicate security-sensitive logic | Inconsistent or vulnerable implementations | Exact test vectors, security review, protocol standards, and approved shared libraries where appropriate |
| Contract bureaucracy | Small changes become slow | Scale evidence and review requirements according to risk; allow patch-level clarifications without full product approval |
| Premature replacement | Working functionality is lost before native parity is proven | Do not distribute native builds as production replacements until the signed full-parity attestation; require recoverable migration and rollback |
| Native ecosystem churn | Three SDK and packaging stacks create operational load | Explicit platform ownership, supported OS baselines, automated builds, and the latest-stable NuGet cadence in [D-NUGET-001](08-decisions.md#d-nuget-001-latest-stable-nuget-authority) |
| Instruction sprawl | Agents miss rules or encounter conflicts | Root constitution, scoped deltas, stable rule IDs, hierarchy linting, and effective-instructions reports |
| Stale local guidance | Child rules preserve obsolete framework or command assumptions | Assigned scope owners, link checks, periodic validation, and removal of duplicated rules |
| ImageEditor submodule version skew | Editor behavior and host integration move on different revisions | Keep native editor modules in the monorepo and land contract, implementation, and evidence atomically |
| Renderer drift | Effects produce materially different images across platforms | Deterministic vectors, golden images, explicit tolerances, and a separately approved headless-kernel option if evidence requires it |
| Incomplete baseline census | Agents deliver an attractive subset while missing settings, commands, integrations, or recovery paths | Independent source and user-journey censuses, reproducible inventory, unresolved-ID reports, and product-owner census closure |
| Moving KovaForge target | Parity never converges or silently changes | Pin commit and submodule SHAs; advance only through a reviewed baseline delta |
| False parity by percentage | A high aggregate hides a critical missing workflow | Release on zero unresolved required IDs, not percentage alone |
| Legacy code becomes specification | Bugs and internal architecture are copied into all native apps | Treat KovaForge as discovery evidence; classify behavior and contract desired outcomes before implementation |
| VideoEditor is silently deferred | Native product lacks a baseline-reachable media workflow | D-VID-001, dedicated ledger domain, Wave 4, and release-blocking evidence |
| Agentic friction becomes permanent technical debt | Every later feature costs more context, navigation, retries, and correlated mistakes | D-REF-001 grants standing authority and expects durable structural, tooling, test, diagnostic, or instruction improvements |
| Opportunistic restructuring becomes unbounded churn | Agents rewrite working code, obscure feature intent, or fail to complete the requested outcome | Require a named material pain point, bounded behavior-preserving change, proportional verification, protected-boundary escalation, and completion of the original task |
| Agent hoards work in one final commit | Review, recovery, bisecting, delegation, and context-loss resilience deteriorate | D-GIT-001 requires coherent verified checkpoints and commits before handoff or likely interruption |
| Progressive commits become checkpoint noise | History fills with broken, arbitrary, or microscopic snapshots | Define checkpoints by independent meaning, valid state, relevant verification, and review/handoff value; keep them local until the final gate by default |
| Completed commits remain only on one workstation | Verified work is lost or invisible to collaborators | Treat final push and remote-HEAD verification as normal completion, with explicit pause and credential exceptions |
| Agent tooling proliferates branches and worktrees | Work fragments across histories, handoffs drift, and humans cannot tell which branch is authoritative | D-BRANCH-001 keeps work on the task's current branch and requires explicit human authorization for every branch creation or switch |
| Current branch cannot accept safe progress | Agent either loses work or silently escapes governance through another branch | Preserve the worktree, report the exact protection/upstream/conflict condition, and obtain explicit direction instead of inventing a branch |
