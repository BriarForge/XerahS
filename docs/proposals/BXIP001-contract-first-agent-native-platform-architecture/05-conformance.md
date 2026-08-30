# 7. Conformance and Release Governance

## 7.1 Traceability manifest

Each platform SHALL publish a machine-readable mapping from contract requirements to implementation and evidence. Conceptually:

```yaml
capability: POST-CAPTURE-CLIPBOARD-001
contract_version: 1.2.0
platform: macos
requirements:
  PCC-001:
    status: implemented
    tests:
      - ClipboardActionTests.copy_valid_image
  PCC-002:
    status: implemented
    tests:
      - ClipboardActionTests.failure_preserves_capture
  PCC-004:
    status: implemented
    evidence:
      - pasteboard-preview-golden.png
```

The manifest provides coverage and traceability. It does not prove correctness by itself.

## 7.2 CI parity gate

CI SHALL produce a capability report such as:

| Check | Windows | macOS | Linux |
|---|---:|---:|---:|
| Contract version accepted | Pass | Pass | Pass |
| Required scenarios | Pass | Pass | Pass |
| Deterministic vectors | Pass | Pass | Pass |
| Native integration tests | Pass | Pass | Pass |
| Accessibility checks | Pass | Pass | Pass |
| Unexpired deviations | None | None | 1 |

The release gate SHALL fail when:

- An in-scope baseline ledger row is unclassified, uncontracted, or missing source evidence.
- A required platform has no disposition for a new or changed requirement.
- A deterministic conformance vector fails.
- A required scenario fails.
- A deviation or waiver has expired.
- A platform targets a contract version incompatible with the release.
- A claimed implementation has no matching conformance or native integration evidence.
- A baseline-compatible data format or setting lacks migration evidence.

## 7.3 Platform capability matrix

Not all operating systems expose the same capabilities. The contract SHALL distinguish:

- **Required**: release-blocking on this platform.
- **Equivalent**: delivered through a different native mechanism with the same product outcome.
- **Degraded**: supported with a documented limitation and user-facing explanation.
- **Unavailable**: impossible or intentionally unsupported, with product approval.
- **Not applicable**: the concept has no meaning on the platform, with rationale.

"Not implemented" is not a permanent capability category.

## 7.4 Compatibility

Contract versions SHOULD follow semantic compatibility principles:

- Patch: clarification or added evidence with no product behavior change.
- Minor: backward-compatible capability or optional behavior.
- Major: incompatible behavior, persistence, plugin, automation, or integration change.

User data, configuration, history, automation, and plugin compatibility MUST be explicitly addressed when a contract version changes.

## 7.5 Baseline coverage report

CI SHALL publish a baseline coverage report grouped by the capability domains in [section 21.2](12-full-parity-delivery.md). For every domain and platform it reports counts and the unresolved IDs for:

- discovered and inventoried
- contracted
- implemented
- conformant
- release-verified
- corrected defects
- approved retirements or intrinsic limitations
- expired or active waivers

The report fails closed on unknown or empty states. A percentage is informational only; the unresolved ID list is the actionable source.

The first full-parity release additionally requires the signed parity attestation defined in [section 21.3 Wave 8](12-full-parity-delivery.md#wave-8---full-parity-release-candidate).
