# COMPATIBILITY-CATALOG-001 Persisted-data compatibility

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; fixtures, runtime observation, and conformance required before activation

## Requirements

- **COMPCAT-001:** Every compatibility-ledger row linked to this package MUST
  retain its stable ledger ID and receive an approved format identity, version
  detection, import, export, round-trip, migration, rejection, unknown-field,
  corruption, security, and platform policy before activation.
- **COMPCAT-002:** Compatibility MUST be derived from documented schemas,
  representative fixtures, and runtime observation. A source type declaration
  or serializer choice alone MUST NOT establish a compatibility promise.
- **COMPCAT-003:** Import MUST parse and validate into an isolated candidate
  model before changing live configuration, history, credentials, workflows,
  plugins, documents, or automation. Failure MUST identify the affected record
  without partially mutating unrelated live state.
- **COMPCAT-004:** Import results MUST report fields or records retained,
  transformed, defaulted, quarantined, rejected, and ignored. Silent data loss
  is prohibited, and unsupported critical behavior MUST block acceptance.
- **COMPCAT-005:** Unknown compatible fields MUST survive round-trip when their
  representation can be retained safely. Unknown executable, authority-bearing,
  secret, or structurally critical content MUST be quarantined or rejected.
- **COMPCAT-006:** Export MUST use an explicit version and target, write
  atomically, avoid machine-specific native handles, and omit or safely
  substitute secret material. An export MUST identify non-portable omissions.
- **COMPCAT-007:** Application, uploader, and workflow JSON MUST preserve stable
  semantic IDs rather than runtime type names; validate values and references;
  and define upgrade, downgrade, enum, path, color, geometry, and collection
  behavior independently of serializer defaults.
- **COMPCAT-008:** Legacy secret stores MUST be treated as sensitive even when
  unencrypted. Migration MUST require explicit consent, use native secure
  storage, verify before removal, redact all reporting, and never re-export the
  plaintext through ordinary configuration compatibility.
- **COMPCAT-009:** History JSON, XML, and SQLite imports MUST preserve stable task
  identity, timestamps, operation state, URLs, local output references, and
  privacy classification where representable; tolerate missing media; and
  quarantine corrupt records without discarding valid neighbors.
- **COMPCAT-010:** SQLite compatibility MUST operate on a consistent snapshot,
  validate schema and integrity, avoid executing triggers or extensions from an
  untrusted database, bound resource use, and record transaction-level failure.
- **COMPCAT-011:** SXCU custom-uploader import MUST treat templates, regex,
  headers, bodies, URLs, and scripts as untrusted data; separate credentials;
  validate network targets; and require explicit review before enabling external
  requests or any executable transformation.
- **COMPCAT-012:** Plugin package compatibility MUST verify provenance and
  manifest schema, enumerate requested authority, prevent path traversal and
  native-library escape during inspection, and require explicit reviewed
  installation into the isolated plugin host.
- **COMPCAT-013:** ImageEditor document and effect compatibility MUST preserve
  canvas geometry, layer and annotation order, stable tool/effect IDs,
  parameters, colors, fonts, assets, and metadata policy. Unknown effects or
  missing resources MUST remain inspectable and use an explicit degraded result
  rather than silently flattening or discarding editable content.
- **COMPCAT-014:** MCP automation JSON MUST use versioned stable tool and resource
  identifiers, MUST NOT deserialize authority or human consent from untrusted
  text, and MUST validate each operation against current schemas and policy
  before it can be invoked.
- **COMPCAT-015:** Malformed, deeply nested, oversized, cyclic, compressed,
  duplicate-key, invalid-encoding, and path-traversal inputs MUST be bounded and
  rejected safely without excessive resource use, code execution, network
  access, or disclosure of local data.
- **COMPCAT-016:** Time, locale, numeric, case-sensitivity, path, newline, and
  Unicode differences MUST have canonical rules and cross-platform fixtures.
  Import on one supported platform MUST NOT make data unreadable on another
  without an explicit non-portable disposition.
- **COMPCAT-017:** A migration MUST be repeatable or detect prior completion,
  preserve an approved rollback or source backup until verification, and record
  format versions and redacted outcomes. Retrying MUST NOT duplicate accounts,
  workflows, history entries, annotations, or remote side effects.
- **COMPCAT-018:** Activation MUST include golden fixtures from every supported
  legacy format and version, corrupt and adversarial fixtures, unknown-field and
  unknown-record cases, upgrade and downgrade, cross-platform round-trip,
  secret redaction, idempotent migration, and runtime-observation evidence.

## Baseline traceability and disposition

The compatibility parity ledger identifies candidate promises found in the
pinned baseline. Linking a row here establishes an approved review owner. It does
not guarantee byte-for-byte output, authorize legacy code execution, or approve
secret portability.
