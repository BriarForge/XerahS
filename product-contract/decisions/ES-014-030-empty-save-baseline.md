# ES-014/030: empty save with a baseline companion

Decision: ES-014-030-LINUX-2026-10-15, resolved by maintainer Aoife on 2026-11-14.
Source: interactive maintainer brief, “select next Linux journey completion”.
Contract: 0.3.2; EDITOR-SESSION-001: 0.2.2. Requirements: ES-014, ES-030, EO-006.

An explicit empty save writes a valid zero-annotation default companion when a
baseline-named companion exists. The default contains the normal ES-013 source
binding, including the published raster SHA-256. Readers prefer the default,
so the baseline annotations stay hidden. The baseline file is never read for
this existence decision, modified, renamed or removed. Deleting the default
later restores baseline discovery, making this policy reversible.

Without a baseline, explicit empty save still removes the default and returns
no companion path. Without explicit intent, neither companion is changed.
Nonempty saves and refusal of unsupported or damaged defaults are unchanged.
A baseline appearing after Core's removal admission still makes removal fail
honestly; an explicit retry then follows the shadow-write path.

The pinned baseline uses the replacement-extension name for its own default
companion and deletes it on empty save. XerahS preserves that separately owned
state under ROOT-IDENTITY-001. The baseline application can still read its own
companion; this is accepted because XerahS replaces it at release.

The [runtime observation](../reference-baselines/provenance/kova-0.29.0-xann-sidecar-observation.md)
records the pinned library's actual path/save behavior. Until that observation
succeeds, ES-030 remains partial. Source reading alone is insufficient.

Impact: version-1 XANN schema and source hashes are unchanged. Linux is the
first native implementation; Windows and macOS remain unimplemented and must
apply this policy when built. No platform waiver, ownership expansion,
credential access or release approval is granted.
