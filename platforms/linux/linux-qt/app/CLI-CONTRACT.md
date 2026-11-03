# CLI contract — native candidate evidence (INTERFACE-CATALOG-001 IFCAT-008)

Scope: the `xerahs` command line on Linux (`linux-qt`). Every statement here is
checked by an actual-process test in `tests/CliContractTest.cpp`,
`tests/NativeInstanceTest.cpp` or `tests/TerminationSignalsTest.cpp`. Native,
physical and human qualification are not claimed.

## Names, help and machine-readable mode

Operation flags are stable long names: `--capture-region`, `--capture-full-screen`,
`--capture-monitor`, `--capture-active-window`, `--edit FILE`, `--run-workflow ID`,
`--list-workflows`, `--workflow-result REQUEST_UUID`, `--start-recording`,
`--start-region-recording`, `--stop-recording`. Modifiers are `--json` (versioned
output, `"schemaVersion": 1`) and `--non-interactive`. `--help` lists them and
needs no display. At most one operation may be selected; anything else is
`usage-invalid`.

## Precedence: arguments and environment

Arguments alone choose the operation and the output mode. The application reads
no `XERAHS_*` variable: there is no environment switch for the operation,
`--json`, `--non-interactive` or verbosity, so an argument can never conflict with
the environment, and ambient `LANG`/`LC_ALL`/`TERM`/`NO_COLOR` do not change a byte
of output. The environment is only used to find the user's profile and session:
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_CACHE_HOME`,
`XDG_RUNTIME_DIR` (profile, private storage, instance endpoint) and the Qt/display
variables of the graphical session. There is no path argument that could
compete with them (`--edit` takes an input image, not a profile location).
Rationale: the reference XerahS/ShareX command lines are argument-only.

## Standard streams

| Mode | stdout | stderr | Exit |
|---|---|---|---|
| `--help` | help text | empty | 0 |
| `--list-workflows`, success | one JSON line | empty | 0 |
| `--list-workflows`, failure | one JSON error line | one `xerahs: <code>: <message>` line, unless `--json` | 1 |
| `--workflow-result`, success | one JSON line | empty | 0 |
| `--workflow-result`, failure | one JSON error line | one `xerahs: Execution could not be read…` line, unless `--json` | 1 |
| launch (capture, edit, recording, run-workflow), accepted | empty, or one JSON line with `--json` | empty | 0 |
| launch, refused or failed | empty, or one JSON error line with `--json` | one `xerahs: <code>: <message>` line (always, including with `--json`) | 1, or 2 for busy |
| usage errors | as launch, refused | as launch, refused | 1 |

stdout carries only the requested result (help text or JSON documents, one per
line, never mixed with prose). stderr carries only a single human diagnostic
for a failure, containing a stable code and fixed text, never raw arguments.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success, or request accepted (acceptance is not capture/output completion) |
| 1 | Refused, invalid, unavailable or unknown delivery |
| 2 | Busy: the running instance rejected the request before starting anything |
| 130 | Primary instance cancelled by SIGINT (128 + 2) |
| 143 | Primary instance cancelled by SIGTERM (128 + 15) |

## Signal handling

The graphical primary installs SIGINT and SIGTERM handlers after it wins instance
election (`src/shell/TerminationSignals`). The signal is turned into an event-loop
request: the loop exits, windows close, owned work is cancelled by the normal
teardown, the instance lock is released, and the process exits 128 + signal.
A repeated signal during shutdown is ignored; SIGKILL is the only forced stop.
The short-lived commands (help, list, result lookup, forwarding client, headless
refusals) and the instant before election keep the default action, which ends the
process by the signal and reports the same 128 + signal status in a shell.
Rationale: 130/143 is the Unix shell convention; the reference daemons also
cancel on Ctrl+C rather than being killed.

## Non-interactive behavior

See [workflow CLI evidence](WORKFLOW-CLI.md): `--non-interactive` refuses every
operation that needs graphical interaction with `presentation-required` before
QApplication, a display connection, instance election, preferences or portal
requests, and never forwards to a running instance.

## Redaction (IFCAT-010)

Output is redacted by default and there is no switch that weakens it. The CLI
prints only stable codes, fixed message text, opaque request/workflow UUIDs,
versioned JSON fields and (for `--list-workflows`) the user's own workflow IDs
and names. It never prints arguments, input or output paths, profile and
storage locations, saved workflow folders/patterns/extra fields, capture
content, credentials or tokens, or stack traces; `--help` shows only the
invocation path the user typed. There is no verbose or diagnostic flag:
`--verbose`, `--debug`, `--diagnostics`, `--log` and short forms are refused as
`usage-invalid` without echoing the value, and ambient diagnostic settings
(`QT_LOGGING_RULES`, any `XERAHS_*` variable) change nothing in the output.
Verified for every mode in `CliContractTest::redactionHoldsInEveryCliModeEvenWithDiagnosticSettings`.
