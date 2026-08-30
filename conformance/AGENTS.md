# Scope
Applies to: conformance/**
Parent: ../AGENTS.md

# Purpose
Owns independent verification that a native implementation satisfies the Product Contract. Does not own product requirements or native UI.

# Local Rules
- **CONF-INDEPENDENT-001** Do not derive expected results solely from one platform implementation.
- **CONF-CONTRACT-001** Expected results come from the Product Contract, fixtures, and test vectors.
- **CONF-GOLDEN-001** Golden-image comparison follows D-GOLD-001. Missing tolerances fail closed.
- **CONF-RUNNER-001** The pilot runner is a pinned Python 3.12-or-later CLI. Platform adapters are thin executables.
- **CONF-ORACLE-001** A legacy Avalonia/ShareX oracle, if used, runs only in isolated development or compatibility tooling and is never packaged with a native app.

# Required Workflow
1. Add or update scenarios and vectors with the contract change.
2. Run shared scenarios and deterministic vectors on each adapter.
3. Record pass/fail and dispositions. Do not edit goldens to match a buggy renderer.

# Verification
CI SHALL produce the parity report in BXIP001 §7.2. The release gate fails on missing dispositions, failed vectors, failed required scenarios, expired waivers, or incompatible contract versions.

# Prohibited Changes
- Copying pixels or outputs from one platform to define expected results for another.
- Widening golden tolerances without the conformance owner.
- Packaging ShareX.ImageEditor or Avalonia XerahS into a native app.

# Escalation
Renderer drift that exceeds D-GOLD-001 after remediation is evidence for D-KERN-001, not a reason to weaken the contract.

# Child Scopes
- [image-editor/](image-editor/) (AGENTS.md when editor fixtures need a distinct scope)
