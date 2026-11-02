#!/usr/bin/env bash
# Launch (or resume) one Codex CLI slice in a detached worktree, surviving usage-limit stops.
#
#   codex-launch.sh <worktree> <contract.md> [state-dir]
#
# - Exits 0 without doing anything when Codex is already running in <worktree>,
#   or when a recorded usage-limit reset time has not passed yet.
# - Prompt = implementer-brief.md + <contract.md>. When the worktree already holds
#   commits or uncommitted changes (a previous run stopped mid-slice), a resume
#   preamble tells Codex to continue that work instead of starting over.
# - After the run, a "usage limit ... try again at <time>" message is parsed and
#   stored as the next allowed start, so a scheduler can call this every N minutes.
# - Runs in the workspace-write sandbox. Tests needing sockets/D-Bus are run by the
#   orchestrator's integration gate (gate.sh) outside the sandbox.
#
# State files (default <state-dir> = <worktree>/../.codex-state/<worktree-name>):
#   run.log        appended Codex output      last.md     Codex's final message
#   retry-after    epoch seconds before which no new start is attempted
#   status         running | finished | usage-limit | failed
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
worktree="$(cd "${1:?worktree}" && pwd)"
contract="$(realpath "${2:?contract}")"
name="$(basename "$worktree")"
state="${3:-$(dirname "$worktree")/.codex-state/$name}"
mkdir -p "$state"
log="$state/run.log"

if pgrep -f "codex exec .*-C $worktree( |$)" >/dev/null; then
  echo "codex-launch: already running in $worktree"; exit 0
fi
now="$(date +%s)"
if [[ -f "$state/retry-after" ]] && (( now < $(cat "$state/retry-after") )); then
  echo "codex-launch: usage limit; next start after $(date -d "@$(cat "$state/retry-after")" '+%F %R')"; exit 0
fi

resume=""
if [[ -n "$(git -C "$worktree" status --porcelain --untracked-files=normal | grep -v 'graphify-out/' || true)" ]] ||
   [[ -n "$(git -C "$worktree" log --oneline origin/main..HEAD 2>/dev/null || true)" ]]; then
  resume="RESUME: a previous run of this same slice stopped early (usage limit or interruption). Your worktree already contains that work. Start with git status, git diff and git log origin/main..HEAD, keep what is correct, finish the remaining steps, rebuild, run the tests and commit. Do not discard or redo completed work.

"
fi

prompt="${resume}$(cat "$here/implementer-brief.md")

Worktree: $worktree

$(cat "$contract")

Sandbox note: you run in the workspace-write sandbox. If tests needing sockets/D-Bus cannot run there, list their exact names in your report; the orchestrator runs the full suite outside the sandbox. If git commit is blocked by the sandbox, leave the work staged and say so."

echo running > "$state/status"
printf '\n===== %s start (%s) =====\n' "$(date -Is)" "${resume:+resume}${resume:-fresh}" >> "$log"
set +e
codex exec -s workspace-write -C "$worktree" -o "$state/last.md" "$prompt" >> "$log" 2>&1
code=$?
set -e

limit="$(tail -n 40 "$log" | grep -o 'usage limit.*try again at [0-9:]* *[AP]M' | tail -n 1 || true)"
if [[ -n "$limit" ]]; then
  at="$(sed -E 's/.*try again at ([0-9:]+ *[AP]M).*/\1/' <<<"$limit")"
  next="$(date -d "$at" +%s)"
  (( next <= $(date +%s) )) && next="$(date -d "tomorrow $at" +%s)"
  echo $(( next + 120 )) > "$state/retry-after"
  echo usage-limit > "$state/status"
  echo "codex-launch: usage limit; retry after $(date -d "@$next" '+%F %R')"
elif (( code == 0 )); then
  rm -f "$state/retry-after"; echo finished > "$state/status"; echo "codex-launch: finished"
else
  echo failed > "$state/status"; echo "codex-launch: codex exited $code"; exit "$code"
fi
