# Native editor preference storage

This is implementation evidence for ESET-001/002/003/004/005/006/007/009/012/015, not activation
of EDITOR-SETTINGS-001, approval of the complete settings ledger, or a release
privacy/security review. The production Qt Core model and store are shared
with the conformance adapter. The native app initializes the store only after
winning its instance socket, before any editor opens. Field recovery and
read-only fallback appear in native startup warnings without private values.

## Fields and disposition

All fields below are editor-global, local to the native Linux edition. The
table describes implemented validation and proposed native metadata. Source
initializer evidence alone does not approve shared defaults. Blur's fallback
30 is independently specified by the published invalid-field vector.

| Stable field | Type and default | Inclusive validation | Privacy and downstream effect | Ledger linkage and Linux evidence |
|---|---|---|---|---|
| `schemaVersion` | Integer, 1 | Exactly 1 for writes; absence loads the version-1 defaults; unsupported versions are read-only | Public format metadata; gates interpretation before mutation | Native envelope/model version; malformed/newer preservation tests |
| `Theme` | Enum string, `Dark` | `Dark`, `Light`, `System` | Non-secret appearance preference; applies when system theme is disabled, with compatible System behavior | `SET-IMAGE-EDITOR-OPTIONS-THEME-001`; core/native resolution, controls, live changes and restart tests |
| `UseSystemTheme` | Boolean, true (candidate census default) | Boolean only | Non-secret appearance preference; follows desktop appearance while retaining the manual theme | `SET-IMAGE-EDITOR-OPTIONS-USE-SYSTEM-THEME-001`; core validation and native resolution/control/portal tests |
| `UseSystemAccentColor` | Boolean, true (candidate census default) | Boolean only | Non-secret appearance preference; follows the desktop accent while retaining the saved manual colour | `SET-IMAGE-EDITOR-OPTIONS-USE-SYSTEM-ACCENT-COLOR-001`; core validation and native resolution/control/portal tests |
| `AccentColorHex` | String, `#FF3E83F2` (candidate census default) | Same alpha-preserving canonical validation as drawing colours | Non-secret appearance preference; display compositing/contrast adjustment never rewrites the saved colour | `SET-IMAGE-EDITOR-OPTIONS-ACCENT-COLOR-HEX-001`; core/native alpha, invalid-field, picker, round-trip and contrast tests |
| `BlurStrength` | Finite number, 30 | 0..100; fractional values allowed | Non-secret preference; future blur creation defaults remain Required | `SET-IMAGE-EDITOR-OPTIONS-BLUR-STRENGTH-001`; published isolation vector and independent boundaries; blur tool pending |
| `Thickness` | Integer, 4 | 1..10000, within the production editor width limit | Non-secret drawing default; affects new objects, without editing existing objects | `SET-IMAGE-EDITOR-OPTIONS-THICKNESS-001`; core and native validation/isolation/restart/history tests |
| `BorderColorHex` | String, `#FFFF0000` | Exactly `#RRGGBB` or `#AARRGGBB`, hexadecimal digits; canonical uppercase `#AARRGGBB` | Non-secret drawing default; alpha, including transparent RGB, survives; applies to new objects | `SET-IMAGE-EDITOR-OPTIONS-BORDER-COLOR-HEX-001`; core alpha/invalid/restart tests; default agrees with the ES rectangle qualification default; native Apply/Cancel, picker and persistence tests |
| `FillColorHex` | String, `#00000000` | Same canonical colour validation as border | Non-secret drawing default; applies to new objects | `SET-IMAGE-EDITOR-OPTIONS-FILL-COLOR-HEX-001`; core alpha/invalid/restart tests; default agrees with the ES rectangle qualification default; native Apply/Cancel, picker and persistence tests |
| `RememberWindowState` | Boolean, true (candidate census default) | Boolean only | Local layout preference; false stops memory and excludes size/maximized state from both versions | `SET-IMAGE-EDITOR-OPTIONS-REMEMBER-WINDOW-STATE-001`; core/native opt-out, failure and previous-record recovery tests |
| `WindowWidth` | Finite number, 1280 (candidate census default) | 1..16777215 logical pixels; fractional input is supported and floored for integer widget geometry | Local layout preference; normal client width, constrained to current work area | `SET-IMAGE-EDITOR-OPTIONS-WINDOW-WIDTH-001`; core bounds/isolation and native restore/close/display tests |
| `WindowHeight` | Finite number, 720 (candidate census default) | Same range/units as width | Local layout preference; normal client height, constrained to current work area | `SET-IMAGE-EDITOR-OPTIONS-WINDOW-HEIGHT-001`; core bounds/isolation and native restore/close/display tests |
| `IsWindowMaximized` | Boolean, true (candidate census default) | Boolean only | Local layout preference; native maximized state is separate from normal size | `SET-IMAGE-EDITOR-OPTIONS-IS-WINDOW-MAXIMIZED-001`; core type/isolation and native maximize/minimize/close/restart tests |
| `RememberRecentImagePaths` | Boolean, false | Boolean only; false ignores and removes retained paths | Private local consent; only later successful Open/Save may add paths; no backfill | Native opt-in authority under ESET-012, absent from the pinned legacy field census; core/native runtime tests |
| `MaxRecentImageFiles` | Integer, 10 | 1..100 | Private local retention policy; decreasing it removes oldest entries | `SET-IMAGE-EDITOR-OPTIONS-MAX-RECENT-IMAGE-FILES-001`; bounded model and preference tests |
| `RecentImageFiles` | Array of strings, empty | At most 100 encoded entries and at most the validated maximum distinct absolute paths; valid platform path syntax | Private local data; newest first, deduplicated, removable individually or together; absent resources stay removable | `SET-IMAGE-EDITOR-OPTIONS-RECENT-IMAGE-FILES-001`; path/opt-in/isolation/removal/recovery tests |
| `ToolbarItems` | Array of action placements, native candidate layout on absence | At most 256 distinct action IDs; typed visibility/group fields and validated single-chord additional hotkeys | Non-secret global editor layout/input preference; explicit empty list stays empty | `SET-IMAGE-EDITOR-OPTIONS-TOOLBAR-ITEMS-001` and the four `SET-IMAGE-EDITOR-TOOLBAR-ITEM-OPTIONS-*` rows; production core validation, preservation and restart tests |

