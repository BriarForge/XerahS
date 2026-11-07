# pstack (vendored subset)

Agent skills from [pstack](https://github.com/cursor/plugins/tree/main/pstack) by Lauren Tan, vendored as dev tooling for agents working in this repository. They are not product code and ship in no build.

## Provenance

| Field | Value |
|---|---|
| Upstream | `https://github.com/cursor/plugins`, folder `pstack/` |
| Commit | `6b0300214a09fd6029cfb7077043125758cd1fd2` (2026-11-06) |
| Version | `0.15.36` |
| License | MIT, copyright 2026 Lauren Tan. Full text in [LICENSE](LICENSE). MIT is compatible with this repository's GPL v3 (ROOT-LICENSE-001). |

Each vendored skill is a byte-for-byte copy of the upstream folder. Do not edit them in place. To update, recopy the same list from a newer upstream commit, rerun the checks below, and update this table.

## Where the files live

- Skills: `.codex/skills/<name>/`, next to `graphify`. Codex reads them there.
- Claude Code reads the same files through the `.claude/skills` and `.claude/agents` symlinks. On Windows, Git checks symlinks out as plain files unless `core.symlinks=true` and Developer Mode are on. Without them, Claude Code users can install the full plugin instead (`/plugin marketplace add cursor/plugins`, then `/plugin install pstack@cursor-plugins`), keeping the rules below.
- Agent: [`agents/comment-sicko.md`](agents/comment-sicko.md), used by the `no-comments` skill.
- Upstream writes `<pstack>/skills/<name>/SKILL.md`. Here that path is `.codex/skills/<name>/SKILL.md`.

## Vendored skills

`arena`, `architect`, `benchmark-checklist`, `blast-radius`, `bro`, `correct`, `create-verification-skill`, `maintain-verification-skill`, `how`, `why`, `teach`, `in-your-own-words`, `interrogate`, `no-comments`, `sharpen`, `show-me-your-work`, `tdd`, `technical-writing`, `unslop`, and every `principle-*` skill (24).

## Left out, and why

| Upstream item | Reason |
|---|---|
| `poteto-mode` and its playbooks, `figure-it-out`, `poteto-help` | The playbooks create stacked branches and worktrees, force-push with lease, and merge PRs (`gh pr merge`, auto-merge). That conflicts with ROOT-BRANCH-001/002 and ROOT-REVIEW-001. Its `scripts/bootstrap.ts` also runs `bun install` on first use. `figure-it-out` and `poteto-help` require `poteto-mode`. |
| `make-bot-ui` | Installs Tailscale with `curl … \| sudo sh` and exposes a local HTTP server that wakes a Cursor webhook routine. |
| `setup-pstack`, `swarm`, `automate-me`, `reflect`, `recall`, `ketchup`, `ketchup-video`, `automations/benny` | Cursor-specific (model-routing rule, Automations, transcript stores) or they rewrite skills from chat history. |
| `statechart`, `create-formal-verification-skill`, `maintain-formal-verification-skill` | About 4 MB of TLA+, Java, and Bun tooling with no current use here. Add them in a separate change if the team wants formal models. |
| `typescript-best-practices` | The repository has no TypeScript. |
| `docs/guide`, `assets` | Cursor walkthrough and images. Read them upstream. |

## Rules for agents using these skills in XerahS

The root [AGENTS.md](../../AGENTS.md) and its children win over any pstack text. In particular:

1. **No branches or worktrees.** Where a skill says to give a worker or candidate its own git worktree or branch (`arena`, `interrogate`, `blast-radius`, the `separate-before-serializing-shared-state` principle), use a scratch directory outside the repository instead, such as `/tmp/arena-<slug>/candidate-<n>/` (which `arena` already allows). Copy the files the worker needs. Do not run `git worktree add` or `git checkout -b`.
2. **No merging, auto-merge, or force-push.** Skills author changes. Humans approve and land them (ROOT-REVIEW-001).
3. **Commits and pushes** follow the git wrapper and progressive-commit rules in AGENTS.md, not pstack's PR conventions.
4. **`principle-never-block-on-the-human`** covers reversible work only. Contract changes, waivers, and security-sensitive code still need a human product owner.
5. **graphify first.** Skills that explore code (`how`, `why`, `blast-radius`, `interrogate`) start with `graphify query` per the graphify section of AGENTS.md, and pass that rule into every subagent brief.
6. **Model names.** Skills name Cursor model slugs such as `claude-opus-5-5-xhigh` and `grok-4.7-xhigh-fast`, read from a `pstack-models.mdc` rule. Without that rule, omit the model and use the session's model. In Claude Code, a `claude-*` slug maps to `model: "opus"` with the matching effort.
7. **Missing skills.** When a vendored skill points to `poteto-mode` (for example, for subagent reasoning levels), skip that pointer and choose a sensible level yourself. When it names `typescript-best-practices`, ignore it.
8. **Cursor-only steps** (Bugbot, Cursor cloud agents, Automations) are skipped with a one-line note, as upstream's Claude Code adapter does.

## Tool names in Claude Code

| Cursor | Claude Code |
|---|---|
| `Task` | `Agent` |
| `subagent_type: generalPurpose` | `subagent_type: "general-purpose"` |
| `readonly: true` | `subagent_type: "Explore"`, and tell it not to edit files |
| `AskQuestion` | `AskUserQuestion`, or ask in plain text |
| Run another pstack skill | Read `.codex/skills/<name>/SKILL.md` and follow it |
| `subagent_type: "comment-sicko"` | `subagent_type: "comment-sicko"` (from `.claude/agents`) |

## Security review summary (at the pinned commit)

- No hooks, MCP servers, or always-on rules in the plugin manifest. Skills are Markdown instructions that load only when invoked (`disable-model-invocation: true`).
- The one vendored script, `show-me-your-work/scripts/log.sh`, appends a row to a local TSV and escapes spreadsheet formula prefixes. It makes no network calls.
- Network or install steps found upstream (`curl | sudo sh`, `bun install`, `npm` Pele renderer) are all in excluded skills.
