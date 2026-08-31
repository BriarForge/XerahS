# INTERFACE-CATALOG-001 Native and agent interfaces

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

## Requirements

- **IFCAT-001:** Every interface-ledger row linked to this package MUST retain
  its stable ledger ID and receive an approved surface ID, semantic operation,
  request and result shape, authority, errors, accessibility behavior, and
  Windows, macOS, and Linux disposition before activation.
- **IFCAT-002:** GUI, CLI, MCP, and provider surfaces MUST invoke shared product
  operations rather than reimplement product behavior. Equivalent requests MUST
  produce equivalent domain results, side effects, and redacted errors.
- **IFCAT-003:** GUI surfaces MUST define initial, loading, populated, empty,
  validation-error, permission-denied, dependency-unavailable, partial-success,
  and terminal-failure states where applicable, without relying on color alone.
- **IFCAT-004:** Every GUI surface MUST support complete keyboard navigation,
  visible focus, logical reading order, accessible names and descriptions,
  screen-reader state changes, text scaling, high contrast, reduced motion, and
  platform-native input conventions.
- **IFCAT-005:** Modal dialogs MUST trap focus only while active, identify their
  purpose, expose safe default and cancel actions, restore focus on close, and
  MUST NOT hide destructive consequences or permission prompts behind generic
  confirmation text.
- **IFCAT-006:** Long-running surfaces MUST expose progress when knowable,
  indeterminate state otherwise, cooperative cancellation, background behavior,
  and a terminal result that remains available after transient UI closes.
- **IFCAT-007:** Layout MUST remain operable under supported scaling,
  localization expansion, right-to-left text where supported, reduced window
  size, display topology change, and native font substitution. Essential
  controls MUST NOT become unreachable.
- **IFCAT-008:** CLI commands MUST have stable names, argument and environment
  precedence, help, stdout and stderr ownership, exit codes, signal handling,
  non-interactive behavior, and a versioned machine-readable output mode.
- **IFCAT-009:** CLI commands MUST NOT prompt when non-interactive mode is
  selected. Missing consent, credentials, permission, or destructive approval
  MUST return a distinct actionable result without performing the operation.
- **IFCAT-010:** CLI output containing secrets, private paths, capture content,
  account data, or tokens MUST be redacted by default. Verbose and diagnostic
  modes MUST NOT weaken redaction.
- **IFCAT-011:** MCP tools MUST publish versioned input and output schemas,
  precise side-effect and authority descriptions, bounded results, progress and
  cancellation behavior, stable error codes, and explicit destructive-action
  or external-publication annotations.
- **IFCAT-012:** MCP execution MUST apply least authority, distinguish agent
  request from human consent, reject authority hidden in free-form text, and
  require human review for credential changes, external publication,
  destructive operations, plugin installation, or policy-defined sensitive
  capture and history access.
- **IFCAT-013:** MCP capture, history, settings, annotation, and upload surfaces
  MUST minimize returned private data, support metadata-only discovery where
  useful, redact secrets, and never expose raw local content beyond the granted
  scope and result lifetime.
- **IFCAT-014:** Destination-provider surfaces MUST use a versioned field schema
  with stable field IDs, types, validation, secret classification, help,
  conditional visibility, and authentication actions. Presentation MUST NOT be
  derived by executing provider-controlled code in the trusted UI process.
- **IFCAT-015:** Provider connection tests and authentication MUST be explicit,
  cancellable network actions. Ordinary configuration viewing and validation
  MUST NOT transmit credentials or content.
- **IFCAT-016:** Interface errors MUST map stable domain error codes to native
  presentation and MUST preserve correlation IDs without exposing stack traces,
  secrets, or private payloads. Retrying MUST be offered only when safe.
- **IFCAT-017:** Interface availability MUST follow capability and platform
  disposition. An unavailable native integration MUST expose the approved
  equivalent, degraded explanation, or explicit absence; a visible but inert
  control is not an acceptable disposition.
- **IFCAT-018:** Activation MUST include semantic equivalence tests across entry
  points, full keyboard and screen-reader review, scaling and localization,
  malformed input, permission and credential denial, cancellation, redaction,
  CLI snapshot and exit-code tests, MCP schema and authority tests, provider
  field fixtures, and all three native platform adapters.

## Baseline traceability and disposition

The interface parity ledger supplies the exhaustive discovery index. Dedicated
ImageEditor GUI surfaces remain linked to their narrower editor packages.
Linking a surface here records contract ownership; it does not require native
applications to reproduce Avalonia layout or command structure.