`BorderColor`, `FillColor` and `AccentColor` are derived convenience aliases, classified
`NonPersistent` and excluded from both saved versions. Only their canonical
`Hex` fields are authorities. A stored alias cannot override the corresponding
field; compatible unknown colour fields still round-trip. Colour names, short
hex, whitespace, incorrect lengths/types and malformed digits recover only the
affected field and emit its `invalid-…ColorHex` diagnostic.
Opaque six-digit input receives explicit `FF` alpha during normalization; no
locale, native colour name, or floating-point conversion participates.

The production session has an explicit creation-style operation separate from
selection editing. Updating creation defaults leaves existing objects, source,
selection, checkpoint, undo and redo untouched. Later creations snapshot the
new style; explicit selection edits retain their existing atomic history path.

Windows and macOS field dispositions remain Required in their own adapters.
Injecting Windows path rules in a Linux core test does not qualify Windows.
Every other ImageEditor ledger field, its semantic metadata, migrations and
downstream behavior remain Required. This small table does not replace that
full inventory. Computed/color convenience properties have no approved native
persisted mapping yet; the store does not serialize a baseline object graph.
Beyond border/fill, their explicit non-persistent dispositions remain part of
full qualification.

## Loading and compatible changes

Missing fields use their own defaults. Known valid fields survive an unrelated
invalid field. Invalid fields use their default and add a value-free
`invalid-<field>` recovery record. Compatible unknown fields and nested values
round-trip unchanged. Newer or malformed schema versions are read-only and
cannot be silently overwritten with version 1. The only automatic upgrade is
adding the missing version-1 field marker within this native format. No
Avalonia settings are read, imported or migrated implicitly.

