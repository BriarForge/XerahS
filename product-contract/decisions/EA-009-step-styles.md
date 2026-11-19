# EA-009: Step styles and reset semantics

Decision: EA-009-STEP-STYLES-2026-11-20, resolved by the loop under the
maintainer's delegated owner-block authority on 2026-11-20 (agent opencode,
GLM 5.3).
Contract: 0.4.3; EDITOR-ANNOTATIONS-001: 0.1.1. Requirements: EA-009.

## Problem

The SPEC requires Step to support numeric, alphabetic and Roman styles, an
explicit starting value, deterministic next-number allocation, reset, and
stable values after deletion, reorder, undo, redo and reload - but supplies
no mapping or reset vectors. The open questions recorded in shared loop
state were: does alphabetic 1 map to A; what does 0 mean; casing; rollover
after Z; Roman handling of 0 and values above 3999; does reset affect only
future allocation or existing objects; and how the explicit next starting
value interacts with existing larger values.

## Choice

- Alphabetic is bijective spreadsheet-style base 26: 1 -> A, 26 -> Z, and
  rollover continues 27 -> AA, 52 -> AZ, 703 -> AAA. Both casings exist
  (UppercaseLetter, LowercaseLetter). This is ShareX's own StepTypeFormatter
  (ShareX.ImageEditor/Core/Annotations/StepType.cs), matched case for case.
- Roman is greedy subtractive with the standard 13-symbol table (M, CM, D,
  CD, C, XC, L, XL, X, IX, V, IV, I), both casings, with no 3999 cap:
  3999 -> MMMCMXCIX, 4000 -> MMMM. This is ShareX's FormatRoman, matched
  symbol for symbol.
- A value of 0 (or negative) renders as plain decimal digits in every
  style - 0 -> "0", -5 -> "-5" - never empty and never letters or Roman.
  This is ShareX's `if (value <= 0) return value.ToString()` guard.
- Reset affects only future allocation. Existing steps keep their printed
  labels; the reset retires the existing sequence so the next allocated
  value is the explicit starting value again. This is Snagit's documented
  Restart sequence: "Click Restart sequence button to restart the
  lettering or numbering sequence for the next step added to the Canvas"
  (TechSmith Step Tool tutorial, techsmith.com/learn/tutorials/snagit/step).
- The explicit starting value is a floor for future allocation, bounded
  1..10: the next allocated value is max(highest non-retired step value +
  1, start). Existing larger values therefore force continuation (the
  sequence never goes backwards or duplicates), and after a reset the
  retired steps no longer hold the floor back.
- One deliberate, contract-mandated divergence from the ShareX reference
  is preserved: values stay stable after deletion, reorder, undo, redo and
  reload. ShareX's EditorCore renumbers later steps down on deletion
  (HandleStepRenumbering); the SPEC requires stable values instead, and
  the deterministic next-value rule above keeps new allocations collision
  free without renumbering anyone.

Sources (read 2026-11-20): the ShareX behavioural reference
/home/mike/Work/ShareX/ShareX/ShareX.ImageEditor/Core/Annotations/StepType.cs
(StepTypeFormatter.FormatAlphabetic, FormatRoman and the value <= 0 guard;
NumberAnnotation and EditorCore.NumberCounter for allocation) and
https://www.techsmith.com/learn/tutorials/snagit/step (Step Tool tutorial,
Restart Sequence section) plus
https://www.techsmith.com/learn/tutorials/snagit/step-capture (delete/add/
reorder renumbering applies to the captured guide view, a separate feature
from the canvas Step tool).

## Impact

The native candidate code and tests already implement exactly these
semantics (formatStepValue, nextStepValue, resetStepSequence in the image
editor; AnnotationShapesTest's step-style vectors, style-name round trips
and allocation/reset cases), so no runtime behavior changes. The decision
approves them and publishes the mapping and allocation as machine-readable
conformance vectors in EDITOR-ANNOTATIONS-001/test-vectors.json (the
capability's first published vectors; step-format and step-allocation
operations run the production helpers through the Linux conformance
adapter). EA-009 leaves blocked and is implemented candidate; EA-008's
separate Text-border geometry question remains open and is not decided
here. Release requires Mike; conformance before activation.
