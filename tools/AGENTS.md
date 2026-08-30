# Scope
Applies to: tools/**
Parent: ../AGENTS.md

# Purpose
Owns tooling that validates the governance system and Product Contract structure. Does not own product behavior or native implementations.

# Local Rules
- **TOOL-LINT-001** The contract linter is blocking on broken parent/child AGENTS.md links, missing or duplicate rule IDs, child override of protected ROOT-* rules, undeclared scopes, and missing traceability updates.
- **TOOL-EFFECTIVE-001** CI SHALL publish an effective-instructions report for every changed path.
- **TOOL-NO-POLICY-001** Tooling reports violations. It does not change contracts or waive platforms.

# Required Workflow
Keep linter rules aligned with ROOT-* and D-AGT-001. A linter change that weakens a protected root check requires the governance owner.

# Verification
Linter self-tests and a fixture repository path that is known-good and known-bad.

# Prohibited Changes
- Skipping hierarchy checks to unblock a platform change.
- Encoding product behavior in the linter that belongs in `product-contract/`.

# Escalation
Ambiguous rule-ID conflicts go to the governance owner.

# Child Scopes
- [contract-linter/AGENTS.md](contract-linter/AGENTS.md)