Recent paths are validated with the same production path rules used by the
native list. An invalid collection becomes empty without resetting valid
theme/tool preferences. A valid list without explicit opt-in is ignored with a
recovery record. Ordinary user changes validate as one transaction and reject
an invalid field before publishing any part of the change. Serialization takes
recent values from the authoritative model, avoiding stale cached JSON fields.

## Atomic persistence and recovery

The native artifact is one JSON object with
`format: "com.xerahs.app/linux-qt/editor-settings"`, `version: 1`, and flat
preference objects in `current` and `previous`. Compatible unknown envelope
fields survive. `QSaveFile`, with direct-write fallback disabled, replaces the
whole artifact. A native lock and comparison against the original file bytes
reject concurrent/external changes before replacement. Only a successful
replace publishes new in-memory preferences and a new snapshot.

The artifact and its final directory require private owner-only permissions
(0600 and 0700). Reads and writes are bounded to 1 MiB, including both versions
and compatible extensions. Existing foreign-owned, shared, symlinked, unsafe,
unreadable or oversized storage is rejected. A missing file causes no write.
The native location is the generic XDG configuration directory plus
`com.xerahs.app/linux-qt/editor-settings.json`, isolated from the live Avalonia
configuration in `~/.config/xerahs/`.

Before a successful write, `previous` keeps only prior recent paths that still
exist in the next current list. Remove, Clear, trim and opt-out therefore erase
those forgotten paths from both versions in the same replacement. Opt-out also
removes prior opt-in authority. Other prior preferences remain recoverable.
These rules apply to retained path data; secure erasure from filesystem snapshots
and storage media is outside this configuration format.

If the current record is not an object but a compatible previous record
survives, loading uses that previous record and emits
`recovered-previous-settings`. Invalid individual fields still recover per
field, preserving unrelated values. A newer current record is never replaced
by an older previous record. If the entire artifact is malformed or unreadable,
the store preserves it read-only and reports the error: it cannot recover a
previous record from destroyed JSON. Independent whole-file recovery/import
flows, approved retention policy, and their human compatibility/privacy/security
reviews remain Required before complete qualification.

The native recent-file dialog describes private on-disk retention, edits a
draft and persists Apply before publishing it. Cancel performs no write. A
changed live list revision rejects a stale draft, including replacement at a
full one-entry limit. Save failure preserves both active and stored preferences.
Successful editor Open and complete raster/sidecar Save remember paths only
after opt-in; failed/cancelled/incomplete image operations do not add them.
Automatic recent-save failures leave successful image work intact and remain
visible across focus, zoom and document refreshes. The empty-path host mode
exists for in-memory test fixtures; application startup selects the native path.

Native diagnostics expose recovery without disclosing private values.
Recent paths must stay excluded from telemetry and support exports. The store
performs no network access and has no credential import/export authority.
Full secret-field classification and secure-service settings are separate
Required work, not implied by compatible unknown-field retention here.

## Native drawing defaults

Preferences → Drawing Defaults edits border/fill/thickness as one draft.
Accessible native fields, alpha-aware pickers and explicit format guidance share
production validation. Reset changes only the draft; Cancel performs no write.
Apply writes before publishing to every open editor. Invalid input, failed
persistence or a changed preference revision leaves the dialog open with a
value-free error. An incoming Open that changes recents also invalidates an older
drawing draft. Drawing changes preserve the live recent model revision, so they
cannot allow a stale recent draft to overwrite a newer entry.

Existing objects, selected objects, source files, dirty checkpoints and both
history branches survive a default change unchanged. Subsequent creations in
open, new and replaced sessions use the saved defaults. The toolbar's colour
and thickness actions explicitly edit the selected objects through their normal
history path, then save the resulting creation defaults. If that preference
write fails, the explicit document edit remains undoable and the old persisted
preferences remain active for other editors; a status error survives ordinary
focus, zoom and document refreshes. A later successful default change clears
that error without rewriting already edited objects.

