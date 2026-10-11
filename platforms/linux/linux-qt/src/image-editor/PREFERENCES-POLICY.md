# Native editor preference storage

This is implementation evidence for ESET-001/002/003/009/012/015, not activation
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
| `Theme` | Enum string, `Dark` | `Dark`, `Light`, `System` | Non-secret preference; future native theme/system-theme resolution and accessible contrast remain Required | `SET-IMAGE-EDITOR-OPTIONS-THEME-001`; load/change/round-trip tests; UI effect pending |
| `BlurStrength` | Finite number, 30 | 0..100; fractional values allowed | Non-secret preference; future blur creation defaults remain Required | `SET-IMAGE-EDITOR-OPTIONS-BLUR-STRENGTH-001`; published isolation vector and independent boundaries; blur tool pending |
| `Thickness` | Integer, 4 | 1..10000, within the production editor width limit | Non-secret preference; future creation defaults must leave existing objects unchanged | `SET-IMAGE-EDITOR-OPTIONS-THICKNESS-001`; validation/isolation tests; preference-to-tool wiring pending |
| `BorderColorHex` | String, `#FFFF0000` | Exactly `#RRGGBB` or `#AARRGGBB`, hexadecimal digits; canonical uppercase `#AARRGGBB` | Non-secret drawing default; alpha, including transparent RGB, survives; applies to new objects | `SET-IMAGE-EDITOR-OPTIONS-BORDER-COLOR-HEX-001`; core alpha/invalid/restart tests; default agrees with the ES rectangle qualification default; native wiring pending |
| `FillColorHex` | String, `#00000000` | Same canonical colour validation as border | Non-secret drawing default; applies to new objects | `SET-IMAGE-EDITOR-OPTIONS-FILL-COLOR-HEX-001`; core alpha/invalid/restart tests; default agrees with the ES rectangle qualification default; native wiring pending |
| `RememberRecentImagePaths` | Boolean, false | Boolean only; false ignores and removes retained paths | Private local consent; only later successful Open/Save may add paths; no backfill | Native opt-in authority under ESET-012, absent from the pinned legacy field census; core/native runtime tests |
| `MaxRecentImageFiles` | Integer, 10 | 1..100 | Private local retention policy; decreasing it removes oldest entries | `SET-IMAGE-EDITOR-OPTIONS-MAX-RECENT-IMAGE-FILES-001`; bounded model and preference tests |
| `RecentImageFiles` | Array of strings, empty | At most 100 encoded entries and at most the validated maximum distinct absolute paths; valid platform path syntax | Private local data; newest first, deduplicated, removable individually or together; absent resources stay removable | `SET-IMAGE-EDITOR-OPTIONS-RECENT-IMAGE-FILES-001`; path/opt-in/isolation/removal/recovery tests |

`BorderColor` and `FillColor` are derived convenience aliases, classified
`NonPersistent` and excluded from both saved versions. Only their canonical
`Hex` fields are authorities. A stored alias cannot override the corresponding
field; compatible unknown colour fields still round-trip. Colour names, short
hex, whitespace, incorrect lengths/types and malformed digits recover only the
affected field and emit `invalid-BorderColorHex` or `invalid-FillColorHex`.
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
