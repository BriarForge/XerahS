# Scope
Applies to: tools/contract-linter/**
Parent: ../AGENTS.md

# Purpose
Validates the AGENTS.md hierarchy, contract manifests, and traceability references.

# Local Rules
- **LINTER-SCOPE-001** Stay inside governance and manifest validation. Do not execute platform tests here.

# Required Workflow
Run `python tools/contract-linter/lint.py` after contract, parity, schema, or
scoped-instruction changes. Run
`python -m unittest discover tools/contract-linter/tests -v` after linter changes.

# Verification
Both commands in Required Workflow MUST pass.

# Prohibited Changes
Shipping a linter that cannot resolve effective root-to-leaf rules for a path.

# Escalation
Schema questions go to the contract steward; hierarchy questions go to the governance owner.

# Child Scopes
None.