Native tests exercise actual dialogs and colour pickers, Cancel/reset/Apply,
invalid inputs, file locking, stale incoming-image drafts, multiple editor
windows, redo preservation, PNG/sidecar persistence, replacement and restart.
All fixtures use temporary preference storage; this evidence does not approve
all settings fields, shared metadata or platform/release qualification.

## Window state and display recovery

The four window fields retain stable ledger names. Their default candidates
come from the pinned field census and remain subject to the shared semantic
review; they do not activate the settings capability. The numeric upper bound
is the native Qt widget extent limit, independent of image dimensions. Normal
client size uses Qt logical pixels, not physical pixels or a saved DPR.
The native window-state record contains no display identifier or window position. Missing native
window fields use their own defaults; invalid values recover independently and
emit field-specific diagnostics.

Disabling memory excludes width, height and maximized state from serialization
and clears them from the in-memory authority. The same atomic save disables
memory and excludes those fields in the retained previous version. Recovery
cannot silently re-enable memory. Other preferences and opted-in recents keep
their existing policies. The native memory toggle updates all editor actions only after a successful
atomic write; failure restores the original checked state and reports the error.
Enabling memory captures the current window state as that explicit preference
transaction. Toggling memory never resizes existing windows or edits documents.

`EditorWindowPlacement` chooses the usable display with the largest frame
intersection, or centres on the preferred/first usable display when the old
position is unavailable. It clamps client size and position within that
work area, including native frame margins. Small work areas take precedence
over the preferred minimum size. Fractional saved sizes are floored in logical
pixels; non-finite/invalid requests recover safely. Invalid displays and unsafe
frame margins are excluded; a temporarily empty display list yields no
placement and allows native recovery to wait for an available display.
Analytical tests cover disconnected/negative-origin displays, overlap, scale
changes, decorations, small work areas, invalid input and repeated recovery.

The native controller reads available Qt work areas and actual frame margins.
It observes screen addition/removal, screen/work-area geometry, logical and
physical DPI, and the native window's screen and state changes. Normal recovery
is queued until Qt completes the transition. The compositor owns maximized and
minimized geometry; returning to normal schedules work-area recovery. A
minimized window retains its preceding maximized flag and normal size. Missing
usable displays leave geometry/settings intact until an available display is
observed. The controller uses a display I/O port for deterministic native tests.

Window state saves only after the editor accepts close, following its existing
Save/Discard/Cancel decision. Cancel, failed image Save, and a pending operation
perform no window-state write. Reentrant close events are rejected while that
flow or its error reporting remains active. A failed preference write reports
its value-free diagnostic and still permits closing after acknowledgement;
the stored file and active preferences remain unchanged. The latest successful
accepted close supplies the normal size/maximized defaults for later editors.
Unchanged state causes no additional write or revision change.

Native tests cover actual Qt frame containment, removed/negative-origin/scaled
and tiny work areas through injected display input, maximized/minimized
normal-size retention, startup without writes, actual close cancellation,
accepted close/restart, locked writes, toggle rollback/shared action state,
disabled persistence, and recovery without changing source files, checkpoints,
undo or redo. Physical compositor/display/scale behavior, accessibility and
shared semantic/compatibility review remain required for full qualification;
these automated ports do not substitute for those acceptance gates.

## Appearance resolution and native integration

`EditorAppearance` is a Qt Core policy over validated preferences, a translated
desktop hint record and a native palette. `UseSystemTheme` or the compatible
`Theme=System` value follows the desktop colour scheme; an unspecified scheme
uses the native palette. Explicit Light/Dark choices select the corresponding
native-edition palette. `UseSystemAccentColor` selects a valid desktop accent,
falling back to the native highlight; otherwise the saved manual accent applies.
These native metadata/precedence choices remain subject to shared semantic
review before activation.

