# Contract: region capture closure (RC-003, 004, 005, 010, 012, 014, 017, 018, 019, 022, 027)

Worktree: /home/mike/Work/BriarForge/XerahS/worktrees/codex-stores (detached at origin/main). Shared files get minimal, localized edits. For each row: read the full requirement bullet (REGION-CAPTURE-001 SPEC.md, its test-vectors and scenarios), map clauses to named passing tests, close real gaps, promote when every clause is proven ("Audit 2026-11-04: ..." naming tests; audit.classification implemented; coverage "Tested Linux candidate seam; native/physical/human qualification is not accepted by this classification."). One row at a time.

## Decision (Aoife, Mike's similar-apps rule) that unblocks RC-003/004/012/022/027 and CORE-014
The authoritative pre-acquisition topology source on Linux is Qt's screen list, as Flameshot and the reference XerahS app use their toolkit's screen list:
- Display identity: a stable ID derived from QScreen name + manufacturer + model + serialNumber (hash; never a list index).
- Per display: logical geometry (QScreen::geometry), scale (devicePixelRatio), rotation (orientation), and physical bounds computed per display as the vectors define (RC-004/RC-022: never apply one display's scale to another).
- Snapshot this topology when the session starts (RC-003); a QScreen add/remove/geometry/DPR change during selection cancels with `display-topology-changed` (already partly implemented — verify).
- After the portal returns its bitmap, cross-check its size against the snapshot's physical desktop bounds. A mismatch fails with a stable code `capture-topology-mismatch` (never guess or resample) — that is the accepted degraded disposition when Qt and the compositor disagree.
- Last region (RC-027): validate the stored display IDs and physical bounds against the CURRENT Qt topology BEFORE requesting any screenshot; a missing display or changed bounds fails `last-region-invalid` without capturing.
- Composition (RC-012): composite per display in desktop physical order without gaps, duplication or resampling, using the snapshot mapping, and prove it with a fake two-display mixed-scale topology (e.g. 1920x1080@1 next to 2560x1440@2) driven through the production mapping/compositor seam.
Record this decision and rationale in each affected row note and in the CORE-014 note.

## Other rows
- RC-005: overlay on every capturable display (one per QScreen) and never in the result — prove with a fake multi-screen test.
- RC-010: arrow move/resize and handles clamped to the capturable desktop; Shift step documented in the overlay instructions — add the documentation text and test it.
- RC-014: frozen capture is one session snapshot (the worker must not re-read the portal file at confirm — fix if it does); live mode is not offered, so state that disposition.
- RC-017, RC-018, RC-019: verify clauses, close gaps (RC-019 high-contrast boundaries and non-color-only cues in the overlay).
RC-013 (cursor) and RC-015 (HDR) need platform dispositions; do not implement, just note what the portal offers. RC-020 is performance measurement; skip.

Unspecified semantics: follow the reference XerahS app (read-only /home/mike/Work/ShareX/XerahS), else ShareX/Flameshot/Greenshot majority. List sandbox-blocked tests by exact name.
