# EDITOR-ANNOTATIONS-001 Annotation tools

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

## User intent

Users can mark up an image with geometric, text, asset, emphasis, redaction,
magnification, crop, and cut-out tools and can revise those objects without
flattening the source prematurely.

## Requirements

- **EA-001:** The editor MUST expose the 20 stable tool IDs in
  `annotation-tools.json`. A platform MAY group or present tools differently,
  but MUST NOT silently omit one; a degraded or unavailable tool requires an
  approved disposition.
- **EA-002:** Selecting a creation tool MUST show its active state, applicable
  options, pointer/keyboard instructions, and whether the next completed object
  keeps the tool active or returns to Select.
- **EA-003:** Select MUST support hit testing, single selection, additive or
  replacement multi-selection where offered, clear selection, delete, move,
  resize, and applicable rotation. Hidden or locked objects MUST NOT be mutated.
- **EA-004:** Creation gestures MUST normalize reverse drags, reject invalid or
  zero-area geometry, clip or explicitly allow out-of-canvas geometry by tool,
  and commit exactly one history operation when completed.
- **EA-005:** Rectangle and Ellipse MUST preserve bounds, stroke, fill, border
  style, thickness, corner radius where applicable, shadow, opacity, rotation,
  visibility, and z-order.
- **EA-006:** Line and Arrow MUST preserve ordered endpoints, optional curved
  control geometry, border style, thickness, arrow style, shadow, opacity, and
  direction when moved, resized, copied, serialized, or rendered.
- **EA-007:** Freehand MUST sample an ordered point path, simplify it only under
  a documented tolerance, retain intentional corners, and render the same
  stroke style at every zoom level and export scale.
- **EA-008:** Text and SpeechBalloon MUST support native text entry, explicit
  commit/cancel, multiline content, font family and size, bold/italic,
  horizontal alignment, text/border/fill colors, and balloon-tail state.
- **EA-009:** Step MUST support numeric, alphabetic, and Roman styles, an
  explicit starting value, deterministic next-number allocation, reset, tail
  state, and stable values after deletion, reorder, undo, redo, and reload.
- **EA-010:** Image, Emoji, and Cursor MUST embed or durably reference their
  selected pixels and metadata, preserve alpha and aspect ratio, reject unsafe
  or oversized assets before decode, and remain available after source removal.
- **EA-011:** Highlight, Blur, Pixelate, Magnify, and Spotlight MUST apply only
  inside their declared geometry, preserve strength and ellipse/blur options,
  clip safely at edges, and render deterministically from immutable source
  pixels rather than recursively sampling their own preview.
- **EA-012:** SmartEraser MUST replace its target area using a deterministic
  documented fill algorithm, preserve its committed result through history,
  and MUST NOT imply content-aware removal when that capability is unavailable.
- **EA-013:** Crop MUST produce an explicit retained canvas region. CutOut MUST
  remove the declared horizontal or vertical span and translate remaining
  pixels and annotations according to a deterministic coordinate rule.
- **EA-014:** A tool's new object MUST snapshot current defaults. Editing the
  defaults MUST NOT retroactively alter existing objects; editing a selected
  object's style MUST be an undoable document mutation.
- **EA-015:** Ordering commands MUST provide Bring Forward, Send Backward,
  Bring to Front, and Send to Back with deterministic behavior for single and
  multiple selections and MUST preserve relative order within a selection.
- **EA-016:** Cut, Copy, Paste, and Duplicate MUST preserve supported object
  types and embedded assets, assign new stable IDs on insertion, offset pasted
  objects visibly, and reject malformed or untrusted clipboard payloads.
- **EA-017:** Create, edit, delete, paste, duplicate, order, style, crop, and
  cut-out mutations MUST participate in the session history and dirty-state
  rules in `EDITOR-SESSION-001`.
- **EA-018:** Every tool and option MUST be operable through accessible native
  controls. Selection, handles, redaction regions, and focus MUST have
  non-color-only cues; keyboard users MUST be able to cancel a live gesture.
- **EA-019:** Re-editable persistence MUST preserve every supported annotation
  field, stable ID, order, embedded asset, and unknown compatible field.
  Unknown types MUST remain round-trippable and MUST NOT execute code.
- **EA-020:** Implementations MUST pass every vector and scenario in this
  package, enforce finite coordinates and resource limits, avoid telemetry or
  uploads without consent, and provide typed failures without partial mutation.

## Native adaptation and platforms

Windows, macOS, and Linux MAY use native gestures, menus, pickers, cursors,
fonts, and accessibility APIs. The document model, history boundaries, tool
availability, failure outcomes, and exported pixels remain equivalent.

## Baseline traceability and disposition

The pinned candidate exposes 20 `EditorTool` members and 19 concrete annotation
types (Select is a mode; Step maps to the numbered annotation model). The approved
proposes preserving all tool outcomes while correcting ambiguous error,
security, persistence, and accessibility behavior. Approval MUST evaluate each
row in `annotation-tools.json`; the reference source is evidence, not authority.
