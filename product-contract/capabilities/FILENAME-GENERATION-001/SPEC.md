# FILENAME-GENERATION-001 Filename generation

Version: 0.2.0

Status: Approved by the human product owner on 2026-08-31 (0.1.0) and 2026-09-28 (0.2.0 implementation-readiness clarifications); conformance required before activation

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
`url`), and an expansion context. It MAY contain an extension (without a
leading `.`) and a maximum length. The output contains the expanded string and
the next counter value, or a typed error and the unchanged counter. A blank
pattern (empty, or only Unicode `White_Space`) does not advance the counter and
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
  random source. An invalid repeat count under FN-019 MUST be reported as a
  validation error rather than silently producing an unbounded value.
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
- **FN-018:** Token recognition MUST scan left to right. At each `%`, the
  implementation MUST match the longest token name in `tokens.json` that begins
  there, comparing case-sensitively; `%%` is a token. A `%` that begins no known
  token is a literal character and scanning resumes at the next character. For
  example `%widthx` is `%width` then `x`, `%rna` is never `%rn` then `a`, and
  `%Y` is literal.
- **FN-019:** Only counter tokens, random character tokens (`%rn`, `%ra`,
  `%rna`, `%rx`, `%rX`, `%remoji`), and `%rf` accept a `{...}` argument that
  immediately follows the token name and ends at the first `}`. For every other
  token a following `{` is literal. A missing `}` MUST fail with
  `unterminated-argument`. Width and repeat arguments MUST be ASCII decimal
  integers from 1 to 256 inclusive; any other value MUST fail with
  `invalid-argument`. A random character token without an argument produces one
  character; a counter token without an argument has no padding.
- **FN-020:** Date and time tokens MUST use the wall-clock fields of the
  context's `local_time` exactly as supplied, including its offset; `%unix` MUST
  use `unix_time`. `%yy` is the last two digits of the year and `%wy` is the
  ISO 8601 week number, each zero-padded to two digits. On the twelve-hour
  clock hour 0 is `12`, hours 13 to 23 subtract 12, and `%pm` is `AM` for hours
  0 to 11 and `PM` for hours 12 to 23. `%mon` and `%w` use the Unicode CLDR
  format-wide month and weekday names of the context locale; `%mon2` and `%w2`
  use the English names.
- **FN-021:** The random source is an ordered byte sequence consumed left to
  right across the whole pattern. Each random element MUST be drawn from the
  ordered list for its token in `random-lists.json` by rejection sampling: for a
  list of N entries, read one byte b; if b is at least 256 minus (256 modulo N),
  discard it and read the next byte; otherwise select entry b modulo N. `%guid`
  and `%GUID` MUST consume 16 bytes, set byte 6 to (byte 6 AND 0x0F) OR 0x40 and
  byte 8 to (byte 8 AND 0x3F) OR 0x80 as an RFC 9562 version 4 UUID, and format
  them as 8-4-4-4-12 hexadecimal digits in lowercase or uppercase respectively.
  Production random sources MUST be cryptographically secure. A conformance
  source that runs out MUST fail with `random-source-exhausted`.
- **FN-022:** Values inserted by `%t`, `%pn`, `%un`, `%uln`, `%cn`, and `%rf`
  MUST be trimmed of leading and trailing Unicode `White_Space`; `%t` and `%pn`
  then replace each U+0020 space with `_`. In `filename` and `path` mode the
  value MUST then have the FN-011 character replacement applied, including to
  `/` and `\`, so metadata never creates a path separator. In `url` mode the
  value MUST be percent-encoded as UTF-8, leaving only RFC 3986 unreserved
  characters (`A-Z a-z 0-9 - . _ ~`) unencoded. In `text` mode it is inserted
  unchanged. Literal pattern text is never percent-encoded.
- **FN-023:** In `filename` mode the implementation MUST finalize the name in
  this order: expand tokens; apply FN-011 replacement and collapse to the
  expanded text; remove trailing spaces and periods; apply FN-013; append `.`
  and the caller's extension after applying FN-011 replacement to the
  extension; truncate the part before the extension to the maximum length
  under FN-014; remove trailing spaces and periods again when no extension is
  present, applying FN-013 if that empties the name; then prefix `_` when the
  text before the first `.` equals `CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`,
  or `LPT1` to `LPT9`, ignoring case. When the prefix makes the name exceed the
  maximum length, the last scalar value before the extension MUST be removed.
  When the extension and its `.` alone reach or exceed the maximum length, the
  parse MUST fail with `name-too-long`.
- **FN-024:** In `path` mode, `/` and `\` in literal pattern text are separators
  and the result MUST use `/` between components; the native layer converts it
  at the filesystem boundary. A pattern that begins with a separator or with a
  drive prefix such as `C:` MUST fail with `path-not-relative`, and a component
  that is `..` after expansion MUST fail with `path-traversal`. Each component
  MUST be finalized as in FN-023 without an extension; the extension and the
  maximum length apply only to the last component.
- **FN-025:** The counter is a non-negative integer that fits in a signed 64-bit
  value. Advancing it beyond 9223372036854775807 MUST fail with
  `counter-overflow`. Base-36 digits are `0` to `9` followed by `a` to `z`
  (`A` to `Z` for `%iA`).
- **FN-026:** An expansion error MUST return a code from this list, the
  offending token (or `null` for a path-structure error), and the zero-based
  offset in Unicode scalar values of the token's `%` (or of the offending path
  component): `invalid-argument`, `unterminated-argument`,
  `random-source-exhausted`, `file-permission-denied`, `file-unavailable`,
  `path-not-relative`, `path-traversal`, `counter-overflow`, `name-too-long`.
  `%rf` selects among the lines of the file, split on LF or CRLF, trimmed, and
  excluding empty lines, as one random element; a file with no such line is
  `file-unavailable`.
- **FN-027:** The preview random source MUST be the byte sequence 0x00, 0x01,
  and onward, wrapping from 0xFF to 0x00. Preview MUST report the next counter
  value that execution would commit without committing it.

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