The [desktop portal Settings reference](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html)
defines colour-scheme, sRGB accent, contrast and reduced-motion hints. Invalid
or unknown hint values revert only that hint. Saved accent alpha is retained;
display colours are composited over their actual backgrounds. Text roles target
4.5:1 and higher-contrast text 7:1. Highlight roles target 3:1 against their
adjacent surfaces; an unusable accent receives a neutral display replacement.
Resolution leaves preferences unchanged. The analytical sRGB implementation
and tests follow the [W3C contrast definition](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html).
These palette checks do not establish complete native accessibility or focus
rendering qualification. Physical compositor, assistive technology, shared
semantic and compatibility reviews remain Required.

`NativeDesktopAppearance` asynchronously reads only the standardized appearance
namespace over the Settings portal, accepts typed SettingChanged signals, and
watches service ownership. Newer signals and owner changes supersede in-flight
snapshots; a restarted owner is read afresh. The observer resolves the unique
portal owner asynchronously and subscribes to that sender before its first
snapshot, so early changes do not depend on a well-known-name cache. Signals
already queued from a previous owner are rejected by their sender identity.
The observer explicitly registers its slot payload types before subscribing;
Qt 6.4 must not depend on an earlier read to register `QDBusVariant`. Failed
subscriptions and unsupported, missing or disconnected portals leave the
native Qt fallback. The observer never writes host settings
or native preferences. High contrast applies independently of theme/accent
choices. Reduced motion disables six Qt menu/combo/tooltip/toolbox effects and
restores their preceding effective state when removed, on owner loss, or when
the observer stops. Other style-specific animation and complete assistive
technology/focus qualification remain review work.

Editor-root palettes follow successful preference publication, desktop hints
and native ApplicationPaletteChange events. QApplication's palette remains the
native source, avoiding feedback from application overrides. Active, inactive
and disabled native control roles, placeholder and tooltip text share readable
display colours; child controls/dialogs keep their native widget semantics,
fonts and focus. Appearance changes never edit source pixels, annotations,
selection, saved checkpoints or history.

The native Appearance dialog keeps a local draft with system-theme/accent
switches, manual Dark/Light and an alpha-aware colour picker. Reset modifies
only the draft. Apply validates/saves all four fields before publication;
invalid, locked, read-only or stale changes leave the dialog open with a
value-free accessible alert. Cancel preserves both stored and active values.
Compatible Theme=System loads as following the desktop; an explicit Apply
stores the dialog's manual Dark/Light fallback plus system switch. Runtime
desktop changes do not normalize or rewrite preferences. Native window
operations remain guarded while the dialog is open.

Tests exercise actual editor actions, image/sidecar/checkpoint/redo isolation,
multi-window/restart, cancellation/reset, invalid/locked/stale drafts, native
alpha picking, focus and accessible names. A separate private D-Bus with no
host service activation tests actual ReadAll/signals, unsigned flags and RGB
tuples, invalid hints, cold-start live updates, first-read subscription, delayed
snapshot races, old-owner signals, service loss/restart and reduced motion
without preference writes.

## Toolbar placements and hotkeys

`ToolbarPreferences` owns a Qt Core model with the census field names `Id`,
`IsVisible`, `BeginGroup` and `Hotkey`. IDs use lowercase ASCII identifiers
of at most 80 characters; visibility is Boolean, a missing/null group flag
inherits the native action's group default, and hotkeys are strings of at most
64 characters. Missing visibility defaults to true and a missing hotkey to
empty. Compatible extension properties survive normalization and reset.

The native candidate catalogue uses stable semantic IDs for the currently
represented editor commands. Absence initializes the existing fourteen visible
Edit/View commands; other registered commands start hidden. A present empty
array is an explicit empty layout, rather than an instruction to regenerate
defaults. These ID/default/missing-field choices require the shared metadata
and compatibility review; source initializers do not approve them.

Nonempty additional hotkeys contain one chord: Ctrl/Alt/Shift/Meta modifiers,
ASCII letters/digits, F1..F35 or documented named navigation/punctuation keys.
Canonical spelling is locale independent, with modifiers in Ctrl/Alt/Shift/Meta
order. Plus is a logical key and discards an incidental Shift modifier.
Malformed sequences, duplicate modifiers, duplicate action placements and
duplicate canonical hotkeys are rejected as one change. Multi-chord sequences
and localized key names are outside this native candidate format.

