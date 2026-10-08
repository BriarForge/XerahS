# Scope
Applies to: platforms/**
Parent: ../AGENTS.md

# Purpose
Owns native realizations of the Product Contract on Windows, macOS, Linux, Android, and iOS. Does not own the contract, shared expected results, or release approval.

# Local Rules
- **PLATFORM-CONTRACT-001** Implement from the Product Contract. Another platform's source is informative, not normative.
- **PLATFORM-PARITY-001** Every new or changed requirement needs a disposition on this platform: Required, Equivalent, Degraded, Unavailable, or Not applicable. "Not implemented" is not a disposition.
- **PLATFORM-TRACE-001** Publish a machine-readable mapping from requirement IDs to implementation and evidence.
- **PLATFORM-NATIVE-001** Use the pilot framework defaults in BXIP001 (D-WIN-001, D-MAC-001, D-LIN-001, D-AND-001, D-IOS-001) unless a later decision supersedes them; Linux editions follow D-LIN-002 and `CORE-PLATFORM-001/linux-editions.json`.
- **PLATFORM-IDENTITY-001** Package identity is `com.xerahs.app` (or the platform equivalent) and the display name is "XerahS". Replacement packaging MUST preserve explicit settings migration, rollback, and recovery safeguards.
- **PLATFORM-STRUCTURE-001** Under ROOT-IMPROVE-001, platform agents MAY reshape internal modules, solution or package boundaries, adapters, build tooling, and tests when doing so removes a material development pain point. Preserve contract IDs and externally governed boundaries, keep traceability navigable, and validate every affected platform surface.

# Required Workflow
1. Read the relevant capability contract and scenarios.
2. Produce a platform-specific plan.
3. Implement with native APIs and conventions.
4. Add platform tests and update the traceability manifest.
5. Do not mark complete until conformance and parity review have a disposition.

# Verification
Platform unit/integration tests plus shared conformance adapters under `conformance/adapters/<platform>/`.

# Prohibited Changes
- Silently omitting a contracted capability.
- Weakening the contract to pass a test.
- Shipping as the Avalonia product identity.
- Adding a shared UI or capture library without D-SHARE-001 / D-KERN-001.

# Escalation
Infeasible native requirements: clarification, deviation, time-bound waiver (max 90 days), or scope change.

# Child Scopes
Platform-specific `AGENTS.md` files are added when Windows, macOS, or Linux implementations exist.

- [linux/](linux/AGENTS.md)
