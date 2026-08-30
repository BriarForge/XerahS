# EDITOR-OUTPUT-ACTIONS-001 Output and task actions

Version: 0.1.0

Status: Draft; product-owner approval required

## Requirements

- **EO-001:** Every output action MUST render from one immutable document
  snapshot and MUST declare whether annotations and backgrounds are included.
  A concurrent edit MUST NOT produce a mixed or partially rendered result.
- **EO-002:** Copy Image MUST place an interoperable native image representation
  on the clipboard, preserve alpha where supported, disclose any format loss,
  and MUST NOT clear dirty state or close the editor.
- **EO-003:** Save MUST use the current durable path only after prior path
  validation. Save As MUST use a native accessible picker and MUST NOT change
  the current path until a complete successful write.
- **EO-004:** User-facing raster Save As MUST offer PNG, JPEG, and WebP with
  explicit alpha, quality, extension, and overwrite behavior as recorded in
  `format-disposition.json`. AVIF, GIF, and BMP MUST NOT be claimed solely
  because a helper or changelog mentions an encoder.
- **EO-005:** Saves MUST use a same-directory temporary file and atomic replace
  where supported, preserve the previous destination on encode/write failure,
  and report destination, format, and recoverability without exposing private
  path data in telemetry.
- **EO-006:** Raster and `.xann` sidecar persistence MUST follow the success,
  mismatch, dirty-state, and partial-failure rules in `EDITOR-SESSION-001`.
- **EO-007:** Upload MUST require an explicit host service and user action,
  render one snapshot, expose destination and privacy implications, support
  cancellation, and return a typed result. Failure MUST NOT close the session.
- **EO-008:** Print MUST use a native print flow, expose printer, page, scale,
  orientation, color, and cancellation, and MUST NOT mutate or save the document.
- **EO-009:** Pin to Screen MUST hand one owned snapshot to an approved native
  pin service. Closing, moving, or scaling the pinned view MUST NOT affect the
  editor document, and unsupported platforms require an approved disposition.
- **EO-010:** Set as Wallpaper MUST show the target display/layout choice,
  obtain confirmation, use a platform service, retain or disclose generated
  files, and return a typed success/failure result without changing the document.
- **EO-011:** Continue in task mode MUST return an owned rendered result and the
  `Continue` outcome exactly once. Auto-copy and auto-close preferences MUST run
  only after the result is ready and MUST report partial failures distinctly.
- **EO-012:** Cancel in task mode MUST return the `Cancel` outcome without a
  rendered result or external side effect. If the document is dirty, discard
  confirmation MUST follow the session close policy.
- **EO-013:** Exit in standalone mode MUST distinguish Save, Discard, and Cancel,
  keep the editor open on cancelled or failed Save, and MUST NOT treat a host
  callback return as success unless its success contract is satisfied.
- **EO-014:** Host callbacks for copy, save, save as, print, pin, upload, task
  completion, and close MUST declare ownership, thread, cancellation, lifetime,
  error, and reentrancy semantics. Missing callbacks MUST disable or explain the
  affected action rather than silently doing nothing.
- **EO-015:** Output actions MUST expose accessible names, shortcuts, busy and
  disabled state, progress, completion, failure, and retry. A shortcut MUST have
  the same confirmation and security behavior as its visible action.
- **EO-016:** Conformance MUST cover success, cancellation, permission denial,
  unsupported service, encoder failure, disk-full, overwrite refusal, callback
  exception, and raster/sidecar split outcomes on every supported platform.

## Baseline discrepancy requiring approval

The pinned code has encoders/helpers for more formats than its visible Save As
surface. The ImageEditor changelog claims AVIF saving, while observed static UI
evidence identifies PNG, JPEG, and WebP as the exposed editor choices. This
draft preserves the user-reachable set and records other formats as unresolved
instead of promoting documentation or dormant code into product behavior.
