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

- A required platform has no disposition for a new or changed requirement.
- A deterministic conformance vector fails.
- A required scenario fails.
- A deviation or waiver has expired.
- A platform targets a contract version incompatible with the release.

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
