# Product Contract reviews

This directory contains review packets for human decisions required by
`ROOT-REVIEW-001`. A packet is an authored aid, not approval. Approval exists
only when the human product owner records an explicit decision and the affected
manifest lifecycle and evidence are updated accordingly.

`APPROVAL-2026-08-31-ALL-PACKAGES.json` approves the original package set.
`APPROVAL-2026-08-31-PRODUCT-IDENTITY.json` records the subsequent product-owner
identity amendment to `CORE-PLATFORM-001` version `0.2.0` within contract version
`0.1.0`.
`APPROVAL-2026-08-31-NUGET-FRESHNESS.json` records standing agent authority and
the latest-stable maintenance requirements in `CORE-PLATFORM-001` version
`0.3.0`.
`APPROVAL-2026-09-28-IMPLEMENTATION-READINESS.json` approves contract version
`0.2.0`: the implementation-readiness clarifications to the four qualification
capabilities, the Linux edition model in `CORE-PLATFORM-001`, and the unchanged
carry-forward of every other package. `APPROVAL-2026-09-28-MOBILE-TARGETS.json` approves contract version `0.3.0`,
adding native Android and iOS targets through `MOBILE-PLATFORM-001`.
`APPROVAL-2026-10-08-LINUX-HYPRLAND-TOOLKIT.json` approves contract version
`0.3.1`: `CORE-PLATFORM-001` version `0.5.1` confirms Qt 6 with LayerShellQt as
the `linux-hyprland` toolkit under D-LIN-002.
`APPROVAL-2026-11-14-CORE-025-WATCH-PUBLICATION.json` approves contract version
`0.4.0`: `CORE-PLATFORM-001` version `0.6.0` adds the CORE-025 watch
publication disposition and retention subsection, with its decision record and
test vectors. The other 15 packages carry forward to contract 0.4.0 unchanged
in normative text from 0.3.2.
Records for earlier contract versions
are retained history; only records for the manifest's current version count.
All records explicitly withhold activation, census closure, and release
authorization.

Review packets MUST distinguish:

- draft linkage from approved product behavior;
- static reference evidence from runtime observation;
- cross-platform requirements from accepted degraded or unavailable decisions;
- authored security, privacy, accessibility, compatibility, and licensing rules
  from the corresponding specialist and product-owner review; and
- unresolved work from an approved, time-bound waiver or scope change.

`APPROVAL-2026-11-12-FILENAME-WIDE-SAMPLING.json` records Mike's explicit approval
of FILENAME-GENERATION-001 0.2.1 within contract 0.3.1: minimal-width big-endian
rejection sampling for random-file lists above 256 entries. Existing one-byte
results are preserved; no activation or release gate is waived.
