# Agent orchestration tools

Development support for multi-agent work on the Linux reference edition (`platforms/linux/linux-qt`). An orchestrator (Aoife, Claude Opus) hands bounded slices to workers, reviews each diff, and is the only one who pushes. These scripts encode that loop. They change no product behavior and grant no approvals (TOOL-NO-POLICY-001).

| File | Purpose |
|---|---|
| [implementer-brief.md](implementer-brief.md) | Shared rules prepended to every worker slice: scope, graphify-first navigation, build/test commands, traceability honesty, commit identity, no push. |
| [codex-launch.sh](codex-launch.sh) | Starts or resumes a Codex CLI slice in a detached worktree. Survives usage-limit stops: records the reset time, refuses to start before it, and resumes from the work left in the worktree. Safe to call from a scheduler every few minutes. |
| [contracts/](contracts/) | Slice contracts handed to Codex (the brief is prepended by the launcher). |
| [gate.sh](gate.sh) | Integration gate: cherry-picks worker commits onto `origin/main`, resolves conflicts only in generated files, regenerates progress and the Linux graph, runs the progress check, contract linter, full build and the whole CTest suite, then pushes through the orchestrator's wrapper. |

## Worktrees

Workers use detached worktrees at `worktrees/<task>` (AGENTS.md layout; `/worktrees/` is in `.git/info/exclude`). Create one per slice from `origin/main`:

```bash
git worktree add --detach worktrees/<task> origin/main
```

Workers commit with their own wrapper and never push: Sonnet subagents use `git-viktor`, Codex uses `git-declan`. The orchestrator integrates with `gate.sh` from its own worktree and pushes with `git-aoife`.

## Codex usage limits

Codex stops when its usage window is exhausted, and the window reopens about five hours later. Uncommitted work stays in its worktree. Call the launcher on a schedule:

```bash
tools/agent-orchestration/codex-launch.sh worktrees/codex path/to/contract.md
```

It does nothing while Codex is running or before the recorded reset time. Otherwise it starts the run, adding a resume preamble when the worktree already holds work from an earlier run. State (log, last message, retry time, status) lives in `worktrees/.codex-state/<worktree>/`.

The launcher uses the workspace-write sandbox. Tests needing sockets or D-Bus may not run inside it, so `gate.sh` always runs the full suite before any push.

## Contracts

A slice contract names the worktree, the requirement IDs and their spec text, the current traceability note, what to build, and the tests that prove it. Unspecified product details follow the standing rule: use the reference XerahS app's behavior, otherwise what ShareX, Flameshot and Greenshot users would expect, and record the rationale in the traceability note. Contract text changes, waivers and security sign-off stay with the human product owner (ROOT-REVIEW-001).
