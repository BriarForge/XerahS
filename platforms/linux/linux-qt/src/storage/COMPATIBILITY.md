# Current native reader boundaries

COMPCAT-004, 005, 006, 015, 016. This describes the current Linux readers,
not approval of every legacy format or cross-platform qualification.

Import reports contain six typed counts: retained, transformed, defaulted,
quarantined, rejected and ignored. Counts may overlap: an invalid field is
rejected and its replacement defaulted. Fields and annotation/effect records are
counted at their respective boundaries; these are not byte counts. Summaries
never include imported keys, values, filenames or credentials. Rejection leaves
live state unchanged (settings may use defaults in read-only mode). Existing
field-specific repair diagnostics remain separate from the report.

## Unknown data and quarantine

Compatible opaque JSON values survive semantic round-trip, including nested
arrays/objects. Whitespace, object key order and numeric spelling are not
preserved. Unknown fields with executable, credential, authority, required-feature
or native-handle names, including nested `$ref`/`$type`, are refused. This is a
conservative name classifier plus schema validation, not a general detector of
secrets embedded in arbitrary prose. No imported value grants consent.

XANN `UnsupportedAnnotation.raw` and unsupported preset steps (`legacy`,
`parameters`, `extra`, `supported=false`) are the inert quarantine representations.
They remain inspectable and serializable; unsupported steps never execute.
Known preset `$type` names are matched to the static mapping table, never loaded
as runtime types. XANN native effect-preset operation receipts recognize these
legacy type tags only in their declared inert receipt positions; tags elsewhere
remain unsafe unknown data. Existing supported-step-only Apply is an explicitly degraded
operation. SXCU `SxcuImportResult.quarantine` is candidate-only JSON, separate
from destination `extra`: it is neither persisted nor applied. Any such content
blocks catalog admission, as does unsupported critical syntax. Known SXCU
headers/query/arguments separate secrets into the credential candidate and retain
only secret references. Unknown compatible metadata using the existing secret
extraction convention is scrubbed too; unknown authentication behavior blocks
admission. Diagnostics and UI reports do not display quarantine values.

Settings refuse unsafe candidates and preserve the original file for recovery;
unknown toolbar entries remain inert and are counted as quarantined. Previous
settings snapshots undergo the same unsafe-content checks. The classifier and
credential boundary require human security review before qualification.

## Resource limits and malformed input

`JsonBoundary.h` validates strict UTF-8 (including surrogate escapes), duplicate
decoded keys, nesting, byte and lexical element budgets before Qt constructs
objects. A lexical element is a container, member name or primitive value.
The diagnostic suffixes are `invalid-json`, `invalid-encoding`, `duplicate-key`,
`too-deep`, `too-large`, `too-many-elements`; callers add their stable format
prefix. No reader performs network access or follows imported references.
The native workflow, destination and pipeline-journal readers apply the same
preflight (1 MiB, depth 64, 100,000 elements); they are not importers and
produce no report. `$ref` is unsafe opaque data, so reference cycles cannot be dereferenced.

| Reader | Bytes | Depth | Lexical elements | Compression |
| --- | --- | --- | --- | --- |
| XANN | 256 MiB compressed, 1 GiB decoded ceiling | 64 | 1,000,000 | Single complete gzip member, expansion <= max(1 MiB, compressed bytes * 1024); trailing members/data rejected |
| SXCU | 1 MiB | 32 | 100,000 | Not accepted |
| SXIE Config.json | 4 MiB | 64 | 100,000 | Existing bounded ZIP reader: 160 MiB input, 256 entries, 32 MiB/entry, 128 MiB total, ratio <= 200 |
| XSIE | 192 MiB | 64 | 100,000 | Not accepted |
| Editor/application settings | 1 MiB | 64 | 100,000 | Not accepted |

Embedded path-like fields reject `..` components (except the editor recent-image list, whose absolute opened-file identities keep kernel meaning through symlinks and are never read while parsing) with either separator; asset
archive names also reject absolute/drive paths. Paths remain data and do not
cause file reads during parsing. Native profile settings have separate known
path validation. Existing file stores use private atomic replacement with direct
write fallback disabled; a failed write does not replace the previous artifact.

## Canonical rules and exports

* JSON uses UTF-8, LF formatting, locale-independent `.` decimals and JSON numeric
  syntax. Both LF and CRLF inputs are accepted. Composed/decomposed display names
  (SXCU/presets), editor font family and portable XANN basename output use NFC.
  Opaque extension strings and actual filesystem path identities remain unchanged;
  normalizing an opaque value could corrupt a newer format or a real Linux path.
* XANN times serialize as UTC ISO-8601 with milliseconds and `Z`; offset-bearing
  input is converted to UTC. Timezone-less ISO inputs keep Qt's local interpretation, which
  matches the reference app (.NET parses an unspecified-kind DateTime as local); the
  next save writes UTC with `Z`, so the value is normalized on first rewrite. Other readers have no active timestamp fields.
* Stable native IDs/enums are case-sensitive; UUIDs serialize lowercase without
  braces. SXCU HTTP method/body aliases are accepted case-insensitively and use
  existing canonical spellings. SXIE type names use exact static matches;
  Config.json discovery alone is case-insensitive. Opaque IDs are unchanged.
* XANN export targets `xann`, version 1, substitutes the NFC basename for local
  directory information and reports `nonportable-image-path`. XANN bytes are
  published by the existing atomic sidecar store. Missing companions are never
  fetched. XSIE export targets `com.xerahs.app/effect-preset`, version 1, and
  reports `external-asset-not-embedded` for unresolved legacy image locations.
  Its native dialog uses PrivateJsonFile for atomic publication. Unsafe unknown
  content cannot be exported through these surfaces.
* Preference/settings saves identify their native format/version and report
  `profile-local-recent-paths`, `profile-local-window-state` and/or
  `profile-local-save-folder`. These codes identify non-portable profile data
  retained for local operation; these saves are not portable backup exports.
  There is no native SXCU/destination export UI or new export feature in this
  slice; `writeDestination` is internal versioned catalog serialization.

The fixture-generating tests `JsonBoundaryTest`, `XannCompatibilityTest`,
`SettingsCompatibilityTest`, `SxcuImportTest` and `SxiePresetTest` exercise locale,
newline, Unicode and adversarial variants against production readers. Windows,
macOS, legacy runtime observations and the remaining catalog formats still need
qualification; these rows remain partial.

Read-only reference consulted: XerahS `SettingsBase.cs` lines 420–434 writes
strict UTF-8, UTC and formatted JSON; `CustomUploaderSettingsSerializer.cs`
lines 33–56 omits default/null values and preserves the uploader name. These
support UTC/UTF-8 and explicit defaults here, but do not establish the additional
native safety limits or grant serializer type authority.
