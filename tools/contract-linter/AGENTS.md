# Scope
Applies to: tools/contract-linter/**
Parent: ../AGENTS.md

# Purpose
Validates the AGENTS.md hierarchy, contract manifests, and traceability references. Not yet implemented in this Phase 0 seed.

# Local Rules
- **LINTER-SCOPE-001** Stay inside governance and manifest validation. Do not execute platform tests here.

# Required Workflow
When implemented, pin the command in this file and in CI.

# Verification
None until the linter exists.

# Prohibited Changes
Shipping a linter that cannot resolve effective root-to-leaf rules for a path.

# Escalation
Schema questions go to the contract steward; hierarchy questions go to the governance owner.

# Child Scopes
None.
