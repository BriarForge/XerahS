# EDITOR-EFFECTS-001 Image effects

Version: 0.1.0

Status: Approved by the human product owner on 2026-08-31; conformance required before activation

## User intent

Users can find, configure, preview, apply, cancel, favorite, and repeat the full
set of supported adjustments, drawings, filters, and image manipulations
without damaging the current document when an operation fails or is cancelled.

## Requirements

- **EE-001:** The approved effect set MUST be the 232 stable IDs selected by
  `effect-catalog-selection.json` from the pinned inventory: 32 adjustments,
  16 drawings, 149 filters, and 35 manipulations. Approval MUST review the ID
  set and any correction, omission, or native substitution explicitly.
- **EE-002:** Effect IDs MUST remain stable across display-name, localization,
  icon, menu, or implementation changes. The browser MUST support category,
  search, recent, and favorite discovery without duplicating an ID.
- **EE-003:** Every parameter MUST have a stable ID, type, default, allowed
  range or choices, validation rule, accessibility label, and serialization
  form. Invalid, missing, and unknown values MUST produce defined fallback or
  typed failure behavior and MUST NOT be silently clamped unless specified.
- **EE-004:** A previewable effect MUST preserve the pre-preview document,
  update a cancellable non-authoritative preview, and commit exactly one
  history operation only after Apply. Cancel MUST restore the exact prior state.
- **EE-005:** An immediate effect MUST either be safely reversible through one
  history entry or obtain explicit confirmation before an irreversible external
  action. Failure MUST leave pixels, annotations, history, and dirty state as
  they were before invocation.
- **EE-006:** Effects that change image or canvas dimensions MUST use the
  coordinate, annotation, and history rules in `EDITOR-CANVAS-001`; their
  browser entry MUST NOT bypass those rules.
- **EE-007:** Effects requiring images, fonts, maps, masks, models, or other
  assets MUST validate type and size before decode, surface missing-asset
  failures, and embed or durably reference assets required for reproducibility.
- **EE-008:** Random or procedural effects MUST expose or derive a persisted
  seed. Identical normalized input, parameters, seed, renderer version, and
  color profile MUST produce identical decoded pixels on all platforms.
- **EE-009:** Effect execution MUST define alpha, premultiplication, color-space,
  orientation, edge sampling, and clipping behavior and MUST avoid unintended
  conversion to an opaque or lower-fidelity image.
- **EE-010:** Expensive effects MUST expose progress and cancellation, honor
  memory and dimension limits, release abandoned previews, and MUST NOT publish
  a partial result after cancellation or resource exhaustion.
- **EE-011:** Applying a new effect after undo MUST clear the redo branch. A
  preview MUST NOT enter history, dirty the document, or alter recent-effects
  ordering until it commits successfully.
- **EE-012:** Recent and favorite lists MUST contain stable effect IDs, reject
  duplicates, retain deterministic ordering, tolerate unknown retired IDs, and
  remain user-editable without changing the effect catalog.
- **EE-013:** Search and categories MUST be keyboard and screen-reader operable;
  parameter controls MUST expose names, values, units, ranges, errors, and
  reset-to-default actions using native accessibility APIs.
- **EE-014:** The UI MAY use accelerated or lower-resolution previews, but Apply
  and export MUST use the authoritative renderer. Preview differences MUST be
  bounded and disclosed for effects where exact live preview is impractical.
- **EE-015:** Unknown compatible effect IDs and parameters in imported presets
  MUST be retained for round-trip and shown as unsupported; they MUST NOT invoke
  reflection, scripts, network access, or arbitrary constructors.
- **EE-016:** Effects MUST operate locally unless a separately approved service
  contract explicitly requires network use. User pixels, parameters, asset
  paths, and previews MUST NOT leave the device without explicit user action.
- **EE-017:** Conformance MUST enumerate every selected ID and prove it is
  constructible, uniquely identified, categorized, parameter-validatable, and
  either previewable or immediate as declared. Representative golden images
  MUST cover every algorithm family and all boundary-value parameter classes.
- **EE-018:** Unsupported hardware, codecs, fonts, or accelerators MUST result in
  a deterministic CPU/native fallback or an approved platform disposition with
  a user-visible diagnostic; silent omission from the browser is prohibited.

## Baseline traceability and disposition

The source candidate is reflection-discovered, so file names alone are not a
complete registry. The deterministic census resolves concrete subclasses,
stable IDs, categories, execution modes, and 963 declared parameter controls at
the pinned commit. It also establishes that 219 effects are previewable and 13
are immediate. These observations are discovery evidence only; this package
proposes preserving their outcomes with stronger validation and determinism.
