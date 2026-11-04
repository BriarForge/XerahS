# Contract: close editor output and session rows (EO-001, 006, 011, 012, 013, 014, 015, 016; ES-009, 019, 020, 021; EC-014, EC-017)

Worktree: /home/mike/Work/BriarForge/XerahS/worktrees/codex-stores (detached at origin/main). Shared files get minimal, localized edits. For each row: read the full requirement bullet (EDITOR-OUTPUT-ACTIONS-001, EDITOR-SESSION-001, EDITOR-CANVAS-001 SPEC.md), list clauses, map each to a named passing test (read the test body), close real gaps, promote when every clause is proven ("Audit 2026-11-04: ..." naming tests; audit.classification implemented; coverage "Tested Linux candidate seam; native/physical/human qualification is not accepted by this classification."). One row at a time. Physical/desktop/human qualification is NOT a blocker.

Do NOT work on rows waiting for Mike: EO-004 (format metadata approval), EO-009 (approved pin service), EO-010 (display/layout disposition), ES-014/ES-030 (legacy fallback companion policy), EC-013 (smart padding rules). If one of your rows depends on them, keep it partial and say so.

Decisions for unspecified semantics (Aoife, Mike's similar-apps rule; record one line in each note):
- ES-020 retention: temporary editor files (preparation temp files, private managed upload temps) are deleted when their task ends and swept at startup if older than 24 hours; no editor content is ever placed in logs or support output. (ShareX/Greenshot keep no editor temp history.)
- EO-013 forced termination: when the desktop session manager asks to quit (QGuiApplication::commitDataRequest / aboutToQuit), dirty editors follow the same Save/Discard/Cancel policy where the session manager allows interaction, and otherwise keep the document dirty without saving silently. Test with the QSessionManager seam if feasible; else document.
- ES-021 performance: add a benchmark-style test asserting a 3840x2160 source with one rectangle reaches interactive state within 500 ms on this machine (QElapsedTimer; skip with a clear message when running under valgrind/sanitizers), and that export exposes progress and cancellation. 60 Hz manipulation is hardware qualification, not a blocker.

Unspecified semantics otherwise: follow the reference XerahS app (read-only /home/mike/Work/ShareX/XerahS), else ShareX/Flameshot/Greenshot majority. List sandbox-blocked tests by exact name.
