# Scope
Applies to: product-contract/**
Parent: ../AGENTS.md

# Purpose
Owns normative product behavior: capability contracts, requirement IDs, schemas, glossary, versioning, compatibility, and product approval records. Does not own native UI, OS integrations, or conformance expected-result derivation.

# Local Rules
- **CONTRACT-NORM-001** Use RFC 2119 keywords. MUST/MUST NOT are required behavior; SHOULD has a documented exception path; MAY is optional.
- **CONTRACT-ID-001** Requirement IDs are stable and never reused for a different meaning. Capability directories are `<AREA>-<SLUG>-<NNN>/`.
- **CONTRACT-EVIDENCE-001** Pilot capabilities SHALL include machine-readable scenarios, schemas, or test vectors. Prose alone is not enough for deterministic behavior.
- **CONTRACT-NO-FIT-001** Do not change a contract solely to satisfy an implementation. Infeasible requirements escalate under ROOT-ESCALATE-001.
- **CONTRACT-VERSION-001** Versioning follows SemVer as in BXIP001 §7.4. The contract version is the product version native apps may claim.

# Required Workflow
1. Write or update SPEC.md and acceptance evidence before platform work.
2. Record impact on formats, integrations, and all three platforms.
3. User-visible MUST/MUST NOT changes require the human product owner.

# Verification
- `python tools/contract-linter/lint.py` and `python tools/contract-backlog/tracker.py --check` pass.
- Manifest lists every capability, version, and requirement ID.

# Prohibited Changes
- Weakening MUST/MUST NOT to match a failing implementation.
- Reusing a requirement ID.
- Treating ShareX/XerahS or Avalonia source as the specification.

# Escalation
Ambiguous intent, Unavailable/Degraded dispositions, and waivers go to the product owner.

# Child Scopes
None. Add an indexed child `AGENTS.md` when a subtree, such as the ImageEditor capabilities under `capabilities/EDITOR-*`, needs a distinct governance boundary.
