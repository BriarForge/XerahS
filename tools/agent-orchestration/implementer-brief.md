# Implementer brief (XerahS native, Linux reference edition)

Shared rules prepended to every slice given to a worker agent. Maintained with [the orchestration README](README.md).

You are an implementer (a Claude Sonnet subagent or the Codex CLI) working for Aoife (Claude Opus, orchestrator). Aoife reviews your diff, integrates and pushes. You do NOT push.

Repository: BriarForge/XerahS. Your worktree is given in the task (a detached-HEAD checkout of origin/main). Work ONLY inside it. Never touch /home/mike/Work/BriarForge/XerahS itself or other worktrees. Do not create branches, do not switch branches, do not stash, do not rebase.

Read first, in this order: <worktree>/AGENTS.md, platforms/AGENTS.md, platforms/linux/AGENTS.md, platforms/linux/linux-qt/README.md, platforms/linux/linux-qt/IMPLEMENTATION.md (skim), the capability SPEC.md and SCENARIOS.feature for your requirement, and the current traceability row in platforms/linux/linux-qt/traceability/<CAPABILITY>.json.

Graphify rule (mandatory): before your first grep/search/source read for a codebase question, run `graphify query "<question>"` from inside platforms/linux (and product-contract for requirements). Prefer `graphify explain "<concept>"` / `graphify path "A" "B"`. Never read graph.json/graph.html. Then read the source files it points at.

Engineering rules:
- Qt 6.4+ / C++17, dynamic Qt. Product logic with a conformance seam goes in linux-qt/src/ libraries depending only on Qt Core; native UI/D-Bus in app/. The conformance adapter holds no product logic.
- Follow existing patterns: OwnedWork workers, guarded async dialogs (Apply/Cancel), PrivateJsonFile, stable error codes, value-free diagnostics, EditorSession history. Reuse before adding.
- Do not weaken the Product Contract, change expected results, add waivers, or edit product-contract/. If a requirement detail needs a product decision, implement the unambiguous part, record the open question in the traceability note, and report it to Aoife. Never invent policy.
- No new third-party dependency (DEPENDENCIES.md) without telling Aoife; Qt modules already linked are fine.
- Tests: add Qt Test cases that exercise the real production path (native tests under linux-qt/tests, registered in CMakeLists.txt). CMakeLists.txt and test files are always in scope.
- Build in your worktree: from platforms/linux/linux-qt: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j4`. Other agents build concurrently: use -j4 at most. Run your new/affected tests with `ctest --test-dir build -R '<regex>' --output-on-failure`, then the whole suite once at the end with `ctest --test-dir build -j4 --output-on-failure` (tests named editor-window-* can be flaky under load; rerun alone before concluding failure). GUI tests use QT_QPA_PLATFORM=offscreen as the suite already configures.
- Traceability: update the row's status/note/evidence/audit for what you actually proved. Only mark `implemented` if the whole requirement text is met by native production code with automated tests; otherwise keep `partial` with an honest note. Then from the worktree root run `python tools/platform-progress/update.py` and `python tools/platform-progress/update.py --check`. Also run `python tools/contract-linter/lint.py`.
- After code changes run `graphify update .` inside platforms/linux.
- Commit in your worktree with your wrapper: Sonnet uses `git-viktor commit` (Viktor Hale), Codex uses `git-declan commit` (Declan Murphy), when the slice builds and its tests pass. Message format: `[Linux] <imperative outcome sentence>` then a short body. Sonnet ends the message with the line
  Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
  Inspect `git status` and `git diff --stat` before committing; never commit build/ output.
- Do NOT push.

Final report to Aoife (concise): commit SHA(s), what user-visible behavior now works, files changed, tests added and the exact ctest result line, traceability status changes, and any open product questions or things you could not verify.
