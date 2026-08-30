# WORKFLOW-CATALOG-001 Workflow and action catalog

Version: 0.1.0

Status: Draft; product-owner approval required

## Requirements

- **WFCAT-001:** Every workflow-ledger row linked to this package MUST retain its
  stable ledger ID and receive an approved action ID, purpose, input schema,
  output schema, side-effect class, authority, cancellation points, failure
  behavior, and Windows, macOS, and Linux disposition before approval.
- **WFCAT-002:** A workflow invocation MUST create one traceable execution with a
  stable ID, start time, origin, effective settings snapshot, ordered step
  results, terminal state, and redacted diagnostics.
- **WFCAT-003:** GUI, hotkey, tray, CLI, MCP, watch-folder, and automation
  invocations of the same action MUST use the same semantic request and result.
  Entry points MAY add presentation but MUST NOT invent different behavior.
- **WFCAT-004:** The engine MUST distinguish success, partial success, skipped,
  cancelled, permission denied, validation failure, unavailable dependency,
  transient failure, and permanent failure. A step MUST NOT report success
  before its promised durable side effects complete.
- **WFCAT-005:** Ordered post-capture and post-upload actions MUST follow the
  approved ordering graph. A failure MUST apply the action's declared continue,
  stop, retry, compensate, or request-user policy and preserve prior results.
- **WFCAT-006:** Cancellation MUST be cooperative, idempotent, and observable.
  It MUST stop new side effects, await or safely detach non-cancellable native
  work, retain completed outputs, and report resources that require cleanup.
- **WFCAT-007:** Capture and recording workflows MUST request platform permission
  before acquisition, expose cancellation, and preserve the accepted capture
  or recording capability's monitor, DPI, cursor, audio, and degraded-mode rules.
- **WFCAT-008:** Clipboard workflows MUST validate available formats, avoid
  replacing clipboard content until the promised result is ready, preserve
  privacy classification, and return an explicit unsupported-content result.
- **WFCAT-009:** File, folder, save, delete, index, external-program, and explorer
  actions MUST resolve canonical paths, prevent unintended traversal, handle
  collisions by approved policy, and require confirmation for destructive or
  authority-expanding operations.
- **WFCAT-010:** Upload and URL actions MUST use the approved destination and
  credential authority, report progress and remote results, avoid leaking
  secrets, and distinguish local success from remote publication success.
- **WFCAT-011:** Image, OCR, QR, hash, color, combine, split, thumbnail, effect,
  and editor actions MUST declare accepted input media, output representation,
  metadata policy, deterministic parameters, and failure behavior.
- **WFCAT-012:** Recording control actions MUST target an explicit current
  session, be idempotent where safe, reject invalid state transitions, and
  preserve recoverable output on stop, abort, or encoder failure as specified
  by the recording contract.
- **WFCAT-013:** Application-control actions such as opening views, disabling
  hotkeys, or exiting MUST respect current modal work, unsaved state, agent
  authority, and destructive confirmation. Headless entry points MUST return an
  actionable result when presentation is required.
- **WFCAT-014:** Concurrent invocations MUST declare whether they serialize,
  coalesce, reject, or execute independently. Shared clipboard, capture,
  recording, file, history, and credential resources MUST have deterministic
  arbitration with no silent last-writer-wins behavior.
- **WFCAT-015:** Retry MUST be bounded, cancellable, and restricted to operations
  declared idempotent or protected by an idempotency key. The execution record
  MUST distinguish attempts without duplicating user-visible results.
- **WFCAT-016:** Workflow definitions and presets MUST be versioned, use stable
  action IDs, validate completely before execution, retain compatible unknown
  fields, and produce an explicit migration or unsupported-action result.
- **WFCAT-017:** Logs, history, notifications, and progress surfaces MUST expose
  the same terminal state and output references while redacting secrets and
  private content. Notifications MUST NOT claim completion for partial failure.
- **WFCAT-018:** Approval MUST include deterministic scenarios for every linked
  action family, ordering and failure matrices, permission denial, cancellation
  at each side-effect boundary, concurrent invocation, retry/idempotency,
  malformed definitions, and execution on all supported platform adapters.

## Baseline traceability and disposition

The workflow parity ledger supplies the exhaustive pinned-baseline discovery
IDs. ImageEditor commands, tools, operations, and effects remain governed by
their dedicated packages. Linking here establishes a draft owner and does not
make enum membership or current execution ordering normative by itself.
