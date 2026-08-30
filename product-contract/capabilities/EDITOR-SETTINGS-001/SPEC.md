# EDITOR-SETTINGS-001 Editor settings

Version: 0.1.0

Status: Draft; product-owner approval required

## Requirements

- **ESET-001:** Every persisted ImageEditor option in the settings ledger MUST
  receive a stable contract name, type, default, validation, migration,
  privacy classification, supported-platform disposition, and downstream-effect
  description before this package can be approved.
- **ESET-002:** Loading MUST distinguish missing, valid, unknown, invalid, and
  newer-version fields. Invalid values MUST fall back per field with a visible
  diagnostic or recovery record and MUST NOT reset unrelated valid settings.
- **ESET-003:** Saving MUST be atomic, retain compatible unknown fields, avoid
  writing computed/color convenience properties marked non-persistent, and keep
  a recoverable previous version according to the configuration policy.
- **ESET-004:** Theme, system-theme, accent, and system-accent preferences MUST
  resolve predictably on all platforms and MUST preserve contrast, focus,
  high-contrast, reduced-motion, and screen-reader requirements.
- **ESET-005:** Remembered window size and maximized state MUST be clamped to an
  available display, adapt to changed display topology or scaling, and MUST NOT
  strand the window off screen. Disabling memory MUST stop persistence.
- **ESET-006:** Last tool, shared border/fill/thickness/corner/border/arrow/cursor
  choices, shadow fields, and all text, balloon, step, emphasis, and redaction
  defaults MUST affect only subsequently created objects unless the user edits a
  current selection explicitly.
- **ESET-007:** Colors MUST persist in an alpha-preserving, locale-independent
  canonical representation. Invalid colors MUST use the field default and emit
  a field-specific recovery diagnostic.
- **ESET-008:** Font family, size, weight, italic, alignment, and step-style
  preferences MUST validate against usable native fonts and ranges. Missing
  fonts MUST use an accessible fallback without rewriting the user's preference
  until they explicitly save a replacement.
- **ESET-009:** Blur, pixelate, magnifier, spotlight, background geometry, shadow,
  and related numeric values MUST reject non-finite input and enforce documented
  inclusive ranges consistently in UI, config import, and automation.
- **ESET-010:** Toolbar items MUST use stable action IDs, visibility, group, and
  hotkey fields; validation MUST reject duplicate action placements where not
  supported, invalid hotkeys, unsafe reserved gestures, and unknown actions
  while retaining unknown compatible entries for round-trip.
- **ESET-011:** Toolbar customization MUST support reorder, show/hide, grouping,
  hotkey assignment, reset to defaults, Cancel without persistence, and Apply as
  one atomic preference change. Essential escape/recovery actions MUST remain
  reachable even if hidden from the toolbar.
- **ESET-012:** Recent image paths MUST be opt-in private local data, de-duplicated
  using platform path rules, bounded by a validated maximum, removable singly or
  in full, resilient to missing files, and excluded from telemetry/support data.
- **ESET-013:** Recent and favorite effects MUST persist stable effect IDs,
  retain order, remove duplicates, tolerate retired IDs, and initialize defaults
  without re-adding an effect the user intentionally removed.
- **ESET-014:** Auto-copy, auto-close, quick-crop, insert-dialog, notification,
  zoom-to-fit, and exit-confirmation preferences MUST never bypass required
  consent, destructive confirmation, permissions, validation, or error reporting.
- **ESET-015:** Settings conformance MUST exercise the vectors in
  `test-vectors.json`, every field's default and invalid boundary, upgrades from
  supported versions, unknown-field round-trip, atomic failure, and all three
  platform adapters before approval.
- **ESET-016:** The actions and precedence in `shortcut-catalog.json` MUST remain
  available through native-equivalent gestures and visible command surfaces.
  macOS MAY substitute Command for Control. Text entry and modal dialogs MUST
  take precedence over editor shortcuts, and user-defined tool hotkeys MUST be
  validated for collisions with protected actions.

## Baseline traceability and disposition

The deterministic census now includes `ImageEditorOptions`, toolbar item
options, and background-remover options rather than scanning only the parent
application. Source declarations are candidate evidence; their semantic
defaults and validation remain review work item-by-item in the settings ledger.
