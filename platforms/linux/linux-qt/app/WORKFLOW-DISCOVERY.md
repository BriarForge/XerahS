# Headless saved-workflow discovery — native candidate evidence

Date: 16 October 2026. CORE-024/027, WFCAT-003/016/017/018 and
IFCAT-008/009/010 partial native behavior; no shared CLI/MCP acceptance is claimed.

## Use

```sh
build/xerahs --list-workflows --non-interactive --json
build/xerahs --help
```

Listing always writes one compact versioned JSON object and newline to stdout,
including when `--json` is omitted. Exit 0 means the saved definitions were read;
exit 1 means argument, format, changed-storage or private-storage validation failed.
Errors contain a stable code; fixed human advice goes to stderr unless `--json`
is set. No raw argument, storage path or file/exception payload is echoed.
Help returns text without GUI, instance election, preference reads or desktop buses.

The result contains `schemaVersion: 1`, `ok`, `scope: saved-definitions`,
`source` (`saved-storage` or `initial-defaults`), `runtimeStatus: not-queried`,
`defaultWorkflowId`, and `workflows`. Each entry contains its canonical full UUID,
name, capture (`region` or `unsupported`), and `definitionAvailable`. Unavailable
entries add `diagnosticCode`. This explicit metadata query reveals workflow names;
it excludes output folders, filename patterns, actions, private extensions and
unknown capture labels. Names/IDs keep saved order; duplicate names require IDs.

Use a returned ID with `--run-workflow <full-UUID>` in the graphical session.
Definition availability uses the same Core support/output validation as native
Run and shortcuts. It does not establish capture consent, portal/clipboard/service
availability, current busy state or eventual output success. The running primary
still validates its own immutable saved definition at admission.

## Read and authority boundary

The query reads only the native private `capture-workflows.json` through the same
QtCore store as the GUI; both use one native path helper and one implemented-action
list. The adapter links QtCore production logic and performs no QWidget, activation,
portal or clipboard operation. It reads no Avalonia preferences or result/history.

A missing file reports the ordinary initial default without creating directories
or settings. Existing v1 definitions are described without upgrading their bytes.
Corrupt/newer/unsafe files are preserved and produce no partial catalogue. The store
rechecks its original byte snapshot before publishing the description; changed
storage fails explicitly. Existing bounds remain 1 MiB and 64 profiles. Compatible
unknown data stays in storage without being printed or given new authority.

The scope is the saved disk snapshot, not an active-instance query or unsaved GUI
draft. An externally edited file can differ from the running primary's loaded
snapshot; execution will then refuse until explicit Reload. Listing does not
activate that process, bypass its busy/modal guards, repair storage, grant consent
or fabricate an activation/execution/task ID. It can run while a primary is busy.

Combining listing with capture, edit or workflow-run flags is `usage-invalid`;
unknown options, unexpected positionals and a value for the valueless listing flag
also fail before GUI or filesystem initialization. `--non-interactive` permits this
read-only query while continuing to refuse graphical operations. Valid help flags
return text after argument parsing, before operation validation or data reads.

## Verification and remaining work

Core cases verify defaults, saved order/default/availability, unsupported capture
redaction, known-field projection, private-field exclusion, unchanged bytes and
stale-storage refusal. Actual child applications run without DISPLAY/Wayland on an
unusable xcb platform and unavailable desktop bus: missing/saved/v1/unavailable,
corrupt/newer/unsafe/symlink stores, a busy authenticated primary, conflicting/
invalid arguments and headless help. They verify one JSON response, exact original
bytes, no preference/runtime/state/cache side effects, no primary activation and
no private fixture paths/patterns/extensions. All stores and peers are temporary.
The full suite, tooling and exact pushed Ubuntu/Qt 6.4.2 CI are recorded with this
checkpoint in the audit and continuation report. Shared expectations stay intact.

Execution identity, invocation origin/settings/acquisition failure records, terminal
result lookup/wait/cancel, full workflow/interface families and shared schemas remain
Required. All supported desktop/privacy/security/accessibility/parity/release gates
remain human or target qualification work. Next link admitted invocations to durable
execution/result identity, then implement atomic filename counters and remaining
capture families. Passing this query does not establish full app completion.


The first exact Ubuntu/Qt 6.4.2 run, 38780652747, passed discovery/native-instance,
Core workflows, shortcuts and conformance. Three existing picker accept cases in
ImageComparisonDialogTest and NewImageDialogTest failed: Qt 6.4 selectFile leaves
a visible focused filename field unchanged. These fixtures now navigate to the fixture directory, set the
actual fileNameEdit and assert selectedFiles before normal acceptance. All prior
reentry, lifetime, pixel/file and timeout assertions remain. No product editor or
capture behavior, shared expectation, test case or deadline changes to fit CI.
The deciding [Qt 6.4.2 implementation](https://raw.githubusercontent.com/qt/qtbase/v6.4.2/src/widgets/dialogs/qfiledialog.cpp)
is selectFile's visible/focused field condition. Final CI is reported with the
follow-up verification checkpoint.
