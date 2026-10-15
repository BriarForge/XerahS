# Saved workflow CLI — native candidate evidence

Date: 16 October 2026. Extends the saved region-profile subset of
WORKFLOW-CATALOG-001 through the existing authenticated native instance.
This is not approval of a shared CLI/MCP schema or full interface catalogue.

## Use

Open **Capture workflows…**, select a saved profile and copy its read-only
**Workflow ID**. Run the native build with that full UUID:

```sh
build/xerahs --run-workflow 12345678-1234-4234-8234-123456789abc --json
```

Full UUID input is normalized to lowercase without braces. Names, prefixes and
nil UUIDs are refused. Conflicting operations, unexpected positional arguments
and unknown flags fail rather than selecting an operation by precedence.
`--help` describes the supported flags. Ordinary success keeps stdout empty.
`--json` emits one versioned delivery/admission object, for example:

```json
{"schemaVersion":1,"ok":true,"requestId":"…","workflowId":"…","delivery":"running-instance","state":"accepted"}
```

The request ID correlates activation, not a pipeline task or terminal result.
Exit 0 from a forwarding client means accepted; 2 means busy; 1 means rejected,
invalid, unavailable or unknown delivery. A newly started primary prints
`delivery: started` after admission and continues its normal GUI lifetime.
Neither acknowledgement proves consent, acquisition, output or upload success.
There is no CLI wait, cancel, progress or terminal-result lookup in this slice.

`--non-interactive` refuses graphical operations with `presentation-required`
before QApplication, display connection, instance election, preferences or portal
requests. It provides no headless capture. Malformed arguments/IDs still report
their validation error. `--json` errors contain stable codes and valid opaque IDs,
not raw arguments, workflow names, actions, folders, patterns or output paths.

## Shared execution and authority

The running primary is authoritative; a secondary forwards before initializing
preferences or loading profiles. Admission checks saved-definition availability,
unchanged storage and current busy/modal state. Core resolution returns an owned
snapshot before acknowledgement; later edits affect subsequent requests. Run
selected uses the same dispatcher and production CaptureController. Late modal
or capture conflicts are shown in the GUI rather than reported as output success.

The private v1 protocol adds `run-workflow` with only `workflow_id`, and
`capture-region-choose-actions` with no payload. Older instances reject unknown
actions. Callers cannot supply actions, output settings or consent authority.
Existing kernel peer authentication, intake limits, deadlines and bounded 256
process-local receipts remain. Same UUID/payload replays a receipt without a
second dispatch; changed payload is rejected. Unknown delivery never auto-resends.
The chooser now dispatches its own reserved operation, releasing the reservation
so later captures/workflows are not permanently busy.

## Verification and remaining work

Core tests cover strict wire fields, UUIDs, safe acknowledgements, frozen
preparation, reentry, busy/modal changes, replay and chooser reservation release.
Actual child applications cover accepted/busy/rejected/lost acknowledgements,
headless refusal, malformed input and missing profiles without preference writes.
Private portal/controller tests save actual synthetic PNGs using the admitted
settings, then use edited settings on the next request; unsupported profiles are
refused before another acquisition. Native controls expose the correct read-only
ID. Fixtures use temporary stores, private buses and an offscreen Qt platform;
GTK platform-theme inheritance is cleared to isolate host display helpers.
The socket/bus fixtures require execution outside this host's restricted sandbox.

The full final suite and exact pushed Ubuntu/Qt 6.4.2 CI are recorded with the
checkpoint in CONTRACT-AUDIT.md and its continuation report. All 114 existing
published vectors remain unchanged; no CORE/WFCAT vectors are published yet.
[Session global shortcuts](WORKFLOW-SHORTCUTS.md) now use the same workflow-ID
admission path. Next implement discovery, execution/result linkage and atomic
filename counters. Complete action families,
shared automation schemas, supported desktop matrix and human security/privacy/
accessibility/parity/release qualification remain Required.
