# ESET-001 editor settings ledger dispositions (decided 2026-11-23)

Decision ID: ESET-001-EDITOR-LEDGER-DISPOSITIONS-2026-11-23
Requirements: EDITOR-SETTINGS-001 ESET-001 (every persisted ImageEditor option
receives a stable contract name, type, default, validation, migration, privacy
classification, platform disposition and downstream-effect description),
ESET-015 (settings conformance exercises the vectors, every field's default
and invalid boundary, upgrades, unknown-field round-trip, atomic failure and
all three platform adapters), with SETCAT-001/002/020 machinery shared by
SETTINGS-CATALOG-001.
Decided by: the build loop under the maintainer's delegated owner-block
authority. No SPEC text change; the MUSTs stand and this record settles the
per-field ledger semantics/defaults the rows named as missing.

## Question

The 100 settings-ledger rows linked to EDITOR-SETTINGS-001 were only
`inventoried`: each carried its pinned baseline declaration but no approved
semantic name, value type, default, validation, scope, privacy class,
migration rule, downstream effect or platform disposition, and SETCAT-002
forbids treating source initializers as normative. Which dispositions govern
these rows, which defaults are normative now, and what machinery proves the
ledger is actually covered?

## Research

- The behavioural reference read directly (ShareX cross-platform,
  `ShareX.ImageEditor/Integration/ImageEditorOptions.cs`): every default and
  enum member the ledger's declarations point at - Theme Dark, window
  1280x720 maximized, Thickness 4, Blur 30, Pixelate 20, Magnifier 2,
  Spotlight 30/blur 0, background margin 80 / padding 40 / corner 20 /
  shadow 30 / smart padding on, Text bold on / italic off / Center,
  TextThickness 8, balloon and step per-type thickness 4, tails on,
  HighlightFillColor Colors.Yellow, per-type colours from PrimaryColor
  #FFF23C3C and SecondaryColor #FFFAFAFA, QuickCrop on,
  ShowInsertImageDialog on, ShowNotifications on. `Core/Annotations/Base/
  Annotation.cs` pins the shadow constants #80000000 / 4 / 1 / 3 / 3.
- The pinned census declarations (`product-contract/parity/
  settings-ledger.yaml`, baseline kova-0.29.0) agree with the reference
  line for line; the reference usage sites define the draft rows' meaning
  (QuickCrop applies a finished crop selection immediately in
  `EditorInputController.cs`; ShowNotifications gates editor toasts in
  `MainViewModel.Notifications.cs`; ShowInsertImageDialog shows the image
  placement dialog before commit in `EditorView.ImageInsert.cs`).
- The recorded decisions that already settled this edition's deliberate
  divergences: EA-005 (CornerRadius 0 - the undecorated rectangle; the
  reference 4 would change the shipped look), EA-008 (the Text border is
  the text-box outline), EA-009 (StepType mapping and reset semantics;
  the decided default Numeric with well-formed unknown styles preserved
  inert), EC-013 (background composition: margin 80, padding 40, corner 20,
  shadow 30, transparent default, solid #FF222222, gradient #FFDD2476 ->
  #FFFF512F, smart padding per the reference's own rule), EU-009/010/011
  (background remover: device Auto with honest typed refusal, model supply
  verify-then-adopt), and the ES rectangle qualification red that this
  edition ships (PREFERENCES-POLICY.md documents #FFFF0000 border and
  #00000000 fill against the reference's PrimaryColor and #00FFFFFF
  transparent, both visually equivalent or shipped-look decisions).
- Exit confirmation (the one genuinely open adjudication):
  - Cloudscape Design System, "Communicating unsaved changes" (fetched
    2026-11-23, cloudscape.design/patterns/general/unsaved-changes):
    "Confirm only when necessary... it's not necessary if a user hasn't
    made any changes... Don't launch a confirmation modal when there is no
    risk of data loss."
  - Flameshot issue #3749 and PR #3758 (fetched 2026-11-23): Flameshot's
    "ask for confirmation when you press Esc" shipped as an opt-in setting,
    not a default.
  - ShareX issue #8435 (fetched 2026-11-23): reference users asking for the
    editor close confirmation to be skippable because it gets in the way of
    multi-window workflows.

## Decision

Every one of the 100 rows now carries a machine-readable disposition record
in `capabilities/EDITOR-SETTINGS-001/setting-dispositions.json`, validated
against the pinned `setting-disposition-v1` schema:

- **58 approved records** - fields the native edition implements with
  automated acceptance evidence. Their defaults are normative now per
  SETCAT-002: the user outcome, boundary behavior and Linux platform result
  are exercised through the published conformance vectors and the native
  store suites; Windows and macOS keep the Required disposition in their own
  adapters. Approved defaults follow the reference unless a recorded
  decision diverges (CornerRadius 0, empty font families, font size 24,
  StepFontSize 0, red border, transparent black fill), and each divergence
  names its reason in the record.
- **42 draft records** - semantics and defaults are decided here (mostly
  per-type creation defaults the native model does not persist yet:
  per-type colours, thicknesses, tails, Highlight fill, background
  composition knobs, background remover selection, QuickCrop and the
  insert-image/notification switches), but SETCAT-002 keeps their defaults
  non-normative until acceptance evidence exists; they carry no vector and
  count against activation.
- The alias rows (`AccentColor`, `BorderColor`, `FillColor`, `ShadowColor`
  and their per-type twins) are `runtime-only`: derived conveniences that
  are never loaded or saved; only their `Hex` authorities persist, and a
  stored alias round-trips without overriding.
- **ShowExitConfirmation is approved at false**, diverging from the
  reference's true: the native close path always offers Save, Discard and
  Cancel when work is unsaved (the ESET-014 destructive gate), the extra
  prompt guards only a clean exit with nothing at stake, and the fetched
  guidance plus Flameshot's opt-in-only confirmation support leaving it off
  by default.
- `tools/setting-dispositions/check.py` is the shared fixture machinery for
  SETCAT-020: it proves the schema validity of every record, the exact
  bijection with the ledger rows linked to the capability, and that every
  value family with approved records (runtime-only exempt) has a
  `defaults-<family>` and an `invalid-<family>` vector whose expected values
  equal the decided defaults, and it emits the generated coverage report
  (approved/draft counts, unexercised approved fields, activation gate).
  Fourteen new EDITOR-SETTINGS-001 vectors (7 approved families x defaults
  + invalid boundary) run through the existing `load-settings` conformance
  operation, so the contract defaults are executed against production, not
  merely written down.

## Impact

ESET-001's named gap (accepted per-field metadata) is settled for the whole
package: the 100/100 bijection is machine-checked in ctest, 58 defaults are
normative with executable Linux evidence, and the 42 remaining rows carry
decided, source-backed drafts instead of silence. Activation remains gated:
ESET-015 still needs upgrade/atomic-failure conformance fixtures and the
Windows/macOS adapters, and the report lists the approved fields no vector
exercises yet (the toolbar item entry fields and the effects-list fields,
whose native stores live behind their own seams - native
`ToolbarPreferences` and `EffectsStore` tests cover them today). The
822 SETTINGS-CATALOG-001 rows keep the same machinery for their own
family-by-family dispositions; no contract MUST changed, so the SPEC text
is untouched and EDITOR-SETTINGS-001 moves to 0.1.1 for the published
artifacts, contract 0.4.6.