The production policy protects every gesture in the shortcut catalogue,
including commands still awaiting native implementation. A protected gesture
may be assigned only to its own action. Native zoom and close bindings are
also protected. Escape/Enter, focus traversal, space-pan, selection movement
and resize, bracket ordering, selection rotation, desktop Meta gestures,
Alt+F4, Ctrl+Q and Ctrl+Alt function-key session switching cannot be reassigned.
An empty hotkey means no additional binding; protected defaults remain available.

Compatible unknown action entries remain inactive and round-trip byte-equivalent
JSON values, with a value-free warning. Their hotkey strings are not interpreted.
Ordinary changes cannot introduce, edit or remove these entries; reorder and
reset retain them. Invalid known layouts recover to native defaults while
retaining compatible unknown entries; unrelated preferences remain unchanged.
Recovery/reset insert known defaults only within the remaining collection
capacity, so retained future entries never cause an oversized repaired list.
Nonfatal preservation warnings do not make an otherwise valid atomic save fail.

Core tests cover independent gesture references, collection/type/length bounds,
protected/reserved collisions, explicit empty layouts, nullable groups, isolated
recovery, reset, extensions, inactive future data, real private storage and
restart. Native `ToolbarDialog` provides reorder, show/hide, nullable grouping,
additional hotkeys and draft reset for the 46 currently registered editor
commands. Apply saves before publishing to every open editor; Cancel,
invalid/locked/stale/read-only writes leave the layout and preferences intact.
The active native hotkey cell editor is committed before Apply reads the draft.
Unknown entries are read-only and retain their original JSON.

`NativeEditorToolbar` reuses production actions. Hidden group boundaries carry
forward to the next visible command, without leading separators. Removing a
toolbar placement leaves its fixed menu and protected shortcut available;
Preferences/Toolbar, View/Show Toolbar and File/Close Editor remain reachable
with an empty layout. Additional shortcuts share command enablement and yield
to text entry, modal/popup widgets and pending editor operations or gestures.
An additional binding equal to its own protected binding is not registered
twice. Native tests cover real delegate editing, atomic controls, multiple
windows, restart, dispatch, text undo, preserved recent-list authority and
actual editor files/checkpoints/redo. Complete contracted command coverage,
physical keyboard/accessibility qualification and shared ID/default/format
and compatibility acceptance remain Required.

Print is the 39th native command, hidden in the unchanged defaults; Ctrl+Shift+P
retains Print ownership and Ctrl+P retains pin-to-screen ownership. Its native
output controls and remaining qualification are indexed in
[EDITOR-PRINT.md](../../app/EDITOR-PRINT.md).

Annotation Cut/Copy/Paste/Duplicate add four hidden native commands (43 total).
Their fixed protected shortcuts, text/modal/busy precedence and remaining
qualification are indexed in [ANNOTATION-TRANSFER.md](../../app/ANNOTATION-TRANSFER.md).

The tool-mode checkpoint added hidden `select-tool` and `rectangle-tool` commands (45 commands at that checkpoint) with unchanged fourteen visible defaults. Checked native controls, text/busy shortcut precedence and explicit mode guidance are indexed in [ANNOTATION-TOOLS.md](../../app/ANNOTATION-TOOLS.md). Their ephemeral state creates no preference write or document mutation. Complete catalogue/physical qualification remains Required.

`rectangle-properties` adds a hidden selected-object command (46 current commands) without changing the fourteen visible defaults. It edits only the chosen rectangle; border/fill/thickness/opacity defaults and the private preference file remain unchanged. Native text/modal/busy and no-repeat rules apply. [Rectangle edit evidence](../../app/RECTANGLE-EDIT.md) covers actual subsequent editor/capture creation and save/reopen; full catalogue/human qualification stays Required.
