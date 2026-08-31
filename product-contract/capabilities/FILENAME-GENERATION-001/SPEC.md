# FILENAME-GENERATION-001 Filename generation

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

## User intent

Users can create predictable, sortable, collision-resistant output names from
time, capture, window, environment, counter, and random values without producing
names that fail on a supported platform.

## Definitions

- **Pattern**: UTF-8 text containing literal characters and `%token` entries.
- **Expansion context**: the timestamp, time zone, locale, counter, capture size,
  window metadata, environment values, and random source supplied for one parse.
- **Portable filename**: a single path component valid on Windows, macOS, and
  Linux under this contract.
- **Counter token**: `%i`, `%i{n}`, `%ia`, `%ia{n}`, `%iA`, `%iA{n}`, `%ix`, or
  `%iX`, where `n` is the minimum output width.

## Preconditions, inputs, and outputs

The input MUST contain a pattern, a parse mode (`filename`, `path`, `text`, or
`url`), and an expansion context. The output contains the expanded string and
the next counter value. A blank pattern does not advance the counter and
produces `_` in `filename` or `path` mode and an empty string otherwise.

## Requirements

- **FN-001:** Implementations MUST expand tokens using one immutable expansion
  context per parse. The clock, locale, environment, and random source MUST NOT
  change between tokens in the same parse.
- **FN-002:** `%y`, `%yy`, `%mo`, `%d`, `%h`, `%mi`, `%s`, `%ms`, `%pm`, `%wy`,
  `%w`, `%w2`, `%mon`, `%mon2`, and `%unix` MUST expand as defined by
  `tokens.json`. Numeric output MUST use ASCII digits. `%mo`, `%d`, `%h`, `%mi`,
  and `%s` MUST be two digits; `%ms` MUST be three digits.
- **FN-003:** `%pm` MUST select twelve-hour `%h` output and expand to `AM` or
  `PM`. Without `%pm`, `%h` MUST use the 24-hour clock.
- **FN-004:** `%width` and `%height` MUST expand to the positive capture pixel
  dimensions in base ten, or to an empty string when the dimension is unknown.
- **FN-005:** `%t` and `%pn` MUST expand to the trimmed window title and process
  name. Spaces MUST become underscores. In `filename` or `path` mode, inserted
  metadata MUST be sanitized before insertion.
- **FN-006:** `%un`, `%uln`, and `%cn` MUST expand from the supplied user,
  login-domain, and computer-name context. Native implementations MUST NOT read
  a different value after parsing begins.
- **FN-007:** The presence of any counter token MUST advance the supplied
  zero-based counter exactly once before all counter tokens expand. `%i` is
  decimal, `%ia`/`%iA` are lower/upper base-36, and `%ix`/`%iX` are lower/upper
  hexadecimal. `{n}` specifies minimum zero-padding and MUST NOT truncate a
  wider value.
- **FN-008:** `%rn{n}`, `%ra{n}`, `%rna{n}`, `%rx{n}`, `%rX{n}`, `%guid`,
  `%radjective`, `%ranimal`, and `%remoji{n}` MUST use the supplied cryptographic
  random source. A missing or invalid positive repeat count MUST be reported as
  a validation error rather than silently producing an unbounded value.
- **FN-009:** `%rf{path}` MAY read one random line only after the caller grants
  file-read permission. A denied, missing, non-text, or empty file MUST produce
  a typed expansion error and MUST NOT expose unrelated filesystem data.
- **FN-010:** Unknown tokens MUST remain unchanged so a pattern can round-trip
  across a newer contract version. A literal percent sign MUST be written as
  `%%` and expand to one `%`.
- **FN-011:** In `filename` mode, implementations MUST replace every control
  character U+0000 through U+001F and each of `< > : " / \\ | ? *` with `_`,
  collapse consecutive replacements to one `_`, and remove trailing spaces and
  periods. The result MUST NOT equal a Windows reserved device name, ignoring
  case and extension; prefix `_` when it would.
- **FN-012:** In `path` mode, each path component MUST use FN-011 while native
  separators remain separators. Absolute paths, parent traversal, and root
  changes MUST be rejected unless the calling capability explicitly permits
  them.
- **FN-013:** In `filename` and `path` mode, an empty result, `.` or `..` MUST
  become `_`. The caller's extension MUST be appended only after pattern
  expansion and MUST NOT be interpreted as a token.
- **FN-014:** A configured maximum length MUST count Unicode scalar values.
  Truncation MUST preserve the extension and MUST NOT split a scalar value.
- **FN-015:** Preview MUST use the same expansion and sanitization rules as
  execution, but MUST use a non-committing counter and deterministic preview
  random source.
- **FN-016:** Counter persistence MUST be atomic relative to successful name
  reservation. Cancellation or validation failure before reservation MUST NOT
  consume a counter; a successfully reserved name MUST consume it even if a
  later workflow action fails.
- **FN-017:** Implementations MUST pass every deterministic vector in
  `test-vectors.json` byte-for-byte in UTF-8.

## Defaults and compatibility

The default capture pattern is `%y%mo%dT%h%mi_%ra{10}` and the active-window
default is `%y%mo%dT%h%mi_%pn_%ra{10}`. Existing KovaForge patterns using the
tokens listed in `tokens.json` MUST import without token renaming. The portable
sanitization in FN-011 intentionally corrects the baseline's OS-dependent
invalid-character behavior; migration previews MUST show any changed output.

## Failure and recovery

Validation and expansion errors MUST identify the offending token and pattern
offset. They MUST NOT create a file, advance a persistent counter, or silently
fall back to an unrelated name. Collision handling belongs to the saving
capability; it MUST use the fully expanded portable name as its input.

## Cross-platform, accessibility, privacy, and performance

All supported platforms MUST return identical output for identical context and
random bytes. Native pattern editors MAY differ visually but MUST expose token
descriptions, validation errors, and a screen-reader-readable preview. Window,
user, host, and random-file values are potentially sensitive and MUST NOT be
logged unless diagnostic consent includes them. A pattern without `%rf` SHOULD
complete in under 10 ms at the 95th percentile on supported hardware.

## Baseline traceability

- Ledger: `FILENAME-GENERATION-001` and settings rows for
  `TaskSettingsUpload.NameFormatPattern`,
  `TaskSettingsUpload.NameFormatPatternActiveWindow`, and
  `ApplicationConfig.NameParserAutoIncrementNumber`.
- Evidence: `src/desktop/core/XerahS.Common/Helpers/NameParser.cs`,
  `CodeMenuEntryFilename.cs`, and `TaskHelpers.cs` at baseline commit
  `5c7e36dea77ab131fe0f5e2101e5d578ccde0306`.
- Disposition: preserve token compatibility and defaults; correct the
  platform-dependent sanitization and non-atomic counter behavior.
