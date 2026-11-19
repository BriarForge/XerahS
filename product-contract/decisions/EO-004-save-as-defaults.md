# EO-004: Save As format disposition and quality/matte defaults

Decision: EO-004-SAVE-AS-DEFAULTS-2026-11-19, resolved by the build loop
under the maintainer's delegated owner-block authority on 2026-11-19
(agent opencode, GLM 5.3).
Contract: 0.4.1; EDITOR-OUTPUT-ACTIONS-001: 0.1.1. Requirements: EO-004.

## Problem

The approved SPEC requires the reachable Save As set to be recorded in
`format-disposition.json`, but that file still carried the
draft-selection-pending-product-owner-approval status, and the recorded
open questions in `platforms/linux/linux-qt/app/EDITOR-SAVE.md` named
four undecided points: confirm the format set; choose JPEG/WebP quality
defaults and whether WebP exposes lossless; require an explicitly chosen
opaque JPEG background or approve a documented default; and decide
original-profile versus explicit export-profile handling.

## Choice

- The format set is confirmed as the SPEC's reachable set: PNG, JPEG and
  WebP. AVIF, GIF and BMP remain not selected for the recorded reasons.
- JPEG quality defaults to 90 and WebP quality defaults to 90. Both
  follow ShareX's shipped defaults (`TaskSettings.cs`: ImageJPEGQuality =
  90, ImageWebPQuality = 90) and GIMP's JPEG export default
  (`plug-ins/file-jpeg/jpeg.c`: the "quality" procedure argument defaults
  to 0.9). One memorable value for both formats, at the quality end both
  references chose.
- The JPEG transparent-pixel background defaults to white (#FFFFFF),
  following ShareX's flatten path (`TaskHelpers.cs`:
  `SkiaImageHelpers.FillBackground(img, Color.White)`). The value
  satisfies the recorded alternative "approve a documented default": it
  is documented here and in `format-disposition.json`, prefilled in the
  Save As dialog, and remains user-overridable - explicit entry stays
  possible, and emptying the field is still refused.
- The defaults prefill the dialog fields only. They are not sticky
  per-user settings: the settings-ledger semantics that remembered
  quality/matte would need are a separately blocked owner question
  (SETCAT/ESET ledger defaults), and the contract records that
  successful Save As options stay editor-local.

Deferred, recorded as follow-ups rather than v1 requirements:
WebP lossless exposure (the disposition's webp row stays
quality-controlled lossy) and the export color-profile/metadata policy
(outside the disposition's recorded fields, unchanged with the EO-014
profile work).

Sources (fetched and read 2026-11-19):
https://raw.githubusercontent.com/GNOME/gimp/master/plug-ins/file-jpeg/jpeg.c
(GIMP JPEG export default quality 0.9); the ShareX behavioural reference
sources `/home/mike/Work/ShareX/ShareX/ShareX/TaskSettings.cs` (lines
347 and 350) and `ShareX/TaskHelpers.cs` (FillBackground(Color.White) at
lines 455, 487, 561).

## Impact

`format-disposition.json` becomes decided with the defaults recorded; the
manifest moves to contract 0.4.1 with EDITOR-OUTPUT-ACTIONS-001 at 0.1.1
and a full sixteen-package approval record
(APPROVAL-2026-11-19-EO-004-SAVE-AS-DEFAULTS.json) whose carry-forward of
the other fifteen packages is verified by this commit's diff. The native
Save As dialog prefills quality 90 and JPEG matte #FFFFFF, so saving
JPEG or WebP needs no typed values; validation is unchanged and clearing
a field is still refused. No encoder, path, sidecar or settings behavior
changes. Release remains Mike's, and activation remains gated on
conformance.
